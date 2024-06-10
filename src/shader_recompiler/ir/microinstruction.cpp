// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <memory>

#include "shader_recompiler/exception.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/type.h"
#include "shader_recompiler/ir/value.h"

namespace Shader::IR {

Inst::Inst(IR::Opcode op_, u32 flags_) noexcept : op{op_}, flags{flags_} {
    if (op == Opcode::Phi) {
        std::construct_at(&phi_args);
    } else {
        std::construct_at(&args);
    }
}

Inst::Inst(const Inst& base) : op{base.op}, flags{base.flags} {
    if (base.op == Opcode::Phi) {
        throw NotImplementedException("Copying phi node");
    }
    std::construct_at(&args);
    const size_t num_args{base.NumArgs()};
    for (size_t index = 0; index < num_args; ++index) {
        SetArg(index, base.Arg(index));
    }
}

Inst::~Inst() {
    if (op == Opcode::Phi) {
        std::destroy_at(&phi_args);
    } else {
        std::destroy_at(&args);
    }
}

bool Inst::MayHaveSideEffects() const noexcept {
    switch (op) {
    case Opcode::ConditionRef:
    case Opcode::Reference:
    case Opcode::PhiMove:
    case Opcode::Prologue:
    case Opcode::Epilogue:
    case Opcode::Discard:
    case Opcode::SetAttribute:
    case Opcode::StoreBufferF32:
    case Opcode::StoreBufferF32x2:
    case Opcode::StoreBufferF32x3:
    case Opcode::StoreBufferF32x4:
    case Opcode::StoreBufferU32:
    case Opcode::ImageWrite:
        return true;
    default:
        return false;
    }
}

bool Inst::AreAllArgsImmediates() const {
    if (op == Opcode::Phi) {
        throw LogicError("Testing for all arguments are immediates on phi instruction");
    }
    return std::all_of(args.begin(), args.begin() + NumArgs(),
                       [](const IR::Value& value) { return value.IsImmediate(); });
}

IR::Type Inst::Type() const {
    return TypeOf(op);
}

void Inst::SetArg(size_t index, Value value) {
    if (index >= NumArgs()) {
        throw InvalidArgument("Out of bounds argument index {} in opcode {}", index, op);
    }
    const IR::Value arg{Arg(index)};
    if (!arg.IsImmediate()) {
        UndoUse(arg);
    }
    if (!value.IsImmediate()) {
        Use(value);
    }
    if (op == Opcode::Phi) {
        phi_args[index].second = value;
    } else {
        args[index] = value;
    }
}

Block* Inst::PhiBlock(size_t index) const {
    if (op != Opcode::Phi) {
        throw LogicError("{} is not a Phi instruction", op);
    }
    if (index >= phi_args.size()) {
        throw InvalidArgument("Out of bounds argument index {} in phi instruction");
    }
    return phi_args[index].first;
}

void Inst::AddPhiOperand(Block* predecessor, const Value& value) {
    if (!value.IsImmediate()) {
        Use(value);
    }
    phi_args.emplace_back(predecessor, value);
}

void Inst::Invalidate() {
    ClearArgs();
    ReplaceOpcode(Opcode::Void);
}

void Inst::ClearArgs() {
    if (op == Opcode::Phi) {
        for (auto& pair : phi_args) {
            IR::Value& value{pair.second};
            if (!value.IsImmediate()) {
                UndoUse(value);
            }
        }
        phi_args.clear();
    } else {
        for (auto& value : args) {
            if (!value.IsImmediate()) {
                UndoUse(value);
            }
        }
        // Reset arguments to null
        // std::memset was measured to be faster on MSVC than std::ranges:fill
        std::memset(reinterpret_cast<char*>(&args), 0, sizeof(args));
    }
}

void Inst::ReplaceUsesWith(Value replacement) {
    Invalidate();
    ReplaceOpcode(Opcode::Identity);
    if (!replacement.IsImmediate()) {
        Use(replacement);
    }
    args[0] = replacement;
}

void Inst::ReplaceOpcode(IR::Opcode opcode) {
    if (opcode == IR::Opcode::Phi) {
        throw LogicError("Cannot transition into Phi");
    }
    if (op == Opcode::Phi) {
        // Transition out of phi arguments into non-phi
        std::destroy_at(&phi_args);
        std::construct_at(&args);
    }
    op = opcode;
}

void Inst::Use(const Value& value) {
    Inst* const inst{value.Inst()};
    ++inst->use_count;
}

void Inst::UndoUse(const Value& value) {
    Inst* const inst{value.Inst()};
    --inst->use_count;
}

} // namespace Shader::IR
