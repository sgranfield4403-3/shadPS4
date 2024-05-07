// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "common/assert.h"
#include "common/logging/log.h"
#include "core/libraries/error_codes.h"
#include "core/libraries/gnmdriver/gnmdriver.h"
#include "core/libraries/libs.h"
#include "video_core/amdgpu/liverpool.h"
#include "video_core/amdgpu/pm4_cmds.h"
#include "video_core/renderer_vulkan/renderer_vulkan.h"

namespace Libraries::GnmDriver {

using namespace AmdGpu;

static std::unique_ptr<AmdGpu::Liverpool> liverpool;

// In case of precise gnm driver emulation we need to send a bunch of HW-specific
// initialization commands. It may slowdown development at early stage as their
// support is not important and can be ignored for a while.
static constexpr bool g_fair_hw_init = false;

// Write a special ending NOP packet with N DWs data block
template <u32 data_block_size>
static inline u32* WriteTrailingNop(u32* cmdbuf) {
    auto* nop = reinterpret_cast<PM4CmdNop*>(cmdbuf);
    nop->header = PM4Type3Header{PM4ItOpcode::Nop, data_block_size - 1};
    nop->data_block[0] = 0; // only one out of `data_block_size` is initialized
    return cmdbuf + data_block_size + 1 /* header */;
}

s32 PS4_SYSV_ABI sceGnmAddEqEvent(SceKernelEqueue eq, u64 id, void* udata) {
    LOG_TRACE(Lib_GnmDriver, "called");
    ASSERT_MSG(id == SceKernelEvent::Type::GfxEop);

    if (!eq) {
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    EqueueEvent kernel_event{};
    kernel_event.event.ident = id;
    kernel_event.event.filter = EVFILT_GRAPHICS_CORE;
    kernel_event.event.flags = 1;
    kernel_event.event.fflags = 0;
    kernel_event.event.data = id;
    kernel_event.event.udata = udata;
    eq->addEvent(kernel_event);

    liverpool->eop_callback = [=]() {
        eq->triggerEvent(SceKernelEvent::Type::GfxEop, EVFILT_GRAPHICS_CORE, nullptr);
    };
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmAreSubmitsAllowed() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmBeginWorkload() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmComputeWaitOnAddress(u32* cmdbuf, u32 size, uintptr_t addr, u32 mask,
                                            u32 cmp_func, u32 ref) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 0xe)) {
        cmdbuf = WriteHeader<PM4ItOpcode::Nop>(cmdbuf, 3);
        cmdbuf = WriteBody(cmdbuf, 0u);
        cmdbuf += 2;

        const u32 is_mem = addr > 0xffffu;
        const u32 addr_mask = is_mem ? 0xfffffffcu : 0xffffu;
        auto* wait_reg_mem = reinterpret_cast<PM4CmdWaitRegMem*>(cmdbuf);
        wait_reg_mem->header = PM4Type3Header{PM4ItOpcode::WaitRegMem, 5};
        wait_reg_mem->raw = (is_mem << 4u) | (cmp_func & 7u);
        wait_reg_mem->poll_addr_lo = u32(addr & addr_mask);
        wait_reg_mem->poll_addr_hi = u32(addr >> 32u);
        wait_reg_mem->ref = ref;
        wait_reg_mem->mask = mask;
        wait_reg_mem->poll_interval = 10;

        WriteTrailingNop<2>(cmdbuf + 7);
        return ORBIS_OK;
    }
    return -1;
}

int PS4_SYSV_ABI sceGnmComputeWaitSemaphore() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmCreateWorkloadStream() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerGetAddressWatch() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerHaltWavefront() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerReadGds() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerReadSqIndirectRegister() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerResumeWavefront() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerResumeWavefrontCreation() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerSetAddressWatch() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerWriteGds() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebuggerWriteSqIndirectRegister() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebugHardwareStatus() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmDeleteEqEvent(SceKernelEqueue eq, u64 id) {
    LOG_TRACE(Lib_GnmDriver, "called");
    ASSERT_MSG(id == SceKernelEvent::Type::GfxEop);

    if (!eq) {
        return ORBIS_KERNEL_ERROR_EBADF;
    }

    eq->removeEvent(id);
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDestroyWorkloadStream() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDingDong() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDingDongForWorkload() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDisableMipStatsReport() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmDispatchDirect(u32* cmdbuf, u32 size, u32 threads_x, u32 threads_y,
                                      u32 threads_z, u32 flags) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 9) && ((s32)(threads_x | threads_y | threads_z) > -1)) {
        const auto predicate = flags & 1 ? PM4Predicate::PredEnable : PM4Predicate::PredDisable;
        cmdbuf = WriteHeader<PM4ItOpcode::DispatchDirect>(cmdbuf, 4, PM4ShaderType::ShaderCompute,
                                                          predicate);
        cmdbuf = WriteBody(cmdbuf, threads_x, threads_y, threads_z);
        cmdbuf[0] = (flags & 0x18) + 1; // ordered append mode

        WriteTrailingNop<3>(cmdbuf + 1);
        return ORBIS_OK;
    }
    return -1;
}

s32 PS4_SYSV_ABI sceGnmDispatchIndirect(u32* cmdbuf, u32 size, u32 data_offset, u32 flags) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 7)) {
        const auto predicate = flags & 1 ? PM4Predicate::PredEnable : PM4Predicate::PredDisable;
        cmdbuf = WriteHeader<PM4ItOpcode::DispatchIndirect>(cmdbuf, 2, PM4ShaderType::ShaderCompute,
                                                            predicate);
        cmdbuf[0] = data_offset;
        cmdbuf[1] = (flags & 0x18) + 1; // ordered append mode

        WriteTrailingNop<3>(cmdbuf + 2);
        return ORBIS_OK;
    }
    return -1;
}

int PS4_SYSV_ABI sceGnmDispatchIndirectOnMec() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

u32 PS4_SYSV_ABI sceGnmDispatchInitDefaultHardwareState(u32* cmdbuf, u32 size) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (size > 0xff) {
        if constexpr (g_fair_hw_init) {
            cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x216u,
                                             0xffffffffu); // COMPUTE_STATIC_THREAD_MGMT_SE0
            cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x217u,
                                             0xffffffffu); // COMPUTE_STATIC_THREAD_MGMT_SE1
            cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x215u, 0x170u); // COMPUTE_RESOURCE_LIMITS

            cmdbuf = WriteHeader<PM4ItOpcode::Unknown58>(
                cmdbuf, 6); // for some reason the packet indicates larger size
            cmdbuf = WriteBody(cmdbuf, 0x28000000u, 0u, 0u, 0u, 0u);

            cmdbuf = WriteHeader<PM4ItOpcode::Nop>(cmdbuf, 0xef);
            cmdbuf = WriteBody(cmdbuf, 0xau, 0u);
        } else {
            cmdbuf = cmdbuf = WriteHeader<PM4ItOpcode::Nop>(cmdbuf, 0x100);
        }
        return 0x100; // it is a size, not a retcode
    }
    return 0;
}

