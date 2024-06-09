// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <boost/icl/separate_interval_set.hpp>
#include "common/assert.h"
#include "common/error.h"
#include "core/address_space.h"
#include "core/libraries/kernel/memory_management.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace Core {

static constexpr size_t BackingSize = SCE_KERNEL_MAIN_DMEM_SIZE;

#ifdef _WIN32
struct AddressSpace::Impl {
    Impl() : process{GetCurrentProcess()} {
        // Allocate virtual address placeholder for our address space.
        MEM_ADDRESS_REQUIREMENTS req{};
        MEM_EXTENDED_PARAMETER param{};
        req.LowestStartingAddress = reinterpret_cast<PVOID>(SYSTEM_MANAGED_MIN);
        // The ending address must align to page boundary - 1
        // https://stackoverflow.com/questions/54223343/virtualalloc2-with-memextendedparameteraddressrequirements-always-produces-error
        req.HighestEndingAddress = reinterpret_cast<PVOID>(USER_MIN + UserSize - 1);
        req.Alignment = 0;
        param.Type = MemExtendedParameterAddressRequirements;
        param.Pointer = &req;

        // Typically, lower parts of system managed area is already reserved in windows.
        // If reservation fails attempt again by reducing the area size a little bit.
        // System managed is about 31GB in size so also cap the number of times we can reduce it
        // to a reasonable amount.
        static constexpr size_t ReductionOnFail = 1_GB;
        static constexpr size_t MaxReductions = 10;
        virtual_size = SystemSize + UserSize + ReductionOnFail;
        for (u32 i = 0; i < MaxReductions && !virtual_base; i++) {
            virtual_size -= ReductionOnFail;
            virtual_base = static_cast<u8*>(VirtualAlloc2(process, NULL, virtual_size,
                                                          MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                                          PAGE_NOACCESS, &param, 1));
        }
        ASSERT_MSG(virtual_base, "Unable to reserve virtual address space!");

        // Initializer placeholder tracker
        const uintptr_t virtual_addr = reinterpret_cast<uintptr_t>(virtual_base);
        placeholders.insert({virtual_addr, virtual_addr + virtual_size});

        // Allocate backing file that represents the total physical memory.
        backing_handle =
            CreateFileMapping2(INVALID_HANDLE_VALUE, nullptr, FILE_MAP_WRITE | FILE_MAP_READ,
                               PAGE_READWRITE, SEC_COMMIT, BackingSize, nullptr, nullptr, 0);
        ASSERT(backing_handle);
        // Allocate a virtual memory for the backing file map as placeholder
        backing_base = static_cast<u8*>(VirtualAlloc2(process, nullptr, BackingSize,
                                                      MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                                      PAGE_NOACCESS, nullptr, 0));
        // Map backing placeholder. This will commit the pages
        void* const ret = MapViewOfFile3(backing_handle, process, backing_base, 0, BackingSize,
                                         MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE, nullptr, 0);
        ASSERT(ret == backing_base);
    }

    ~Impl() {
        if (virtual_base) {
            if (!VirtualFree(virtual_base, 0, MEM_RELEASE)) {
                LOG_CRITICAL(Render, "Failed to free virtual memory");
            }
        }
        if (backing_base) {
            if (!UnmapViewOfFile2(process, backing_base, MEM_PRESERVE_PLACEHOLDER)) {
                LOG_CRITICAL(Render, "Failed to unmap backing memory placeholder");
            }
            if (!VirtualFreeEx(process, backing_base, 0, MEM_RELEASE)) {
                LOG_CRITICAL(Render, "Failed to free backing memory");
            }
        }
        if (!CloseHandle(backing_handle)) {
            LOG_CRITICAL(Render, "Failed to free backing memory file handle");
        }
    }

