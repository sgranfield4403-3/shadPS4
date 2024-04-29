// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"

namespace AmdGpu {

enum class PM4ItOpcode : u32 {
    Nop = 0x10,
    SetBase = 0x11,
    ClearState = 0x12,
    IndexBufferSize = 0x13,
    DispatchDirect = 0x15,
    DispatchIndirect = 0x16,
    AtomicGds = 0x1D,
    Atomic = 0x1E,
    OcclusionQuery = 0x1F,
    SetPredication = 0x20,
    RegRmw = 0x21,
    CondExec = 0x22,
    PredExec = 0x23,
    DrawIndirect = 0x24,
    DrawIndexIndirect = 0x25,
    IndexBase = 0x26,
    DrawIndex2 = 0x27,
    ContextControl = 0x28,
    IndexType = 0x2A,
    DrawIndirectMulti = 0x2C,
    DrawIndexAuto = 0x2D,
    NumInstances = 0x2F,
    DrawIndexMultiAuto = 0x30,
    IndirectBufferConst = 0x33,
    DrawIndexOffset2 = 0x35,
    WriteData = 0x37,
    DrawIndexIndirectMulti = 0x38,
    MemSemaphore = 0x39,
    IndirectBuffer = 0x3F,
    CondIndirectBuffer = 0x3F,
    CopyData = 0x40,
    CommandProcessorDma = 0x41,
    SurfaceSync = 0x43,
    CondWrite = 0x45,
    EventWrite = 0x46,
    EventWriteEop = 0x47,
    EventWriteEos = 0x48,
    PremableCntl = 0x4A,
    DmaData = 0x50,
    ContextRegRmw = 0x51,
    LoadShReg = 0x5F,
    LoadConfigReg = 0x60,
    LoadContextReg = 0x61,
    SetConfigReg = 0x68,
    SetContextReg = 0x69,
    SetContextRegIndirect = 0x73,
    SetShReg = 0x76,
    SetShRegOffset = 0x77,
    SetUconfigReg = 0x79
};

} // namespace AmdGpu
