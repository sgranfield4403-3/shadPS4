// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <thread>
#include "common/alignment.h"
#include "common/arch.h"
#include "common/assert.h"
#include "common/error.h"
#include "video_core/page_manager.h"
#include "video_core/renderer_vulkan/vk_rasterizer.h"

#ifndef _WIN64
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#ifdef ENABLE_USERFAULTFD
#include <linux/userfaultfd.h>
#endif
#else
#include <windows.h>
#endif

namespace VideoCore {

constexpr size_t PAGESIZE = 4_KB;
constexpr size_t PAGEBITS = 12;

#ifdef _WIN64
struct PageManager::Impl {
    Impl(Vulkan::Rasterizer* rasterizer_) {
        rasterizer = rasterizer_;

        veh_handle = AddVectoredExceptionHandler(0, GuestFaultSignalHandler);
        ASSERT_MSG(veh_handle, "Failed to register an exception handler");
    }

    void OnMap(VAddr address, size_t size) {}

    void OnUnmap(VAddr address, size_t size) {}

    void Protect(VAddr address, size_t size, bool allow_write) {
        DWORD prot = allow_write ? PAGE_READWRITE : PAGE_READONLY;
        DWORD old_prot{};
        BOOL result = VirtualProtect(std::bit_cast<LPVOID>(address), size, prot, &old_prot);
        ASSERT_MSG(result != 0, "Region protection failed");
    }

    static LONG WINAPI GuestFaultSignalHandler(EXCEPTION_POINTERS* pExp) noexcept {
        const u32 ec = pExp->ExceptionRecord->ExceptionCode;
        if (ec == EXCEPTION_ACCESS_VIOLATION) {
            const auto info = pExp->ExceptionRecord->ExceptionInformation;
            if (info[0] == 1) { // Write violation
                const VAddr addr_aligned = Common::AlignDown(info[1], PAGESIZE);
                rasterizer->InvalidateMemory(addr_aligned, PAGESIZE);
                return EXCEPTION_CONTINUE_EXECUTION;
            } /* else {
                UNREACHABLE();
            }*/
        }
        return EXCEPTION_CONTINUE_SEARCH; // pass further
    }

    inline static Vulkan::Rasterizer* rasterizer;
    void* veh_handle{};
};
#elif ENABLE_USERFAULTFD
struct PageManager::Impl {
    Impl(Vulkan::Rasterizer* rasterizer_) : rasterizer{rasterizer_} {
        uffd = syscall(__NR_userfaultfd, O_CLOEXEC | O_NONBLOCK);
        ASSERT_MSG(uffd != -1, "{}", Common::GetLastErrorMsg());

        // Request uffdio features from kernel.
        uffdio_api api;
        api.api = UFFD_API;
        api.features = UFFD_FEATURE_THREAD_ID;
        const int ret = ioctl(uffd, UFFDIO_API, &api);
        ASSERT(ret == 0 && api.api == UFFD_API);

        // Create uffd handler thread
        ufd_thread = std::jthread([&](std::stop_token token) { UffdHandler(token); });
    }

    void OnMap(VAddr address, size_t size) {
        uffdio_register reg;
        reg.range.start = address;
        reg.range.len = size;
        reg.mode = UFFDIO_REGISTER_MODE_WP;
        const int ret = ioctl(uffd, UFFDIO_REGISTER, &reg);
        ASSERT_MSG(ret != -1, "Uffdio register failed");
    }

    void OnUnmap(VAddr address, size_t size) {
        uffdio_range range;
        range.start = address;
        range.len = size;
        const int ret = ioctl(uffd, UFFDIO_UNREGISTER, &range);
        ASSERT_MSG(ret != -1, "Uffdio unregister failed");
    }

    void Protect(VAddr address, size_t size, bool allow_write) {
        uffdio_writeprotect wp;
        wp.range.start = address;
        wp.range.len = size;
        wp.mode = allow_write ? 0 : UFFDIO_WRITEPROTECT_MODE_WP;
        const int ret = ioctl(uffd, UFFDIO_WRITEPROTECT, &wp);
        ASSERT_MSG(ret != -1, "Uffdio writeprotect failed with error: {}",
                   Common::GetLastErrorMsg());
    }