    void* Map(VAddr virtual_addr, PAddr phys_addr, size_t size, ULONG prot) {
        const auto it = placeholders.find(virtual_addr);
        ASSERT_MSG(it != placeholders.end(), "Cannot map already mapped region");
        ASSERT_MSG(virtual_addr >= it->lower() && virtual_addr + size <= it->upper(),
                   "Map range must be fully contained in a placeholder");

        // Windows only allows splitting a placeholder into two.
        // This means that if the map range is fully
        // contained the the placeholder we need to perform two split operations,
        // one at the start and at the end.
        const VAddr placeholder_start = it->lower();
        const VAddr placeholder_end = it->upper();
        const VAddr virtual_end = virtual_addr + size;

        // If the placeholder doesn't exactly start at virtual_addr, split it at the start.
        if (placeholder_start != virtual_addr) {
            VirtualFreeEx(process, reinterpret_cast<LPVOID>(placeholder_start),
                          virtual_addr - placeholder_start, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        }

        // If the placeholder doesn't exactly end at virtual_end, split it at the end.
        if (placeholder_end != virtual_end) {
            VirtualFreeEx(process, reinterpret_cast<LPVOID>(virtual_end),
                          placeholder_end - virtual_end, MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        }

        // Remove the placeholder.
        placeholders.erase({virtual_addr, virtual_end});

        // Perform the map.
        void* ptr = nullptr;
        if (phys_addr != -1) {
            ptr = MapViewOfFile3(backing_handle, process, reinterpret_cast<PVOID>(virtual_addr),
                                 phys_addr, size, MEM_REPLACE_PLACEHOLDER, prot, nullptr, 0);
        } else {
            ptr =
                VirtualAlloc2(process, reinterpret_cast<PVOID>(virtual_addr), size,
                              MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER, prot, nullptr, 0);
        }
        ASSERT_MSG(ptr, "{}", Common::GetLastErrorMsg());
        return ptr;
    }

    void Unmap(VAddr virtual_addr, PAddr phys_addr, size_t size) {
        bool ret;
        if (phys_addr != -1) {
            ret = UnmapViewOfFile2(process, reinterpret_cast<PVOID>(virtual_addr),
                                   MEM_PRESERVE_PLACEHOLDER);
        } else {
            ret = VirtualFreeEx(process, reinterpret_cast<PVOID>(virtual_addr), size,
                                MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
        }
        ASSERT_MSG(ret, "Unmap operation on virtual_addr={:#X} failed: {}", virtual_addr,
                   Common::GetLastErrorMsg());

        // The unmap call will create a new placeholder region. We need to see if we can coalesce it
        // with neighbors.
        VAddr placeholder_start = virtual_addr;
        VAddr placeholder_end = virtual_addr + size;

        // Check if a placeholder exists right before us.
        const auto left_it = placeholders.find(virtual_addr - 1);
        if (left_it != placeholders.end()) {
            ASSERT_MSG(left_it->upper() == virtual_addr,
                       "Left placeholder does not end at virtual_addr!");
            placeholder_start = left_it->lower();
            VirtualFreeEx(process, reinterpret_cast<LPVOID>(placeholder_start),
                          placeholder_end - placeholder_start,
                          MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS);
        }

        // Check if a placeholder exists right after us.
        const auto right_it = placeholders.find(placeholder_end + 1);
        if (right_it != placeholders.end()) {
            ASSERT_MSG(right_it->lower() == placeholder_end,
                       "Right placeholder does not start at virtual_end!");
            placeholder_end = right_it->upper();
            VirtualFreeEx(process, reinterpret_cast<LPVOID>(placeholder_start),
                          placeholder_end - placeholder_start,
                          MEM_RELEASE | MEM_COALESCE_PLACEHOLDERS);
        }

        // Insert the new placeholder.
        placeholders.insert({placeholder_start, placeholder_end});
    }

    void Protect(VAddr virtual_addr, size_t size, bool read, bool write, bool execute) {
        DWORD new_flags{};
        if (read && write) {
            new_flags = PAGE_READWRITE;
        } else if (read && !write) {
            new_flags = PAGE_READONLY;
        } else if (!read && !write) {
            new_flags = PAGE_NOACCESS;
        } else {
            UNIMPLEMENTED_MSG("Protection flag combination read={} write={}", read, write);
        }

        const VAddr virtual_end = virtual_addr + size;
        auto [it, end] = placeholders.equal_range({virtual_addr, virtual_end});
        while (it != end) {
            const size_t offset = std::max(it->lower(), virtual_addr);
            const size_t protect_length = std::min(it->upper(), virtual_end) - offset;
            DWORD old_flags{};
            if (!VirtualProtect(virtual_base + offset, protect_length, new_flags, &old_flags)) {
                LOG_CRITICAL(Common_Memory, "Failed to change virtual memory protect rules");
            }
            ++it;
        }
    }

    HANDLE process{};
    HANDLE backing_handle{};
    u8* backing_base{};
    u8* virtual_base{};
    size_t virtual_size{};
    boost::icl::separate_interval_set<uintptr_t> placeholders;
};
#else

enum PosixPageProtection {
    PAGE_NOACCESS = 0,
    PAGE_READONLY = PROT_READ,
    PAGE_READWRITE = PROT_READ | PROT_WRITE,
    PAGE_EXECUTE = PROT_EXEC,
    PAGE_EXECUTE_READ = PROT_EXEC | PROT_READ,
    PAGE_EXECUTE_READWRITE = PROT_EXEC | PROT_READ | PROT_WRITE
};

struct AddressSpace::Impl {
    Impl() {
        UNREACHABLE();
    }

    void* MapUser(VAddr virtual_addr, PAddr phys_addr, size_t size, PosixPageProtection prot) {
        UNREACHABLE();
        return nullptr;
    }

    void* MapPrivate(VAddr virtual_addr, size_t size, u64 alignment, PosixPageProtection prot) {
        UNREACHABLE();
        return nullptr;
    }

    void UnmapUser(VAddr virtual_addr, size_t size) {
        UNREACHABLE();
    }

    void UnmapPrivate(VAddr virtual_addr, size_t size) {
        UNREACHABLE();
    }

    void Protect(VAddr virtual_addr, size_t size, bool read, bool write, bool execute) {
        UNREACHABLE();
    }

    u8* backing_base{};
    u8* virtual_base{};
};
#endif

AddressSpace::AddressSpace() : impl{std::make_unique<Impl>()} {
    virtual_base = impl->virtual_base;
    backing_base = impl->backing_base;
    virtual_size = impl->virtual_size;
}

AddressSpace::~AddressSpace() = default;

void* AddressSpace::Map(VAddr virtual_addr, size_t size, u64 alignment, PAddr phys_addr,
                        bool is_exec) {
    return impl->Map(virtual_addr, phys_addr, size,
                     is_exec ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE);
}

void AddressSpace::Unmap(VAddr virtual_addr, size_t size, PAddr phys_addr) {
    return impl->Unmap(virtual_addr, phys_addr, size);
}

void AddressSpace::Protect(VAddr virtual_addr, size_t size, MemoryPermission perms) {
    return impl->Protect(virtual_addr, size, true, true, true);
}

} // namespace Core
