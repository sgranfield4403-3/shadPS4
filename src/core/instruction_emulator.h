// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

namespace Xbyak {
class CodeGenerator;
}

namespace Core {

void PatchInstructions(u64 segment_addr, u64 segment_size, Xbyak::CodeGenerator& c);

} // namespace Core