s32 PS4_SYSV_ABI sceGnmDrawIndex(u32* cmdbuf, u32 size, u32 index_count, uintptr_t index_addr,
                                 u32 flags, u32 type) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 10) && (index_addr != 0) && (index_addr & 1) == 0 &&
        (flags & 0x1ffffffe) == 0) { // no predication will be set in the packet
        auto* draw_index = reinterpret_cast<PM4CmdDrawIndex2*>(cmdbuf);
        draw_index->header =
            PM4Type3Header{PM4ItOpcode::DrawIndex2, 4, PM4ShaderType::ShaderGraphics};
        draw_index->max_size = index_count;
        draw_index->index_base_lo = u32(index_addr);
        draw_index->index_base_hi = u32(index_addr >> 32);
        draw_index->index_count = index_count;
        draw_index->draw_initiator = 0;

        WriteTrailingNop<3>(cmdbuf + 6);
        return ORBIS_OK;
    }
    return -1;
}

s32 PS4_SYSV_ABI sceGnmDrawIndexAuto(u32* cmdbuf, u32 size, u32 index_count, u32 flags) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 7) &&
        (flags & 0x1ffffffe) == 0) { // no predication will be set in the packet
        cmdbuf = WritePacket<PM4ItOpcode::DrawIndexAuto>(cmdbuf, PM4ShaderType::ShaderGraphics,
                                                         index_count, 2u);
        WriteTrailingNop<3>(cmdbuf);
        return ORBIS_OK;
    }
    return -1;
}

int PS4_SYSV_ABI sceGnmDrawIndexIndirect() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawIndexIndirectCountMulti() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawIndexIndirectMulti() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawIndexMultiInstanced() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmDrawIndexOffset(u32* cmdbuf, u32 size, u32 index_offset, u32 index_count,
                                       u32 flags) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 9)) {
        const auto predicate = flags & 1 ? PM4Predicate::PredEnable : PM4Predicate::PredDisable;
        cmdbuf = WriteHeader<PM4ItOpcode::DrawIndexOffset2>(
            cmdbuf, 4, PM4ShaderType::ShaderGraphics, predicate);
        cmdbuf = WriteBody(cmdbuf, index_count, index_offset, index_count, 0u);

        WriteTrailingNop<3>(cmdbuf);
        return ORBIS_OK;
    }
    return -1;
}

int PS4_SYSV_ABI sceGnmDrawIndirect() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawIndirectCountMulti() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawIndirectMulti() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState175() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

u32 PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState200(u32* cmdbuf, u32 size) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (size > 0xff) {
        if constexpr (g_fair_hw_init) {
            ASSERT_MSG(0, "Not implemented");
        } else {
            cmdbuf = cmdbuf = WriteHeader<PM4ItOpcode::Nop>(cmdbuf, 0x100);
        }
        return 0x100; // it is a size, not a retcode
    }
    return 0;
}

u32 PS4_SYSV_ABI sceGnmDrawInitDefaultHardwareState350(u32* cmdbuf, u32 size) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (size > 0xff) {
        if constexpr (g_fair_hw_init) {
            ASSERT_MSG(0, "Not implemented");
        } else {
            cmdbuf = cmdbuf = WriteHeader<PM4ItOpcode::Nop>(cmdbuf, 0x100);
        }
        return 0x100; // it is a size, not a retcode
    }
    return 0;
}

int PS4_SYSV_ABI sceGnmDrawInitToDefaultContextState() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawInitToDefaultContextState400() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawOpaqueAuto() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverCaptureInProgress() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterface() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForGpuDebugger() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForGpuException() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForHDRScopes() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForReplay() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForResourceRegistration() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalRetrieveGnmInterfaceForValidation() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverInternalVirtualQuery() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverTraceInProgress() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDriverTriggerCapture() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmEndWorkload() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmFindResourcesPublic() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

void PS4_SYSV_ABI sceGnmFlushGarlic() {
    LOG_WARNING(Lib_GnmDriver, "(STUBBED) called");
}

int PS4_SYSV_ABI sceGnmGetCoredumpAddress() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetCoredumpMode() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetCoredumpProtectionFaultTimestamp() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetDbgGcHandle() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetDebugTimestamp() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetEqEventType() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetEqTimeStamp() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetGpuBlockStatus() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetGpuCoreClockFrequency() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetGpuInfoStatus() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetLastWaitedAddress() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetNumTcaUnits() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetOffChipTessellationBufferSize() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetOwnerName() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetPhysicalCounterFromVirtualized() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetProtectionFaultTimeStamp() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetResourceBaseAddressAndSizeInBytes() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetResourceName() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetResourceShaderGuid() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetResourceType() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetResourceUserData() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetShaderProgramBaseAddress() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetShaderStatus() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetTheTessellationFactorRingBufferBaseAddress() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGpuPaDebugEnter() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGpuPaDebugLeave() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmInsertDingDongMarker() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmInsertPopMarker(u32* cmdbuf, u32 size) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && (size == 6)) {
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(
            cmdbuf, PM4ShaderType::ShaderGraphics,
            static_cast<u32>(PM4CmdNop::PayloadType::DebugMarkerPop), 0u, 0u, 0u, 0u);
        return ORBIS_OK;
    }
    return -1;
}

int PS4_SYSV_ABI sceGnmInsertPushColorMarker() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmInsertPushMarker(u32* cmdbuf, u32 size, const char* marker) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (cmdbuf && marker) {
        const auto len = std::strlen(marker);
        const u32 packet_size = ((len + 8) >> 2) + ((len + 0xc) >> 3);
        if (packet_size + 2 == size) {
            auto* nop = reinterpret_cast<PM4CmdNop*>(cmdbuf);
            nop->header =
                PM4Type3Header{PM4ItOpcode::Nop, packet_size, PM4ShaderType::ShaderGraphics};
            nop->data_block[0] = static_cast<u32>(PM4CmdNop::PayloadType::DebugMarkerPush);
            const auto marker_len = len + 1;
            std::memcpy(&nop->data_block[1], marker, marker_len);
            std::memset(reinterpret_cast<u8*>(&nop->data_block[1]) + marker_len, 0,
                        packet_size * 4 - marker_len);
            return ORBIS_OK;
        }
    }
    return -1;
}

int PS4_SYSV_ABI sceGnmInsertSetColorMarker() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmInsertSetMarker() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmInsertThreadTraceMarker() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmInsertWaitFlipDone() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmIsCoredumpValid() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmIsUserPaEnabled() {
    LOG_TRACE(Lib_GnmDriver, "called");
    return 0; // PA Debug is always disabled in retail FW
}

