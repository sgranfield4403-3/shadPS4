// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <limits>
#include "common/assert.h"
#include "common/config.h"
#include "core/libraries/videoout/buffer.h"
#include "core/virtual_memory.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/texture_cache/texture_cache.h"
#include "video_core/texture_cache/tile_manager.h"

#ifndef _WIN64
#include <signal.h>
#include <sys/mman.h>

#define PAGE_NOACCESS PROT_NONE
#define PAGE_READWRITE (PROT_READ | PROT_WRITE)
#else
#include <windows.h>

void mprotect(void* addr, size_t len, int prot) {
    DWORD old_prot{};
    BOOL result = VirtualProtect(addr, len, prot, &old_prot);
    ASSERT_MSG(result != 0, "Region protection failed");
}

#endif

namespace VideoCore {

static TextureCache* g_texture_cache = nullptr;

#ifndef _WIN64
void GuestFaultSignalHandler(int sig, siginfo_t* info, void* raw_context) {
    ucontext_t* ctx = reinterpret_cast<ucontext_t*>(raw_context);
    const VAddr address = reinterpret_cast<VAddr>(info->si_addr);
    if (ctx->uc_mcontext.gregs[REG_ERR] & 0x2) {
        g_texture_cache->OnCpuWrite(address);
    } else {
        // Read not supported!
        UNREACHABLE();
    }
}
#else
LONG WINAPI GuestFaultSignalHandler(EXCEPTION_POINTERS* pExp) noexcept {
    const u32 ec = pExp->ExceptionRecord->ExceptionCode;
    if (ec == EXCEPTION_ACCESS_VIOLATION) {
        const auto info = pExp->ExceptionRecord->ExceptionInformation;
        if (info[0] == 1) { // Write violation
            g_texture_cache->OnCpuWrite(info[1]);
            return EXCEPTION_CONTINUE_EXECUTION;
        } /* else {
            UNREACHABLE();
        }*/
    }
    return EXCEPTION_CONTINUE_SEARCH; // pass further
}
#endif

static constexpr u64 StreamBufferSize = 128_MB;
static constexpr u64 PageShift = 12;

TextureCache::TextureCache(const Vulkan::Instance& instance_, Vulkan::Scheduler& scheduler_)
    : instance{instance_}, scheduler{scheduler_},
      staging{instance, scheduler, vk::BufferUsageFlagBits::eTransferSrc, StreamBufferSize,
              Vulkan::BufferType::Upload} {

#ifndef _WIN64
    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGSEGV);

    using HandlerType = decltype(sigaction::sa_sigaction);

    struct sigaction guest_access_fault {};
    guest_access_fault.sa_flags = SA_SIGINFO | SA_ONSTACK;
    guest_access_fault.sa_sigaction = &GuestFaultSignalHandler;
    guest_access_fault.sa_mask = signal_mask;
    sigaction(SIGSEGV, &guest_access_fault, nullptr);
#else
    veh_handle = AddVectoredExceptionHandler(0, GuestFaultSignalHandler);
    ASSERT_MSG(veh_handle, "Failed to register an exception handler");
#endif
    g_texture_cache = this;
}

TextureCache::~TextureCache() {
#if _WIN64
    RemoveVectoredExceptionHandler(veh_handle);
#endif
}

void TextureCache::OnCpuWrite(VAddr address) {
    const VAddr address_aligned = address & ~((1 << PageBits) - 1);
    ForEachImageInRegion(address_aligned, 1 << PageBits, [&](ImageId image_id, Image& image) {
        // Ensure image is reuploaded when accessed again.
        image.flags |= ImageFlagBits::CpuModified;
        // Untrack image, so the range is unprotected and the guest can write freely.
        UntrackImage(image, image_id);
    });
}

Image& TextureCache::FindDisplayBuffer(const Libraries::VideoOut::BufferAttributeGroup& group,
                                       VAddr cpu_address) {
    boost::container::small_vector<ImageId, 2> image_ids;
    ForEachImageInRegion(cpu_address, group.size_in_bytes, [&](ImageId image_id, Image& image) {
        if (image.cpu_addr == cpu_address) {
            image_ids.push_back(image_id);
        }
    });

    ASSERT_MSG(image_ids.size() <= 1, "Overlapping framebuffers not allowed!");

    ImageId image_id{};
    if (image_ids.empty()) {
        image_id = slot_images.insert(instance, scheduler, ImageInfo{group}, cpu_address);
        RegisterImage(image_id);
    } else {
        image_id = image_ids[0];
    }

    Image& image = slot_images[image_id];
    if (True(image.flags & ImageFlagBits::CpuModified)) {
        RefreshImage(image);
        TrackImage(image, image_id);
    }

    return image;
}