    void UffdHandler(std::stop_token token) {
        while (!token.stop_requested()) {
            pollfd pollfd;
            pollfd.fd = uffd;
            pollfd.events = POLLIN;

            // Block until the descriptor is ready for data reads.
            const int pollres = poll(&pollfd, 1, -1);
            switch (pollres) {
            case -1:
                perror("Poll userfaultfd");
                continue;
                break;
            case 0:
                continue;
            case 1:
                break;
            default:
                UNREACHABLE_MSG("Unexpected number of descriptors {} out of poll", pollres);
            }

            // We don't want an error condition to have occured.
            ASSERT_MSG(!(pollfd.revents & POLLERR), "POLLERR on userfaultfd");

            // We waited until there is data to read, we don't care about anything else.
            if (!(pollfd.revents & POLLIN)) {
                continue;
            }

            // Read message from kernel.
            uffd_msg msg;
            const int readret = read(uffd, &msg, sizeof(msg));
            ASSERT_MSG(readret != -1 || errno == EAGAIN, "Unexpected result of uffd read");
            if (errno == EAGAIN) {
                continue;
            }
            ASSERT_MSG(readret == sizeof(msg), "Unexpected short read, exiting");
            ASSERT(msg.arg.pagefault.flags & UFFD_PAGEFAULT_FLAG_WP);

            // Notify rasterizer about the fault.
            const VAddr addr = msg.arg.pagefault.address;
            const VAddr addr_page = Common::AlignDown(addr, PAGESIZE);
            rasterizer->InvalidateMemory(addr_page, PAGESIZE);
        }
    }

    Vulkan::Rasterizer* rasterizer;
    std::jthread ufd_thread;
    int uffd;
};
#else

#if defined(__APPLE__)

#if defined(ARCH_X86_64)
#define IS_WRITE_ERROR(ctx) ((ctx)->uc_mcontext->__es.__err & 0x2)
#elif defined(ARCH_ARM64)
#define IS_WRITE_ERROR(ctx) ((ctx)->uc_mcontext->__es.__esr & 0x40)
#endif

#else

#if defined(ARCH_X86_64)
#define IS_WRITE_ERROR(ctx) ((ctx)->uc_mcontext.gregs[REG_ERR] & 0x2)
#endif

#endif

#ifndef IS_WRITE_ERROR
#error "Missing IS_WRITE_ERROR() implementation for target OS and CPU architecture.
#endif

struct PageManager::Impl {
    Impl(Vulkan::Rasterizer* rasterizer_) {
        rasterizer = rasterizer_;

#ifdef __APPLE__
        // Read-only memory write results in SIGBUS on Apple.
        static constexpr int SignalType = SIGBUS;
#else
        static constexpr int SignalType = SIGSEGV;
#endif
        sigset_t signal_mask;
        sigemptyset(&signal_mask);
        sigaddset(&signal_mask, SignalType);

        using HandlerType = decltype(sigaction::sa_sigaction);

        struct sigaction guest_access_fault {};
        guest_access_fault.sa_flags = SA_SIGINFO | SA_ONSTACK;
        guest_access_fault.sa_sigaction = &GuestFaultSignalHandler;
        guest_access_fault.sa_mask = signal_mask;
        sigaction(SignalType, &guest_access_fault, nullptr);
    }

    void OnMap(VAddr address, size_t size) {}

    void OnUnmap(VAddr address, size_t size) {}

    void Protect(VAddr address, size_t size, bool allow_write) {
        mprotect(reinterpret_cast<void*>(address), size,
                 PROT_READ | (allow_write ? PROT_WRITE : 0));
    }

    static void GuestFaultSignalHandler(int sig, siginfo_t* info, void* raw_context) {
        ucontext_t* ctx = reinterpret_cast<ucontext_t*>(raw_context);
        const VAddr address = reinterpret_cast<VAddr>(info->si_addr);
        if (IS_WRITE_ERROR(ctx)) {
            const VAddr addr_aligned = Common::AlignDown(address, PAGESIZE);
            rasterizer->InvalidateMemory(addr_aligned, PAGESIZE);
        } else {
            // Read not supported!
            UNREACHABLE();
        }
    }

    inline static Vulkan::Rasterizer* rasterizer;
};
#endif

PageManager::PageManager(Vulkan::Rasterizer* rasterizer_)
    : impl{std::make_unique<Impl>(rasterizer_)}, rasterizer{rasterizer_} {}

PageManager::~PageManager() = default;

void PageManager::OnGpuMap(VAddr address, size_t size) {
    impl->OnMap(address, size);
}

void PageManager::OnGpuUnmap(VAddr address, size_t size) {
    impl->OnUnmap(address, size);
}

void PageManager::UpdatePagesCachedCount(VAddr addr, u64 size, s32 delta) {
    static constexpr u64 PageShift = 12;

    std::scoped_lock lk{mutex};
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
        if (delta > 0 && count == delta) {
            impl->Protect(interval_start_addr, interval_size, false);
        } else if (delta < 0 && count == -delta) {
            impl->Protect(interval_start_addr, interval_size, true);
        } else {
            ASSERT(count >= 0);
        }
    }

    if (delta < 0) {
        cached_pages.add({pages_interval, delta});
    }
}

} // namespace VideoCore