int PS4_SYSV_ABI sceGnmLogicalCuIndexToPhysicalCuIndex() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmLogicalCuMaskToPhysicalCuMask() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmLogicalTcaUnitToPhysical() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmMapComputeQueue() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmMapComputeQueueWithPriority() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmPaDisableFlipCallbacks() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmPaEnableFlipCallbacks() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmPaHeartbeat() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmQueryResourceRegistrationUserMemoryRequirements() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRaiseUserExceptionEvent() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRegisterGdsResource() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRegisterGnmLiveCallbackConfig() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRegisterOwner() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRegisterResource() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRequestFlipAndSubmitDone() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRequestFlipAndSubmitDoneForWorkload() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRequestMipStatsReportAndReset() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmResetVgtControl() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaClose() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaConstFill() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaCopyLinear() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaCopyTiled() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaCopyWindow() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaFlush() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaGetMinCmdSize() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSdmaOpen() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmSetCsShader(u32* cmdbuf, u32 size, const u32* cs_regs) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x18) {
        return -1;
    }
    if (!cs_regs) {
        LOG_ERROR(Lib_GnmDriver, "Null pointer in shader registers.");
        return -1;
    }
    if (cs_regs[1] != 0) {
        LOG_ERROR(Lib_GnmDriver, "Invalid shader address.");
        return -1;
    }

    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x20cu, cs_regs[0],
                                     0u); // COMPUTE_PGM_LO/COMPUTE_PGM_HI
    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x212u, cs_regs[2],
                                     cs_regs[3]); // COMPUTE_PGM_RSRC1/COMPUTE_PGM_RSRC2
    cmdbuf = PM4CmdSetData::SetShReg(
        cmdbuf, 0x207u, cs_regs[4], cs_regs[5],
        cs_regs[6]); // COMPUTE_NUM_THREAD_X/COMPUTE_NUM_THREAD_Y/COMPUTE_NUM_THREAD_Z

    WriteTrailingNop<11>(cmdbuf);
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmSetCsShaderWithModifier(u32* cmdbuf, u32 size, const u32* cs_regs,
                                               u32 modifier) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x18) {
        return -1;
    }
    if (!cs_regs) {
        LOG_ERROR(Lib_GnmDriver, "Null pointer in shader registers.");
        return -1;
    }
    if ((modifier & 0xfffffc3fu) != 0) {
        LOG_ERROR(Lib_GnmDriver, "Invalid modifier mask.");
        return -1;
    }
    if (cs_regs[1] != 0) {
        LOG_ERROR(Lib_GnmDriver, "Invalid shader address.");
        return -1;
    }

    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x20cu, cs_regs[0],
                                     0u); // COMPUTE_PGM_LO/COMPUTE_PGM_HI
    const u32 rsrc1 = modifier == 0 ? cs_regs[2] : (cs_regs[2] & 0xfffffc3fu) | modifier;
    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x212u, rsrc1,
                                     cs_regs[3]); // COMPUTE_PGM_RSRC1/COMPUTE_PGM_RSRC2
    cmdbuf = PM4CmdSetData::SetShReg(
        cmdbuf, 0x207u, cs_regs[4], cs_regs[5],
        cs_regs[6]); // COMPUTE_NUM_THREAD_X/COMPUTE_NUM_THREAD_Y/COMPUTE_NUM_THREAD_Z

    WriteTrailingNop<11>(cmdbuf);
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetEmbeddedPsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetEmbeddedVsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetEsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetGsRingSizes() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetGsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetHsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetLsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmSetPsShader(u32* cmdbuf, u32 size, const u32* ps_regs) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x27) {
        return -1;
    }
    if (!ps_regs) {
        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, 0u,
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x203u, 0u); // DB_SHADER_CONTROL

        WriteTrailingNop<0x20>(cmdbuf);
    } else {
        if (ps_regs[1] != 0) {
            LOG_ERROR(Lib_GnmDriver, "Invalid shader address.");
            return -1;
        }

        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, ps_regs[0],
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = PM4CmdSetData::SetShReg(
            cmdbuf, 10u, ps_regs[2],
            ps_regs[3]); // SPI_SHADER_USER_DATA_PS_4/SPI_SHADER_USER_DATA_PS_5
        cmdbuf = PM4CmdSetData::SetContextReg(
            cmdbuf, 0x1c4u, ps_regs[4], ps_regs[5]); // SPI_SHADER_Z_FORMAT/SPI_SHADER_COL_FORMAT
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b3u, ps_regs[6],
                                              ps_regs[7]); // SPI_PS_INPUT_ENA/SPI_PS_INPUT_ADDR
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b6u, ps_regs[8]);  // SPI_PS_IN_CONTROL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b8u, ps_regs[9]);  // SPI_BARYC_CNTL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x203u, ps_regs[10]); // DB_SHADER_CONTROL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x8fu, ps_regs[11]);  // CB_SHADER_MASK

        WriteTrailingNop<11>(cmdbuf);
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmSetPsShader350(u32* cmdbuf, u32 size, const u32* ps_regs) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x27) {
        return -1;
    }
    if (!ps_regs) {
        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, 0u,
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x203u, 0u);  // DB_SHADER_CONTROL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x8fu, 0xfu); // CB_SHADER_MASK

        WriteTrailingNop<0x1d>(cmdbuf);
    } else {
        if (ps_regs[1] != 0) {
            LOG_ERROR(Lib_GnmDriver, "Invalid shader address.");
            return -1;
        }

        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, ps_regs[0],
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = PM4CmdSetData::SetShReg(
            cmdbuf, 10u, ps_regs[2],
            ps_regs[3]); // SPI_SHADER_USER_DATA_PS_4/SPI_SHADER_USER_DATA_PS_5
        cmdbuf = PM4CmdSetData::SetContextReg(
            cmdbuf, 0x1c4u, ps_regs[4], ps_regs[5]); // SPI_SHADER_Z_FORMAT/SPI_SHADER_COL_FORMAT
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b3u, ps_regs[6],
                                              ps_regs[7]); // SPI_PS_INPUT_ENA/SPI_PS_INPUT_ADDR
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b6u, ps_regs[8]);  // SPI_PS_IN_CONTROL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b8u, ps_regs[9]);  // SPI_BARYC_CNTL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x203u, ps_regs[10]); // DB_SHADER_CONTROL
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x8fu, ps_regs[11]);  // CB_SHADER_MASK

        WriteTrailingNop<11>(cmdbuf);
    }
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetResourceRegistrationUserMemory() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetResourceUserData() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetSpiEnableSqCounters() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetSpiEnableSqCountersForUnitInstance() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetupMipStatsReport() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetVgtControl() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmSetVsShader(u32* cmdbuf, u32 size, const u32* vs_regs, u32 shader_modifier) {
    if (!cmdbuf || size <= 0x1c) {
        return -1;
    }

    if (!vs_regs) {
        LOG_ERROR(Lib_GnmDriver, "Null pointer passed as argument");
        return -1;
    }

    if (shader_modifier & 0xfcfffc3f) {
        LOG_ERROR(Lib_GnmDriver, "Invalid modifier mask");
        return -1;
    }

    if (vs_regs[1] != 0) {
        LOG_ERROR(Lib_GnmDriver, "Invalid shader address");
        return -1;
    }

    const u32 var = shader_modifier == 0 ? vs_regs[2] : (vs_regs[2] & 0xfcfffc3f | shader_modifier);
    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x48u, vs_regs[0], 0u);   // SPI_SHADER_PGM_LO_VS
    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x4au, var, vs_regs[3]);  // SPI_SHADER_PGM_RSRC1_VS
    cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x207u, vs_regs[6]); // PA_CL_VS_OUT_CNTL
    cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1b1u, vs_regs[4]); // SPI_VS_OUT_CONFIG
    cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x1c3u, vs_regs[5]); // SPI_SHADER_POS_FORMAT

    WriteTrailingNop<11>(cmdbuf);

    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetWaveLimitMultiplier() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSetWaveLimitMultipliers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmEndSpm() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmInit() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmInit2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmSetDelay() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmSetMuxRam() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmSetMuxRam2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmSetSelectCounter() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmSetSpmSelects() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmSetSpmSelects2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSpmStartSpm() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttFini() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttFinishTrace() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetBcInfo() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetGpuClocks() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetHiWater() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetStatus() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetTraceCounter() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetTraceWptr() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetWrapCounts() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetWrapCounts2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttGetWritebackLabels() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttInit() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSelectMode() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSelectTarget() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSelectTokens() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetCuPerfMask() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetDceEventWrite() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetHiWater() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetTraceBuffer2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetTraceBuffers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetUserData() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSetUserdataTimer() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttStartTrace() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttStopTrace() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSwitchTraceBuffer() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttSwitchTraceBuffer2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSqttWaitForEvent() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSubmitAndFlipCommandBuffersForWorkload() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSubmitCommandBuffers(u32 count, void* dcbGpuAddrs[], u32* dcbSizesInBytes,
                                            void* ccbGpuAddrs[], u32* ccbSizesInBytes) {
    LOG_INFO(Lib_GnmDriver, "called");
    ASSERT_MSG(count == 1, "Multiple command buffer submission is unsupported!");

    if (!dcbGpuAddrs || !dcbSizesInBytes) {
        LOG_ERROR(Lib_GnmDriver, "dcbGpuAddrs and dcbSizesInBytes must not be NULL");
        return 0x80d11000;
    }

    for (u32 i = 0; i < count; i++) {
        if (dcbSizesInBytes[i] == 0) {
            LOG_ERROR(Lib_GnmDriver, "Submitting a null DCB {}", i);
            return 0x80d11000;
        }
        if (dcbSizesInBytes[i] > 0x3ffffc) {
            LOG_ERROR(Lib_GnmDriver, "dcbSizesInBytes[{}] ({}) is limited to (2*20)-1 DWORDS", i,
                      dcbSizesInBytes[i]);
            return 0x80d11000;
        }
        if (ccbSizesInBytes && ccbSizesInBytes[i] > 0x3ffffc) {
            LOG_ERROR(Lib_GnmDriver, "ccbSizesInBytes[{}] ({}) is limited to (2*20)-1 DWORDS", i,
                      ccbSizesInBytes[i]);
            return 0x80d11000;
        }
    }

    liverpool->ProcessCmdList(reinterpret_cast<u32*>(dcbGpuAddrs[0]), dcbSizesInBytes[0]);

    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSubmitCommandBuffersForWorkload() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmSubmitDone() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmUnmapComputeQueue() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmUnregisterAllResourcesForOwner() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmUnregisterOwnerAndResources() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmUnregisterResource() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmUpdateGsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmUpdateHsShader() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmUpdatePsShader(u32* cmdbuf, u32 size, const u32* ps_regs) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x27) {
        return -1;
    }
    if (!ps_regs) {
        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, 0u,
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e0203u,
                                               0u); // DB_SHADER_CONTROL update
        WriteTrailingNop<0x20>(cmdbuf);
    } else {
        if (ps_regs[1] != 0) {
            LOG_ERROR(Lib_GnmDriver, "Invalid shader address.");
            return -1;
        }

        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, ps_regs[0],
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = PM4CmdSetData::SetShReg(
            cmdbuf, 10u, ps_regs[2],
            ps_regs[3]); // SPI_SHADER_USER_DATA_PS_4/SPI_SHADER_USER_DATA_PS_5
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(
            cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01c4u, ps_regs[4],
            ps_regs[5]); // SPI_SHADER_Z_FORMAT/SPI_SHADER_COL_FORMAT update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(
            cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b3u, ps_regs[6],
            ps_regs[7]); // SPI_PS_INPUT_ENA/SPI_PS_INPUT_ADDR update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b6u,
                                               ps_regs[8]); // SPI_PS_IN_CONTROL update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b8u,
                                               ps_regs[9]); // SPI_BARYC_CNTL update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e0203u,
                                               ps_regs[10]); // DB_SHADER_CONTROL update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e008fu,
                                               ps_regs[11]); // CB_SHADER_MASK update

        WriteTrailingNop<11>(cmdbuf);
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmUpdatePsShader350(u32* cmdbuf, u32 size, const u32* ps_regs) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x27) {
        return -1;
    }
    if (!ps_regs) {
        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, 0u,
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e0203u,
                                               0u);                 // DB_SHADER_CONTROL update
        cmdbuf = PM4CmdSetData::SetContextReg(cmdbuf, 0x8fu, 0xfu); // CB_SHADER_MASK

        WriteTrailingNop<0x1d>(cmdbuf);
    } else {
        if (ps_regs[1] != 0) {
            LOG_ERROR(Lib_GnmDriver, "Invalid shader address.");
            return -1;
        }

        cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 8u, ps_regs[0],
                                         0u); // SPI_SHADER_PGM_LO_PS/SPI_SHADER_PGM_HI_PS
        cmdbuf = PM4CmdSetData::SetShReg(
            cmdbuf, 10u, ps_regs[2],
            ps_regs[3]); // SPI_SHADER_USER_DATA_PS_4/SPI_SHADER_USER_DATA_PS_5
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(
            cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01c4u, ps_regs[4],
            ps_regs[5]); // SPI_SHADER_Z_FORMAT/SPI_SHADER_COL_FORMAT update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(
            cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b3u, ps_regs[6],
            ps_regs[7]); // SPI_PS_INPUT_ENA/SPI_PS_INPUT_ADDR update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b6u,
                                               ps_regs[8]); // SPI_PS_IN_CONTROL update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b8u,
                                               ps_regs[9]); // SPI_BARYC_CNTL update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e0203u,
                                               ps_regs[10]); // DB_SHADER_CONTROL update
        cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e008fu,
                                               ps_regs[11]); // CB_SHADER_MASK update

        WriteTrailingNop<11>(cmdbuf);
    }
    return ORBIS_OK;
}