void TextureCache::RefreshImage(Image& image) {
    // Mark image as validated.
    image.flags &= ~ImageFlagBits::CpuModified;

    // Upload data to the staging buffer.
    const auto [data, offset, _] = staging.Map(image.info.guest_size_bytes, 0);
    const u8* image_data = reinterpret_cast<const u8*>(image.cpu_addr);
    if (image.info.is_tiled) {
        ConvertTileToLinear(data, image_data, image.info.size.width, image.info.size.height,
                            Config::isNeoMode());
    } else {
        std::memcpy(data, image_data, image.info.guest_size_bytes);
    }
    staging.Commit(image.info.guest_size_bytes);

    // Copy to the image.
    const vk::BufferImageCopy image_copy = {
        .bufferOffset = offset,
        .bufferRowLength = 0,
        .bufferImageHeight = 0,
        .imageSubresource{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
        .imageOffset = {0, 0, 0},
        .imageExtent = {image.info.size.width, image.info.size.height, 1},
    };

    const auto cmdbuf = scheduler.CommandBuffer();
    const vk::ImageSubresourceRange range = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .baseMipLevel = 0,
        .levelCount = 1,
        .baseArrayLayer = 0,
        .layerCount = VK_REMAINING_ARRAY_LAYERS,
    };
    const vk::ImageMemoryBarrier read_barrier = {
        .srcAccessMask = vk::AccessFlagBits::eShaderRead,
        .dstAccessMask = vk::AccessFlagBits::eTransferWrite,
        .oldLayout = vk::ImageLayout::eGeneral,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange = range,
    };
    const vk::ImageMemoryBarrier write_barrier = {
        .srcAccessMask = vk::AccessFlagBits::eTransferWrite,
        .dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eGeneral,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image.image,
        .subresourceRange = range,
    };

    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eAllGraphics,
                           vk::PipelineStageFlagBits::eTransfer, vk::DependencyFlagBits::eByRegion,
                           {}, {}, read_barrier);
    cmdbuf.copyBufferToImage(staging.Handle(), image.image, vk::ImageLayout::eTransferDstOptimal,
                             image_copy);
    cmdbuf.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                           vk::PipelineStageFlagBits::eAllGraphics,
                           vk::DependencyFlagBits::eByRegion, {}, {}, write_barrier);
}

void TextureCache::RegisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(False(image.flags & ImageFlagBits::Registered),
               "Trying to register an already registered image");
    image.flags |= ImageFlagBits::Registered;
    ForEachPage(image.cpu_addr, image.info.guest_size_bytes,
                [this, image_id](u64 page) { page_table[page].push_back(image_id); });
}

void TextureCache::UnregisterImage(ImageId image_id) {
    Image& image = slot_images[image_id];
    ASSERT_MSG(True(image.flags & ImageFlagBits::Registered),
               "Trying to unregister an already registered image");
    image.flags &= ~ImageFlagBits::Registered;
    ForEachPage(image.cpu_addr, image.info.guest_size_bytes, [this, image_id](u64 page) {
        const auto page_it = page_table.find(page);
        if (page_it == page_table.end()) {
            ASSERT_MSG(false, "Unregistering unregistered page=0x{:x}", page << PageBits);
            return;
        }
        auto& image_ids = page_it.value();
        const auto vector_it = std::ranges::find(image_ids, image_id);
        if (vector_it == image_ids.end()) {
            ASSERT_MSG(false, "Unregistering unregistered image in page=0x{:x}", page << PageBits);
            return;
        }
        image_ids.erase(vector_it);
    });
    slot_images.erase(image_id);
}

void TextureCache::TrackImage(Image& image, ImageId image_id) {
    if (True(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    image.flags |= ImageFlagBits::Tracked;
    UpdatePagesCachedCount(image.cpu_addr, image.info.guest_size_bytes, 1);
}

void TextureCache::UntrackImage(Image& image, ImageId image_id) {
    if (False(image.flags & ImageFlagBits::Tracked)) {
        return;
    }
    image.flags &= ~ImageFlagBits::Tracked;
    UpdatePagesCachedCount(image.cpu_addr, image.info.guest_size_bytes, -1);
}

void TextureCache::UpdatePagesCachedCount(VAddr addr, u64 size, s32 delta) {
    const u64 num_pages = ((addr + size - 1) >> PageShift) - (addr >> PageShift) + 1;
    const u64 page_start = addr >> PageShift;
    const u64 page_end = page_start + num_pages;

    const auto pages_interval =
        decltype(cached_pages)::interval_type::right_open(page_start, page_end);
    if (delta > 0) {
        cached_pages.add({pages_interval, delta});
    }

    const auto& range = cached_pages.equal_range(pages_interval);
    for (const auto& [range, count] : boost::make_iterator_range(range)) {
        const auto interval = range & pages_interval;
        const VAddr interval_start_addr = boost::icl::first(interval) << PageShift;
        const VAddr interval_end_addr = boost::icl::last_next(interval) << PageShift;
        const u32 interval_size = interval_end_addr - interval_start_addr;
        void* addr = reinterpret_cast<void*>(interval_start_addr);
        if (delta > 0 && count == delta) {
            mprotect(addr, interval_size, PAGE_NOACCESS);
        } else if (delta < 0 && count == -delta) {
            mprotect(addr, interval_size, PAGE_READWRITE);
        } else {
            ASSERT(count >= 0);
        }
    }

    if (delta < 0) {
        cached_pages.add({pages_interval, delta});
    }
}

} // namespace VideoCore
