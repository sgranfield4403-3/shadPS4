#include "LibKernel.h"

#include <Util/log.h>
#include <debug.h>
#include <windows.h>

#include "Emulator/Util/singleton.h"
#include "../Loader/Elf.h"
#include "Kernel/Objects/physical_memory.h"
#include "Kernel/cpu_management.h"
#include "Kernel/event_queues.h"
#include "Kernel/memory_management.h"
#include "Libs.h"
#include "Emulator/HLE/Libraries/LibKernel/FileSystem/file_system.h"
#include "Emulator/HLE/Libraries/LibKernel/FileSystem/posix_file_system.h"

namespace HLE::Libs::LibKernel {

static u64 g_stack_chk_guard = 0xDEADBEEF54321ABC;  // dummy return

int32_t PS4_SYSV_ABI sceKernelReleaseDirectMemory(off_t start, size_t len) {
    BREAKPOINT();
    return 0;
}

static PS4_SYSV_ABI void stack_chk_fail() { BREAKPOINT(); }
u64 PS4_SYSV_ABI sceKernelReadTsc() {
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return c.QuadPart;
}
int PS4_SYSV_ABI sceKernelMunmap(void* addr, size_t len) { BREAKPOINT(); }
void LibKernel_Register(SymbolsResolver* sym) {
    // obj
    LIB_OBJ("f7uOxY9mM1U", "libkernel", 1, "libkernel", 1, 1, &HLE::Libs::LibKernel::g_stack_chk_guard);
    // memory
    LIB_FUNCTION("rTXw65xmLIA", "libkernel", 1, "libkernel", 1, 1, MemoryManagement::sceKernelAllocateDirectMemory);
    LIB_FUNCTION("pO96TwzOm5E", "libkernel", 1, "libkernel", 1, 1, MemoryManagement::sceKernelGetDirectMemorySize);
    LIB_FUNCTION("L-Q3LEjIbgA", "libkernel", 1, "libkernel", 1, 1, MemoryManagement::sceKernelMapDirectMemory);
    LIB_FUNCTION("MBuItvba6z8", "libkernel", 1, "libkernel", 1, 1, sceKernelReleaseDirectMemory);
    LIB_FUNCTION("cQke9UuBQOk", "libkernel", 1, "libkernel", 1, 1, sceKernelMunmap);
    // equeue
    LIB_FUNCTION("D0OdFMjp46I", "libkernel", 1, "libkernel", 1, 1, EventQueues::sceKernelCreateEqueue);
    LIB_FUNCTION("fzyMKs9kim0", "libkernel", 1, "libkernel", 1, 1, EventQueues::sceKernelWaitEqueue);
    // misc
    LIB_FUNCTION("WslcK1FQcGI", "libkernel", 1, "libkernel", 1, 1, CPUManagement::sceKernelIsNeoMode);
    LIB_FUNCTION("Ou3iL1abvng", "libkernel", 1, "libkernel", 1, 1, stack_chk_fail);
    // time
    LIB_FUNCTION("-2IRUCO--PM", "libkernel", 1, "libkernel", 1, 1, sceKernelReadTsc);
    // fs
    LIB_FUNCTION("1G3lF1Gg1k8", "libkernel", 1, "libkernel", 1, 1, Emulator::HLE::Libraries::LibKernel::FileSystem::sceKernelOpen);
    LIB_FUNCTION("wuCroIGjt2g", "libScePosix", 1, "libkernel", 1, 1, Emulator::HLE::Libraries::LibKernel::FileSystem::POSIX::open);
}

};  // namespace HLE::Libs::LibKernel