s32 PS4_SYSV_ABI sceGnmUpdateVsShader(u32* cmdbuf, u32 size, const u32* vs_regs,
                                      u32 shader_modifier) {
    LOG_TRACE(Lib_GnmDriver, "called");

    if (!cmdbuf || size <= 0x1c) {
        return -1;
    }

    if (!vs_regs) {
        LOG_ERROR(Lib_GnmDriver, "Null pointer passed as argument");
        return -1;
    }

    if (shader_modifier & 0xfcfffc3f) {
        LOG_ERROR(Lib_GnmDriver, "Invalid modifier mask");
        return -1;
    }

    if (vs_regs[1] != 0) {
        LOG_ERROR(Lib_GnmDriver, "Invalid shader address");
        return -1;
    }

    const u32 var = shader_modifier == 0 ? vs_regs[2] : (vs_regs[2] & 0xfcfffc3f | shader_modifier);
    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x48u, vs_regs[0], 0u);  // SPI_SHADER_PGM_LO_VS
    cmdbuf = PM4CmdSetData::SetShReg(cmdbuf, 0x4au, var, vs_regs[3]); // SPI_SHADER_PGM_RSRC1_VS
    cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e0207u,
                                           vs_regs[6]); // PA_CL_VS_OUT_CNTL update
    cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01b1u,
                                           vs_regs[4]); // PA_CL_VS_OUT_CNTL update
    cmdbuf = WritePacket<PM4ItOpcode::Nop>(cmdbuf, PM4ShaderType::ShaderGraphics, 0xc01e01c3u,
                                           vs_regs[5]); // PA_CL_VS_OUT_CNTL update

    WriteTrailingNop<11>(cmdbuf);
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateCommandBuffers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateDisableDiagnostics() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateDisableDiagnostics2() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateDispatchCommandBuffers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateDrawCommandBuffers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateGetDiagnosticInfo() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateGetDiagnostics() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateGetVersion() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateOnSubmitEnabled() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidateResetState() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmValidationRegisterMemoryCheckCallback() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceRazorCaptureCommandBuffersOnlyImmediate() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceRazorCaptureCommandBuffersOnlySinceLastFlip() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceRazorCaptureImmediate() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceRazorCaptureSinceLastFlip() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceRazorIsLoaded() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_063D065A2D6359C3() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_0CABACAFB258429D() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_150CF336FC2E99A3() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_17CA687F9EE52D49() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_1870B89F759C6B45() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_26F9029EF68A955E() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_301E3DBBAB092DB0() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_30BAFE172AF17FEF() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_3E6A3E8203D95317() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_40FEEF0C6534C434() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_416B9079DE4CBACE() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_4774D83BB4DDBF9A() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_50678F1CCEEB9A00() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_54A2EC5FA4C62413() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_5A9C52C83138AE6B() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_5D22193A31EA1142() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_725A36DEBB60948D() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_8021A502FA61B9BB() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_9D002FE0FA40F0E6() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_9D297F36A7028B71() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_A2D7EC7A7BCF79B3() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_AA12A3CB8990854A() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_ADC8DDC005020BC6() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_B0A8688B679CB42D() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_B489020B5157A5FF() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_BADE7B4C199140DD() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_D1511B9DCFFB3DD9() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_D53446649B02E58E() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_D8B6E8E28E1EF0A3() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_D93D733A19DD7454() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_DE995443BC2A8317() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_DF6E9528150C23FF() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_ECB4C6BA41FE3350() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebugModuleReset() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDebugReset() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_C4C328B7CF3B4171() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawInitToDefaultContextStateInternalCommand() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmDrawInitToDefaultContextStateInternalSize() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmFindResources() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmGetResourceRegistrationBuffers() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI sceGnmRegisterOwnerForSystem() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_1C43886B16EE5530() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_81037019ECCD0E01() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_BFB41C057478F0BF() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_E51D44DB8151238C() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

