// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/ir/program.h"

namespace Shader::Optimization {

void Visit(Info& info, IR::Inst& inst) {
    switch (inst.GetOpcode()) {
    case IR::Opcode::GetAttribute:
    case IR::Opcode::GetAttributeU32: {
        info.loads.Set(inst.Arg(0).Attribute(), inst.Arg(1).U32());
        break;
    }
    case IR::Opcode::SetAttribute: {
        info.stores.Set(inst.Arg(0).Attribute(), inst.Arg(2).U32());
        break;
    }
    case IR::Opcode::LoadSharedS8:
    case IR::Opcode::LoadSharedU8:
    case IR::Opcode::WriteSharedU8:
        info.uses_shared_u8 = true;
        break;
    case IR::Opcode::LoadSharedS16:
    case IR::Opcode::LoadSharedU16:
    case IR::Opcode::WriteSharedU16:
        info.uses_shared_u16 = true;
        break;
    case IR::Opcode::QuadShuffle:
        info.uses_group_quad = true;
        break;
    default:
        break;
    }
}

void CollectShaderInfoPass(IR::Program& program) {
    Info& info{program.info};
    for (IR::Block* const block : program.post_order_blocks) {
        for (IR::Inst& inst : block->Instructions()) {
            Visit(info, inst);
        }
    }
}

} // namespace Shader::Optimization
