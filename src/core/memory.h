// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <functional>
#include <mutex>
#include <string_view>
#include <vector>
#include <boost/icl/split_interval_map.hpp>
#include "common/enum.h"
#include "common/singleton.h"
#include "common/types.h"
#include "core/address_space.h"
#include "core/libraries/kernel/memory_management.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Vulkan {
class Instance;
}

namespace Libraries::Kernel {
struct OrbisQueryInfo;
}

namespace Core {

enum class MemoryProt : u32 {
    NoAccess = 0,
    CpuRead = 1,
    CpuReadWrite = 2,
    GpuRead = 16,
    GpuWrite = 32,
    GpuReadWrite = 38,
};

enum class MemoryMapFlags : u32 {
    NoFlags = 0,
    Shared = 1,
    Private = 2,
    Fixed = 0x10,
    NoOverwrite = 0x0080,
    NoSync = 0x800,
    NoCore = 0x20000,
    NoCoalesce = 0x400000,
};
DECLARE_ENUM_FLAG_OPERATORS(MemoryMapFlags)

enum class VMAType : u32 {
    Free = 0,
    Reserved = 1,
    Direct = 2,
    Flexible = 3,
    Pooled = 4,
    Stack = 5,
    Code = 6,
    File = 7,
};

struct DirectMemoryArea {
    PAddr base = 0;
    size_t size = 0;
    int memory_type = 0;
    bool is_free = true;

    PAddr GetEnd() const {
        return base + size;
    }

    bool CanMergeWith(const DirectMemoryArea& next) const {
        if (base + size != next.base) {
            return false;
        }
        if (is_free != next.is_free) {
            return false;
        }
        return true;
    }
};

struct VirtualMemoryArea {
    VAddr base = 0;
    size_t size = 0;
    PAddr phys_base = 0;
    VMAType type = VMAType::Free;
    MemoryProt prot = MemoryProt::NoAccess;
    bool disallow_merge = false;
    std::string name = "";
    uintptr_t fd = 0;

    bool Contains(VAddr addr, size_t size) const {
        return addr >= base && (addr + size) < (base + this->size);
    }

    bool CanMergeWith(const VirtualMemoryArea& next) const {
        if (disallow_merge || next.disallow_merge) {
            return false;
        }
        if (base + size != next.base) {
            return false;
        }
        if (type == VMAType::Direct && phys_base + size != next.phys_base) {
            return false;
        }
        if (prot != next.prot || type != next.type) {
            return false;
        }
        return true;
    }
};

class MemoryManager {
    using DMemMap = std::map<PAddr, DirectMemoryArea>;
    using DMemHandle = DMemMap::iterator;

    using VMAMap = std::map<VAddr, VirtualMemoryArea>;
    using VMAHandle = VMAMap::iterator;

public:
    explicit MemoryManager();
    ~MemoryManager();

    void SetInstance(const Vulkan::Instance* instance_) {
        instance = instance_;
    }

    void SetTotalFlexibleSize(u64 size) {
        total_flexible_size = size;
    }

    u64 GetAvailableFlexibleSize() const {
        return total_flexible_size - flexible_usage;
    }

    PAddr Allocate(PAddr search_start, PAddr search_end, size_t size, u64 alignment,
                   int memory_type);

    void Free(PAddr phys_addr, size_t size);

    int MapMemory(void** out_addr, VAddr virtual_addr, size_t size, MemoryProt prot,
                  MemoryMapFlags flags, VMAType type, std::string_view name = "",
                  bool is_exec = false, PAddr phys_addr = -1, u64 alignment = 0);

    int MapFile(void** out_addr, VAddr virtual_addr, size_t size, MemoryProt prot,
                MemoryMapFlags flags, uintptr_t fd, size_t offset);

    void UnmapMemory(VAddr virtual_addr, size_t size);

    int QueryProtection(VAddr addr, void** start, void** end, u32* prot);

    int VirtualQuery(VAddr addr, int flags, Libraries::Kernel::OrbisVirtualQueryInfo* info);

    int DirectMemoryQuery(PAddr addr, bool find_next, Libraries::Kernel::OrbisQueryInfo* out_info);

    int DirectQueryAvailable(PAddr search_start, PAddr search_end, size_t alignment,
                             PAddr* phys_addr_out, size_t* size_out);

    std::pair<vk::Buffer, size_t> GetVulkanBuffer(VAddr addr);

private:
    VMAHandle FindVMA(VAddr target) {
        return std::prev(vma_map.upper_bound(target));
    }

    DMemHandle FindDmemArea(PAddr target) {
        return std::prev(dmem_map.upper_bound(target));
    }

    template <typename Handle>
    Handle MergeAdjacent(auto& handle_map, Handle iter) {
        const auto next_vma = std::next(iter);
        if (next_vma != handle_map.end() && iter->second.CanMergeWith(next_vma->second)) {
            iter->second.size += next_vma->second.size;
            handle_map.erase(next_vma);
        }

        if (iter != handle_map.begin()) {
            auto prev_vma = std::prev(iter);
            if (prev_vma->second.CanMergeWith(iter->second)) {
                prev_vma->second.size += iter->second.size;
                handle_map.erase(iter);
                iter = prev_vma;
            }
        }

        return iter;
    }

    VirtualMemoryArea& AddMapping(VAddr virtual_addr, size_t size);

    DirectMemoryArea& AddDmemAllocation(PAddr addr, size_t size);

    VMAHandle Split(VMAHandle vma_handle, size_t offset_in_vma);

    DMemHandle Split(DMemHandle dmem_handle, size_t offset_in_area);

    void MapVulkanMemory(VAddr addr, size_t size);

    void UnmapVulkanMemory(VAddr addr, size_t size);

private:
    AddressSpace impl;
    DMemMap dmem_map;
    VMAMap vma_map;
    std::recursive_mutex mutex;
    size_t total_flexible_size = 448_MB;
    size_t flexible_usage{};

    struct MappedMemory {
        vk::UniqueBuffer buffer;
        vk::UniqueDeviceMemory backing;
        size_t buffer_size;
    };
    std::map<VAddr, MappedMemory> mapped_memories;
    const Vulkan::Instance* instance{};
};

using Memory = Common::Singleton<MemoryManager>;

} // namespace Core