int PS4_SYSV_ABI Func_F916890425496553() {
    LOG_ERROR(Lib_GnmDriver, "(STUBBED) called");
    return ORBIS_OK;
}

void RegisterlibSceGnmDriver(Core::Loader::SymbolsResolver* sym) {
    liverpool = std::make_unique<AmdGpu::Liverpool>();

    LIB_FUNCTION("b0xyllnVY-I", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmAddEqEvent);
    LIB_FUNCTION("b08AgtPlHPg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmAreSubmitsAllowed);
    LIB_FUNCTION("ihxrbsoSKWc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmBeginWorkload);
    LIB_FUNCTION("ffrNQOshows", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmComputeWaitOnAddress);
    LIB_FUNCTION("EJapNl2+pgU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmComputeWaitSemaphore);
    LIB_FUNCTION("5udAm+6boVg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmCreateWorkloadStream);
    LIB_FUNCTION("jwCEzr7uEP4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerGetAddressWatch);
    LIB_FUNCTION("PNf0G7gvFHQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerHaltWavefront);
    LIB_FUNCTION("nO-tMnaxJiE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerReadGds);
    LIB_FUNCTION("t0HIQWnvK9E", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerReadSqIndirectRegister);
    LIB_FUNCTION("HsLtF4jKe48", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerResumeWavefront);
    LIB_FUNCTION("JRKSSV0YzwA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerResumeWavefrontCreation);
    LIB_FUNCTION("jpTMyYB8UBI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerSetAddressWatch);
    LIB_FUNCTION("MJG69Q7ti+s", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerWriteGds);
    LIB_FUNCTION("PaFw9w6f808", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebuggerWriteSqIndirectRegister);
    LIB_FUNCTION("qpGITzPE+Zc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebugHardwareStatus);
    LIB_FUNCTION("PVT+fuoS9gU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmDeleteEqEvent);
    LIB_FUNCTION("UtObDRQiGbs", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDestroyWorkloadStream);
    LIB_FUNCTION("bX5IbRvECXk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmDingDong);
    LIB_FUNCTION("byXlqupd8cE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDingDongForWorkload);
    LIB_FUNCTION("HHo1BAljZO8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDisableMipStatsReport);
    LIB_FUNCTION("0BzLGljcwBo", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDispatchDirect);
    LIB_FUNCTION("Z43vKp5k7r0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDispatchIndirect);
    LIB_FUNCTION("wED4ZXCFJT0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDispatchIndirectOnMec);
    LIB_FUNCTION("nF6bFRUBRAU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDispatchInitDefaultHardwareState);
    LIB_FUNCTION("HlTPoZ-oY7Y", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmDrawIndex);
    LIB_FUNCTION("GGsn7jMTxw4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmDrawIndexAuto);
    LIB_FUNCTION("ED9-Fjr8Ta4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndexIndirect);
    LIB_FUNCTION("thbPcG7E7qk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndexIndirectCountMulti);
    LIB_FUNCTION("5q95ravnueg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndexIndirectMulti);
    LIB_FUNCTION("jHdPvIzlpKc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndexMultiInstanced);
    LIB_FUNCTION("oYM+YzfCm2Y", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndexOffset);
    LIB_FUNCTION("4v+otIIdjqg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmDrawIndirect);
    LIB_FUNCTION("cUCo8OvArrw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndirectCountMulti);
    LIB_FUNCTION("f5QQLp9rzGk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawIndirectMulti);
    LIB_FUNCTION("Idffwf3yh8s", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitDefaultHardwareState);
    LIB_FUNCTION("QhnyReteJ1M", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitDefaultHardwareState175);
    LIB_FUNCTION("0H2vBYbTLHI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitDefaultHardwareState200);
    LIB_FUNCTION("yb2cRhagD1I", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitDefaultHardwareState350);
    LIB_FUNCTION("8lH54sfjfmU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitToDefaultContextState);
    LIB_FUNCTION("im2ZuItabu4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitToDefaultContextState400);
    LIB_FUNCTION("stDSYW2SBVM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawOpaqueAuto);
    LIB_FUNCTION("TLV4mswiZ4A", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverCaptureInProgress);
    LIB_FUNCTION("ODEeJ1GfDtE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterface);
    LIB_FUNCTION("4LSXsEKPTsE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterfaceForGpuDebugger);
    LIB_FUNCTION("MpncRjHNYRE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterfaceForGpuException);
    LIB_FUNCTION("EwjWGcIOgeM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterfaceForHDRScopes);
    LIB_FUNCTION("3EXdrVC7WFk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterfaceForReplay);
    LIB_FUNCTION("P9iKqxAGeck", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterfaceForResourceRegistration);
    LIB_FUNCTION("t-vIc5cTEzg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalRetrieveGnmInterfaceForValidation);
    LIB_FUNCTION("BvvO8Up88Zc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverInternalVirtualQuery);
    LIB_FUNCTION("R6z1xM3pW-w", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverTraceInProgress);
    LIB_FUNCTION("d88anrgNoKY", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDriverTriggerCapture);
    LIB_FUNCTION("Fa3x75OOLRA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmEndWorkload);
    LIB_FUNCTION("4Mv9OXypBG8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmFindResourcesPublic);
    LIB_FUNCTION("iBt3Oe00Kvc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmFlushGarlic);
    LIB_FUNCTION("GviyYfFQIkc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetCoredumpAddress);
    LIB_FUNCTION("meiO-5ZCVIE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetCoredumpMode);
    LIB_FUNCTION("O-7nHKgcNSQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetCoredumpProtectionFaultTimestamp);
    LIB_FUNCTION("bSJFzejYrJI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetDbgGcHandle);
    LIB_FUNCTION("pd4C7da6sEg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetDebugTimestamp);
    LIB_FUNCTION("UoYY0DWMC0U", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetEqEventType);
    LIB_FUNCTION("H7-fgvEutM0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetEqTimeStamp);
    LIB_FUNCTION("oL4hGI1PMpw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetGpuBlockStatus);
    LIB_FUNCTION("Fwvh++m9IQI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetGpuCoreClockFrequency);
    LIB_FUNCTION("tZCSL5ulnB4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetGpuInfoStatus);
    LIB_FUNCTION("iFirFzgYsvw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetLastWaitedAddress);
    LIB_FUNCTION("KnldROUkWJY", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetNumTcaUnits);
    LIB_FUNCTION("FFVZcCu3zWU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetOffChipTessellationBufferSize);
    LIB_FUNCTION("QJjPjlmPAL0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmGetOwnerName);
    LIB_FUNCTION("dewXw5roLs0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetPhysicalCounterFromVirtualized);
    LIB_FUNCTION("fzJdEihTFV4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetProtectionFaultTimeStamp);
    LIB_FUNCTION("4PKnYXOhcx4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetResourceBaseAddressAndSizeInBytes);
    LIB_FUNCTION("O0S96YnD04U", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetResourceName);
    LIB_FUNCTION("UBv7FkVfzcQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetResourceShaderGuid);
    LIB_FUNCTION("bdqdvIkLPIU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetResourceType);
    LIB_FUNCTION("UoBuWAhKk7U", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetResourceUserData);
    LIB_FUNCTION("nEyFbYUloIM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetShaderProgramBaseAddress);
    LIB_FUNCTION("k7iGTvDQPLQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetShaderStatus);
    LIB_FUNCTION("ln33zjBrfjk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetTheTessellationFactorRingBufferBaseAddress);
    LIB_FUNCTION("QLdG7G-PBZo", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGpuPaDebugEnter);
    LIB_FUNCTION("tVEdZe3wlbY", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGpuPaDebugLeave);
    LIB_FUNCTION("NfvOrNzy6sk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertDingDongMarker);
    LIB_FUNCTION("7qZVNgEu+SY", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertPopMarker);
    LIB_FUNCTION("aPIZJTXC+cU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertPushColorMarker);
    LIB_FUNCTION("W1Etj-jlW7Y", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertPushMarker);
    LIB_FUNCTION("aj3L-iaFmyk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertSetColorMarker);
    LIB_FUNCTION("jiItzS6+22g", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertSetMarker);
    LIB_FUNCTION("URDgJcXhQOs", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertThreadTraceMarker);
    LIB_FUNCTION("1qXLHIpROPE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmInsertWaitFlipDone);
    LIB_FUNCTION("HRyNHoAjb6E", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmIsCoredumpValid);
    LIB_FUNCTION("jg33rEKLfVs", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmIsUserPaEnabled);
    LIB_FUNCTION("26PM5Mzl8zc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmLogicalCuIndexToPhysicalCuIndex);
    LIB_FUNCTION("RU74kek-N0c", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmLogicalCuMaskToPhysicalCuMask);
    LIB_FUNCTION("Kl0Z3LH07QI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmLogicalTcaUnitToPhysical);
    LIB_FUNCTION("29oKvKXzEZo", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmMapComputeQueue);
    LIB_FUNCTION("A+uGq+3KFtQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmMapComputeQueueWithPriority);
    LIB_FUNCTION("+N+wrSYBLIw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmPaDisableFlipCallbacks);
    LIB_FUNCTION("8WDA9RiXLaw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmPaEnableFlipCallbacks);
    LIB_FUNCTION("tNuT48mApTc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmPaHeartbeat);
    LIB_FUNCTION("6IMbpR7nTzA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmQueryResourceRegistrationUserMemoryRequirements);
    LIB_FUNCTION("+rJnw2e9O+0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRaiseUserExceptionEvent);
    LIB_FUNCTION("9Mv61HaMhfA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRegisterGdsResource);
    LIB_FUNCTION("t7-VbMosbR4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRegisterGnmLiveCallbackConfig);
    LIB_FUNCTION("ZFqKFl23aMc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmRegisterOwner);
    LIB_FUNCTION("nvEwfYAImTs", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRegisterResource);
    LIB_FUNCTION("gObODli-OH8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRequestFlipAndSubmitDone);
    LIB_FUNCTION("6YRHhh5mHCs", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRequestFlipAndSubmitDoneForWorkload);
    LIB_FUNCTION("f85orjx7qts", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRequestMipStatsReportAndReset);
    LIB_FUNCTION("MYRtYhojKdA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmResetVgtControl);
    LIB_FUNCTION("hS0MKPRdNr0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSdmaClose);
    LIB_FUNCTION("31G6PB2oRYQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSdmaConstFill);
    LIB_FUNCTION("Lg2isla2XeQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSdmaCopyLinear);
    LIB_FUNCTION("-Se2FY+UTsI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSdmaCopyTiled);
    LIB_FUNCTION("OlFgKnBsALE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSdmaCopyWindow);
    LIB_FUNCTION("LQQN0SwQv8c", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSdmaFlush);
    LIB_FUNCTION("suUlSjWr7CE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSdmaGetMinCmdSize);
    LIB_FUNCTION("5AtqyMgO7fM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSdmaOpen);
    LIB_FUNCTION("KXltnCwEJHQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetCsShader);
    LIB_FUNCTION("Kx-h-nWQJ8A", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetCsShaderWithModifier);
    LIB_FUNCTION("X9Omw9dwv5M", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetEmbeddedPsShader);
    LIB_FUNCTION("+AFvOEXrKJk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetEmbeddedVsShader);
    LIB_FUNCTION("FUHG8sQ3R58", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetEsShader);
    LIB_FUNCTION("jtkqXpAOY6w", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetGsRingSizes);
    LIB_FUNCTION("UJwNuMBcUAk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetGsShader);
    LIB_FUNCTION("VJNjFtqiF5w", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetHsShader);
    LIB_FUNCTION("vckdzbQ46SI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetLsShader);
    LIB_FUNCTION("bQVd5YzCal0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetPsShader);
    LIB_FUNCTION("5uFKckiJYRM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetPsShader350);
    LIB_FUNCTION("q-qhDxP67Hg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetResourceRegistrationUserMemory);
    LIB_FUNCTION("K3BKBBYKUSE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetResourceUserData);
    LIB_FUNCTION("0O3xxFaiObw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetSpiEnableSqCounters);
    LIB_FUNCTION("lN7Gk-p9u78", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetSpiEnableSqCountersForUnitInstance);
    LIB_FUNCTION("+xuDhxlWRPg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetupMipStatsReport);
    LIB_FUNCTION("cFCp0NX8wf0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetVgtControl);
    LIB_FUNCTION("gAhCn6UiU4Y", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSetVsShader);
    LIB_FUNCTION("y+iI2lkX+qI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetWaveLimitMultiplier);
    LIB_FUNCTION("XiyzNZ9J4nQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSetWaveLimitMultipliers);
    LIB_FUNCTION("kkn+iy-mhyg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmEndSpm);
    LIB_FUNCTION("aqhuK2Mj4X4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmInit);
    LIB_FUNCTION("KHpZ9hJo1c0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmInit2);
    LIB_FUNCTION("QEsMC+M3yjE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmSetDelay);
    LIB_FUNCTION("hljMAxTLNF0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmSetMuxRam);
    LIB_FUNCTION("bioGsp74SLM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmSetMuxRam2);
    LIB_FUNCTION("cMWWYeqQQlM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSpmSetSelectCounter);
    LIB_FUNCTION("-zJi8Vb4Du4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSpmSetSpmSelects);
    LIB_FUNCTION("xTsOqp-1bE4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSpmSetSpmSelects2);
    LIB_FUNCTION("AmmYLcJGTl0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSpmStartSpm);
    LIB_FUNCTION("UHDiSFDxNao", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSqttFini);
    LIB_FUNCTION("a3tLC56vwug", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttFinishTrace);
    LIB_FUNCTION("L-owl1dSKKg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSqttGetBcInfo);
    LIB_FUNCTION("LQtzqghKQm4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetGpuClocks);
    LIB_FUNCTION("wYN5mmv6Ya8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetHiWater);
    LIB_FUNCTION("9X4SkENMS0M", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSqttGetStatus);
    LIB_FUNCTION("lbMccQM2iqc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetTraceCounter);
    LIB_FUNCTION("DYAC6JUeZvM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetTraceWptr);
    LIB_FUNCTION("pS2tjBxzJr4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetWrapCounts);
    LIB_FUNCTION("rXV8az6X+fM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetWrapCounts2);
    LIB_FUNCTION("ARS+TNLopyk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttGetWritebackLabels);
    LIB_FUNCTION("X6yCBYPP7HA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSqttInit);
    LIB_FUNCTION("2IJhUyK8moE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSelectMode);
    LIB_FUNCTION("QA5h6Gh3r60", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSelectTarget);
    LIB_FUNCTION("F5XJY1XHa3Y", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSelectTokens);
    LIB_FUNCTION("wJtaTpNZfH4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetCuPerfMask);
    LIB_FUNCTION("kY4dsQh+SH4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetDceEventWrite);
    LIB_FUNCTION("7XRH1CIfNpI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetHiWater);
    LIB_FUNCTION("05YzC2r3hHo", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetTraceBuffer2);
    LIB_FUNCTION("ASUric-2EnI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetTraceBuffers);
    LIB_FUNCTION("gPxYzPp2wlo", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetUserData);
    LIB_FUNCTION("d-YcZX7SIQA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSetUserdataTimer);
    LIB_FUNCTION("ru8cb4he6O8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttStartTrace);
    LIB_FUNCTION("gVuGo1nBnG8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSqttStopTrace);
    LIB_FUNCTION("OpyolX6RwS0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSwitchTraceBuffer);
    LIB_FUNCTION("dl5u5eGBgNk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttSwitchTraceBuffer2);
    LIB_FUNCTION("QLzOwOF0t+A", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSqttWaitForEvent);
    LIB_FUNCTION("xbxNatawohc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSubmitAndFlipCommandBuffers);
    LIB_FUNCTION("Ga6r7H6Y0RI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSubmitAndFlipCommandBuffersForWorkload);
    LIB_FUNCTION("zwY0YV91TTI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSubmitCommandBuffers);
    LIB_FUNCTION("jRcI8VcgTz4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmSubmitCommandBuffersForWorkload);
    LIB_FUNCTION("yvZ73uQUqrk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceGnmSubmitDone);
    LIB_FUNCTION("ArSg-TGinhk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUnmapComputeQueue);
    LIB_FUNCTION("yhFCnaz5daw", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUnregisterAllResourcesForOwner);
    LIB_FUNCTION("fhKwCVVj9nk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUnregisterOwnerAndResources);
    LIB_FUNCTION("k8EXkhIP+lM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUnregisterResource);
    LIB_FUNCTION("nLM2i2+65hA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUpdateGsShader);
    LIB_FUNCTION("GNlx+y7xPdE", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUpdateHsShader);
    LIB_FUNCTION("4MgRw-bVNQU", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUpdatePsShader);
    LIB_FUNCTION("mLVL7N7BVBg", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUpdatePsShader350);
    LIB_FUNCTION("V31V01UiScY", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmUpdateVsShader);
    LIB_FUNCTION("iCO804ZgzdA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateCommandBuffers);
    LIB_FUNCTION("SXw4dZEkgpA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateDisableDiagnostics);
    LIB_FUNCTION("BgM3t3LvcNk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateDisableDiagnostics2);
    LIB_FUNCTION("qGP74T5OWJc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateDispatchCommandBuffers);
    LIB_FUNCTION("hsZPf1lON7E", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateDrawCommandBuffers);
    LIB_FUNCTION("RX7XCNSaL6I", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateGetDiagnosticInfo);
    LIB_FUNCTION("5SHGNwLXBV4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateGetDiagnostics);
    LIB_FUNCTION("HzMN7ANqYEc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateGetVersion);
    LIB_FUNCTION("rTIV11nMQuM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateOnSubmitEnabled);
    LIB_FUNCTION("MBMa6EFu4Ko", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidateResetState);
    LIB_FUNCTION("Q7t4VEYLafI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceGnmValidationRegisterMemoryCheckCallback);
    LIB_FUNCTION("xeTLfxVIQO4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceRazorCaptureCommandBuffersOnlyImmediate);
    LIB_FUNCTION("9thMn+uB1is", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceRazorCaptureCommandBuffersOnlySinceLastFlip);
    LIB_FUNCTION("u9YKpRRHe-M", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceRazorCaptureImmediate);
    LIB_FUNCTION("4UFagYlfuAM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 sceRazorCaptureSinceLastFlip);
    LIB_FUNCTION("f33OrruQYbM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1, sceRazorIsLoaded);
    LIB_FUNCTION("Bj0GWi1jWcM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_063D065A2D6359C3);
    LIB_FUNCTION("DKusr7JYQp0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_0CABACAFB258429D);
    LIB_FUNCTION("FQzzNvwumaM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_150CF336FC2E99A3);
    LIB_FUNCTION("F8pof57lLUk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_17CA687F9EE52D49);
    LIB_FUNCTION("GHC4n3Wca0U", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_1870B89F759C6B45);
    LIB_FUNCTION("JvkCnvaKlV4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_26F9029EF68A955E);
    LIB_FUNCTION("MB49u6sJLbA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_301E3DBBAB092DB0);
    LIB_FUNCTION("MLr+Fyrxf+8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_30BAFE172AF17FEF);
    LIB_FUNCTION("Pmo+ggPZUxc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_3E6A3E8203D95317);
    LIB_FUNCTION("QP7vDGU0xDQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_40FEEF0C6534C434);
    LIB_FUNCTION("QWuQed5Mus4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_416B9079DE4CBACE);
    LIB_FUNCTION("R3TYO7Tdv5o", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_4774D83BB4DDBF9A);
    LIB_FUNCTION("UGePHM7rmgA", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_50678F1CCEEB9A00);
    LIB_FUNCTION("VKLsX6TGJBM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_54A2EC5FA4C62413);
    LIB_FUNCTION("WpxSyDE4rms", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_5A9C52C83138AE6B);
    LIB_FUNCTION("XSIZOjHqEUI", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_5D22193A31EA1142);
    LIB_FUNCTION("clo23rtglI0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_725A36DEBB60948D);
    LIB_FUNCTION("gCGlAvphubs", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_8021A502FA61B9BB);
    LIB_FUNCTION("nQAv4PpA8OY", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_9D002FE0FA40F0E6);
    LIB_FUNCTION("nSl-NqcCi3E", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_9D297F36A7028B71);
    LIB_FUNCTION("otfsenvPebM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_A2D7EC7A7BCF79B3);
    LIB_FUNCTION("qhKjy4mQhUo", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_AA12A3CB8990854A);
    LIB_FUNCTION("rcjdwAUCC8Y", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_ADC8DDC005020BC6);
    LIB_FUNCTION("sKhoi2ectC0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_B0A8688B679CB42D);
    LIB_FUNCTION("tIkCC1FXpf8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_B489020B5157A5FF);
    LIB_FUNCTION("ut57TBmRQN0", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_BADE7B4C199140DD);
    LIB_FUNCTION("0VEbnc-7Pdk", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_D1511B9DCFFB3DD9);
    LIB_FUNCTION("1TRGZJsC5Y4", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_D53446649B02E58E);
    LIB_FUNCTION("2Lbo4o4e8KM", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_D8B6E8E28E1EF0A3);
    LIB_FUNCTION("2T1zOhnddFQ", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_D93D733A19DD7454);
    LIB_FUNCTION("3plUQ7wqgxc", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_DE995443BC2A8317);
    LIB_FUNCTION("326VKBUMI-8", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_DF6E9528150C23FF);
    LIB_FUNCTION("7LTGukH+M1A", "libSceGnmDriver", 1, "libSceGnmDriver", 1, 1,
                 Func_ECB4C6BA41FE3350);
    LIB_FUNCTION("dqPBvjFVpTA", "libSceGnmDebugModuleReset", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebugModuleReset);
    LIB_FUNCTION("RNPAItiMLIg", "libSceGnmDebugReset", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDebugReset);
    LIB_FUNCTION("xMMot887QXE", "libSceGnmDebugReset", 1, "libSceGnmDriver", 1, 1,
                 Func_C4C328B7CF3B4171);
    LIB_FUNCTION("pF1HQjbmQJ0", "libSceGnmDriverCompat", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitToDefaultContextStateInternalCommand);
    LIB_FUNCTION("jajhf-Gi3AI", "libSceGnmDriverCompat", 1, "libSceGnmDriver", 1, 1,
                 sceGnmDrawInitToDefaultContextStateInternalSize);
    LIB_FUNCTION("vbcR4Ken6AA", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 sceGnmFindResources);
    LIB_FUNCTION("eLQbNsKeTkU", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetResourceRegistrationBuffers);
    LIB_FUNCTION("j6mSQs3UgaY", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 sceGnmRegisterOwnerForSystem);
    LIB_FUNCTION("HEOIaxbuVTA", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 Func_1C43886B16EE5530);
    LIB_FUNCTION("gQNwGezNDgE", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 Func_81037019ECCD0E01);
    LIB_FUNCTION("v7QcBXR48L8", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 Func_BFB41C057478F0BF);
    LIB_FUNCTION("5R1E24FRI4w", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 Func_E51D44DB8151238C);
    LIB_FUNCTION("+RaJBCVJZVM", "libSceGnmDriverResourceRegistration", 1, "libSceGnmDriver", 1, 1,
                 Func_F916890425496553);
    LIB_FUNCTION("Fwvh++m9IQI", "libSceGnmGetGpuCoreClockFrequency", 1, "libSceGnmDriver", 1, 1,
                 sceGnmGetGpuCoreClockFrequency);
    LIB_FUNCTION("R3TYO7Tdv5o", "libSceGnmWaitFreeSubmit", 1, "libSceGnmDriver", 1, 1,
                 Func_4774D83BB4DDBF9A);
    LIB_FUNCTION("ut57TBmRQN0", "libSceGnmWaitFreeSubmit", 1, "libSceGnmDriver", 1, 1,
                 Func_BADE7B4C199140DD);
};

} // namespace Libraries::GnmDriver
