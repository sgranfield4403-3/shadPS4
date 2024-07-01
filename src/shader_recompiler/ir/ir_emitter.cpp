// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <bit>
#include "shader_recompiler/exception.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/ir/value.h"

namespace Shader::IR {
namespace {
[[noreturn]] void ThrowInvalidType(Type type) {
    UNREACHABLE_MSG("Invalid type {}", u32(type));
}

Value MakeLodClampPair(IREmitter& ir, const F32& bias_lod, const F32& lod_clamp) {
    if (!bias_lod.IsEmpty() && !lod_clamp.IsEmpty()) {
        return ir.CompositeConstruct(bias_lod, lod_clamp);
    } else if (!bias_lod.IsEmpty()) {
        return bias_lod;
    } else if (!lod_clamp.IsEmpty()) {
        return lod_clamp;
    } else {
        return Value{};
    }
}
} // Anonymous namespace

U1 IREmitter::Imm1(bool value) const {
    return U1{Value{value}};
}

U8 IREmitter::Imm8(u8 value) const {
    return U8{Value{value}};
}

U16 IREmitter::Imm16(u16 value) const {
    return U16{Value{value}};
}

U32 IREmitter::Imm32(u32 value) const {
    return U32{Value{value}};
}

U32 IREmitter::Imm32(s32 value) const {
    return U32{Value{static_cast<u32>(value)}};
}

F32 IREmitter::Imm32(f32 value) const {
    return F32{Value{value}};
}

U64 IREmitter::Imm64(u64 value) const {
    return U64{Value{value}};
}

U64 IREmitter::Imm64(s64 value) const {
    return U64{Value{static_cast<u64>(value)}};
}

F64 IREmitter::Imm64(f64 value) const {
    return F64{Value{value}};
}

template <>
IR::U32 IREmitter::BitCast<IR::U32, IR::F32>(const IR::F32& value) {
    return Inst<IR::U32>(Opcode::BitCastU32F32, value);
}

template <>
IR::F32 IREmitter::BitCast<IR::F32, IR::U32>(const IR::U32& value) {
    return Inst<IR::F32>(Opcode::BitCastF32U32, value);
}

template <>
IR::U16 IREmitter::BitCast<IR::U16, IR::F16>(const IR::F16& value) {
    return Inst<IR::U16>(Opcode::BitCastU16F16, value);
}

template <>
IR::F16 IREmitter::BitCast<IR::F16, IR::U16>(const IR::U16& value) {
    return Inst<IR::F16>(Opcode::BitCastF16U16, value);
}

template <>
IR::U64 IREmitter::BitCast<IR::U64, IR::F64>(const IR::F64& value) {
    return Inst<IR::U64>(Opcode::BitCastU64F64, value);
}

template <>
IR::F64 IREmitter::BitCast<IR::F64, IR::U64>(const IR::U64& value) {
    return Inst<IR::F64>(Opcode::BitCastF64U64, value);
}

U1 IREmitter::ConditionRef(const U1& value) {
    return Inst<U1>(Opcode::ConditionRef, value);
}

void IREmitter::Reference(const Value& value) {
    Inst(Opcode::Reference, value);
}

void IREmitter::PhiMove(IR::Inst& phi, const Value& value) {
    Inst(Opcode::PhiMove, Value{&phi}, value);
}

void IREmitter::Prologue() {
    Inst(Opcode::Prologue);
}

void IREmitter::Epilogue() {
    Inst(Opcode::Epilogue);
}

void IREmitter::Discard() {
    Inst(Opcode::Discard);
}

U32 IREmitter::GetUserData(IR::ScalarReg reg) {
    return Inst<U32>(Opcode::GetUserData, reg);
}

U1 IREmitter::GetThreadBitScalarReg(IR::ScalarReg reg) {
    return Inst<U1>(Opcode::GetThreadBitScalarReg, reg);
}

void IREmitter::SetThreadBitScalarReg(IR::ScalarReg reg, const U1& value) {
    Inst(Opcode::SetThreadBitScalarReg, reg, value);
}

template <>
U32 IREmitter::GetScalarReg(IR::ScalarReg reg) {
    return Inst<U32>(Opcode::GetScalarRegister, reg);
}

template <>
F32 IREmitter::GetScalarReg(IR::ScalarReg reg) {
    return BitCast<F32>(GetScalarReg<U32>(reg));
}

template <>
U32 IREmitter::GetVectorReg(IR::VectorReg reg) {
    return Inst<U32>(Opcode::GetVectorRegister, reg);
}

template <>
F32 IREmitter::GetVectorReg(IR::VectorReg reg) {
    return BitCast<F32>(GetVectorReg<U32>(reg));
}

void IREmitter::SetScalarReg(IR::ScalarReg reg, const U32F32& value) {
    const U32 value_typed = value.Type() == Type::F32 ? BitCast<U32>(F32{value}) : U32{value};
    Inst(Opcode::SetScalarRegister, reg, value_typed);
}

void IREmitter::SetVectorReg(IR::VectorReg reg, const U32F32& value) {
    const U32 value_typed = value.Type() == Type::F32 ? BitCast<U32>(F32{value}) : U32{value};
    Inst(Opcode::SetVectorRegister, reg, value_typed);
}

U1 IREmitter::GetGotoVariable(u32 id) {
    return Inst<U1>(Opcode::GetGotoVariable, id);
}

U1 IREmitter::Condition(IR::Condition cond) {
    switch (cond) {
    case IR::Condition::False:
        return Imm1(false);
    case IR::Condition::True:
        return Imm1(true);
    case IR::Condition::Scc0:
        return LogicalNot(GetScc());
    case IR::Condition::Scc1:
        return GetScc();
    case IR::Condition::Vccz:
        return LogicalNot(GetVcc());
    case IR::Condition::Vccnz:
        return GetVcc();
    case IR::Condition::Execz:
        return LogicalNot(GetExec());
    case IR::Condition::Execnz:
        return GetExec();
    default:
        throw NotImplementedException("");
    }
}

void IREmitter::SetGotoVariable(u32 id, const U1& value) {
    Inst(Opcode::SetGotoVariable, id, value);
}

U1 IREmitter::GetScc() {
    return Inst<U1>(Opcode::GetScc);
}

U1 IREmitter::GetExec() {
    return Inst<U1>(Opcode::GetExec);
}

U1 IREmitter::GetVcc() {
    return Inst<U1>(Opcode::GetVcc);
}

U32 IREmitter::GetVccLo() {
    return Inst<U32>(Opcode::GetVccLo);
}

U32 IREmitter::GetVccHi() {
    return Inst<U32>(Opcode::GetVccHi);
}

void IREmitter::SetScc(const U1& value) {
    Inst(Opcode::SetScc, value);
}

void IREmitter::SetExec(const U1& value) {
    Inst(Opcode::SetExec, value);
}

void IREmitter::SetVcc(const U1& value) {
    Inst(Opcode::SetVcc, value);
}

void IREmitter::SetVccLo(const U32& value) {
    Inst(Opcode::SetVccLo, value);
}

void IREmitter::SetVccHi(const U32& value) {
    Inst(Opcode::SetVccHi, value);
}

F32 IREmitter::GetAttribute(IR::Attribute attribute, u32 comp) {
    return Inst<F32>(Opcode::GetAttribute, attribute, Imm32(comp));
}

U32 IREmitter::GetAttributeU32(IR::Attribute attribute, u32 comp) {
    return Inst<U32>(Opcode::GetAttributeU32, attribute, Imm32(comp));
}

void IREmitter::SetAttribute(IR::Attribute attribute, const F32& value, u32 comp) {
    Inst(Opcode::SetAttribute, attribute, value, Imm32(comp));
}

U32U64 IREmitter::ReadShared(int bit_size, bool is_signed, const U32& offset) {
    /*switch (bit_size) {
    case 8:
        return Inst<U32>(is_signed ? Opcode::ReadSharedS8 : Opcode::ReadSharedU8, offset);
    case 16:
        return Inst<U32>(is_signed ? Opcode::ReadSharedS16 : Opcode::ReadSharedU16, offset);
    case 32:
        return Inst<U32>(Opcode::ReadSharedU32, offset);
    case 64:
        return Inst<U64>(Opcode::ReadSharedU64, offset);
    }
    UNREACHABLE_MSG("Invalid bit size {}", bit_size);*/
}

void IREmitter::WriteShared(int bit_size, const Value& value, const U32& offset) {
    /*switch (bit_size) {
    case 8:
        Inst(Opcode::WriteSharedU8, offset, value);
        break;
    case 16:
        Inst(Opcode::WriteSharedU16, offset, value);
        break;
    case 32:
        Inst(Opcode::WriteSharedU32, offset, value);
        break;
    case 64:
        Inst(Opcode::WriteSharedU64, offset, value);
        break;
    default:
        UNREACHABLE_MSG("Invalid bit size {}", bit_size);
    }*/
}

U32 IREmitter::ReadConst(const Value& base, const U32& offset) {
    return Inst<U32>(Opcode::ReadConst, base, offset);
}

F32 IREmitter::ReadConstBuffer(const Value& handle, const U32& index) {
    return Inst<F32>(Opcode::ReadConstBuffer, handle, index);
}

Value IREmitter::LoadBuffer(int num_dwords, const Value& handle, const Value& address,
                            BufferInstInfo info) {
    switch (num_dwords) {
    case 1:
        return Inst(Opcode::LoadBufferF32, Flags{info}, handle, address);
    case 2:
        return Inst(Opcode::LoadBufferF32x2, Flags{info}, handle, address);
    case 3:
        return Inst(Opcode::LoadBufferF32x3, Flags{info}, handle, address);
    case 4:
        return Inst(Opcode::LoadBufferF32x4, Flags{info}, handle, address);
    default:
        UNREACHABLE_MSG("Invalid number of dwords {}", num_dwords);
    }
}

void IREmitter::StoreBuffer(int num_dwords, const Value& handle, const Value& address,
                            const Value& data, BufferInstInfo info) {
    switch (num_dwords) {
    case 1:
        Inst(data.Type() == Type::F32 ? Opcode::StoreBufferF32 : Opcode::StoreBufferU32,
             Flags{info}, handle, address, data);
        break;
    case 2:
        Inst(Opcode::StoreBufferF32x2, Flags{info}, handle, address, data);
        break;
    case 3:
        Inst(Opcode::StoreBufferF32x3, Flags{info}, handle, address, data);
        break;
    case 4:
        Inst(Opcode::StoreBufferF32x4, Flags{info}, handle, address, data);
        break;
    default:
        UNREACHABLE_MSG("Invalid number of dwords {}", num_dwords);
    }
}

U32 IREmitter::LaneId() {
    return Inst<U32>(Opcode::LaneId);
}

U32 IREmitter::QuadShuffle(const U32& value, const U32& index) {
    return Inst<U32>(Opcode::QuadShuffle, value, index);
}

F32F64 IREmitter::FPAdd(const F32F64& a, const F32F64& b) {
    if (a.Type() != b.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", a.Type(), b.Type());
    }
    switch (a.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPAdd32, a, b);
    case Type::F64:
        return Inst<F64>(Opcode::FPAdd64, a, b);
    default:
        ThrowInvalidType(a.Type());
    }
}

F32F64 IREmitter::FPSub(const F32F64& a, const F32F64& b) {
    if (a.Type() != b.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", a.Type(), b.Type());
    }
    switch (a.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPSub32, a, b);
    default:
        ThrowInvalidType(a.Type());
    }
}

Value IREmitter::CompositeConstruct(const Value& e1, const Value& e2) {
    if (e1.Type() != e2.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", e1.Type(), e2.Type());
    }
    switch (e1.Type()) {
    case Type::U32:
        return Inst(Opcode::CompositeConstructU32x2, e1, e2);
    case Type::F16:
        return Inst(Opcode::CompositeConstructF16x2, e1, e2);
    case Type::F32:
        return Inst(Opcode::CompositeConstructF32x2, e1, e2);
    case Type::F64:
        return Inst(Opcode::CompositeConstructF64x2, e1, e2);
    default:
        ThrowInvalidType(e1.Type());
    }
}

Value IREmitter::CompositeConstruct(const Value& e1, const Value& e2, const Value& e3) {
    if (e1.Type() != e2.Type() || e1.Type() != e3.Type()) {
        UNREACHABLE_MSG("Mismatching types {}, {}, and {}", e1.Type(), e2.Type(), e3.Type());
    }
    switch (e1.Type()) {
    case Type::U32:
        return Inst(Opcode::CompositeConstructU32x3, e1, e2, e3);
    case Type::F16:
        return Inst(Opcode::CompositeConstructF16x3, e1, e2, e3);
    case Type::F32:
        return Inst(Opcode::CompositeConstructF32x3, e1, e2, e3);
    case Type::F64:
        return Inst(Opcode::CompositeConstructF64x3, e1, e2, e3);
    default:
        ThrowInvalidType(e1.Type());
    }
}

Value IREmitter::CompositeConstruct(const Value& e1, const Value& e2, const Value& e3,
                                    const Value& e4) {
    if (e1.Type() != e2.Type() || e1.Type() != e3.Type() || e1.Type() != e4.Type()) {
        UNREACHABLE_MSG("Mismatching types {}, {}, {}, and {}", e1.Type(), e2.Type(),
                              e3.Type(), e4.Type());
    }
    switch (e1.Type()) {
    case Type::U32:
        return Inst(Opcode::CompositeConstructU32x4, e1, e2, e3, e4);
    case Type::F16:
        return Inst(Opcode::CompositeConstructF16x4, e1, e2, e3, e4);
    case Type::F32:
        return Inst(Opcode::CompositeConstructF32x4, e1, e2, e3, e4);
    case Type::F64:
        return Inst(Opcode::CompositeConstructF64x4, e1, e2, e3, e4);
    default:
        ThrowInvalidType(e1.Type());
    }
}

Value IREmitter::CompositeExtract(const Value& vector, size_t element) {
    const auto read{[&](Opcode opcode, size_t limit) -> Value {
        if (element >= limit) {
            UNREACHABLE_MSG("Out of bounds element {}", element);
        }
        return Inst(opcode, vector, Value{static_cast<u32>(element)});
    }};
    switch (vector.Type()) {
    case Type::U32x2:
        return read(Opcode::CompositeExtractU32x2, 2);
    case Type::U32x3:
        return read(Opcode::CompositeExtractU32x3, 3);
    case Type::U32x4:
        return read(Opcode::CompositeExtractU32x4, 4);
    case Type::F16x2:
        return read(Opcode::CompositeExtractF16x2, 2);
    case Type::F16x3:
        return read(Opcode::CompositeExtractF16x3, 3);
    case Type::F16x4:
        return read(Opcode::CompositeExtractF16x4, 4);
    case Type::F32x2:
        return read(Opcode::CompositeExtractF32x2, 2);
    case Type::F32x3:
        return read(Opcode::CompositeExtractF32x3, 3);
    case Type::F32x4:
        return read(Opcode::CompositeExtractF32x4, 4);
    case Type::F64x2:
        return read(Opcode::CompositeExtractF64x2, 2);
    case Type::F64x3:
        return read(Opcode::CompositeExtractF64x3, 3);
    case Type::F64x4:
        return read(Opcode::CompositeExtractF64x4, 4);
    default:
        ThrowInvalidType(vector.Type());
    }
}

Value IREmitter::CompositeInsert(const Value& vector, const Value& object, size_t element) {
    const auto insert{[&](Opcode opcode, size_t limit) {
        if (element >= limit) {
            UNREACHABLE_MSG("Out of bounds element {}", element);
        }
        return Inst(opcode, vector, object, Value{static_cast<u32>(element)});
    }};
    switch (vector.Type()) {
    case Type::U32x2:
        return insert(Opcode::CompositeInsertU32x2, 2);
    case Type::U32x3:
        return insert(Opcode::CompositeInsertU32x3, 3);
    case Type::U32x4:
        return insert(Opcode::CompositeInsertU32x4, 4);
    case Type::F16x2:
        return insert(Opcode::CompositeInsertF16x2, 2);
    case Type::F16x3:
        return insert(Opcode::CompositeInsertF16x3, 3);
    case Type::F16x4:
        return insert(Opcode::CompositeInsertF16x4, 4);
    case Type::F32x2:
        return insert(Opcode::CompositeInsertF32x2, 2);
    case Type::F32x3:
        return insert(Opcode::CompositeInsertF32x3, 3);
    case Type::F32x4:
        return insert(Opcode::CompositeInsertF32x4, 4);
    case Type::F64x2:
        return insert(Opcode::CompositeInsertF64x2, 2);
    case Type::F64x3:
        return insert(Opcode::CompositeInsertF64x3, 3);
    case Type::F64x4:
        return insert(Opcode::CompositeInsertF64x4, 4);
    default:
        ThrowInvalidType(vector.Type());
    }
}

Value IREmitter::Select(const U1& condition, const Value& true_value, const Value& false_value) {
    if (true_value.Type() != false_value.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", true_value.Type(), false_value.Type());
    }
    switch (true_value.Type()) {
    case Type::U1:
        return Inst(Opcode::SelectU1, condition, true_value, false_value);
    case Type::U8:
        return Inst(Opcode::SelectU8, condition, true_value, false_value);
    case Type::U16:
        return Inst(Opcode::SelectU16, condition, true_value, false_value);
    case Type::U32:
        return Inst(Opcode::SelectU32, condition, true_value, false_value);
    case Type::U64:
        return Inst(Opcode::SelectU64, condition, true_value, false_value);
    case Type::F32:
        return Inst(Opcode::SelectF32, condition, true_value, false_value);
    case Type::F64:
        return Inst(Opcode::SelectF64, condition, true_value, false_value);
    default:
        UNREACHABLE_MSG("Invalid type {}", true_value.Type());
    }
}

U64 IREmitter::PackUint2x32(const Value& vector) {
    return Inst<U64>(Opcode::PackUint2x32, vector);
}

Value IREmitter::UnpackUint2x32(const U64& value) {
    return Inst<Value>(Opcode::UnpackUint2x32, value);
}

U32 IREmitter::PackFloat2x16(const Value& vector) {
    return Inst<U32>(Opcode::PackFloat2x16, vector);
}

Value IREmitter::UnpackFloat2x16(const U32& value) {
    return Inst(Opcode::UnpackFloat2x16, value);
}

U32 IREmitter::PackHalf2x16(const Value& vector) {
    return Inst<U32>(Opcode::PackHalf2x16, vector);
}

Value IREmitter::UnpackHalf2x16(const U32& value) {
    return Inst(Opcode::UnpackHalf2x16, value);
}

F32F64 IREmitter::FPMul(const F32F64& a, const F32F64& b) {
    if (a.Type() != b.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", a.Type(), b.Type());
    }
    switch (a.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPMul32, a, b);
    case Type::F64:
        return Inst<F64>(Opcode::FPMul64, a, b);
    default:
        ThrowInvalidType(a.Type());
    }
}

F32F64 IREmitter::FPFma(const F32F64& a, const F32F64& b, const F32F64& c) {
    if (a.Type() != b.Type() || a.Type() != c.Type()) {
        UNREACHABLE_MSG("Mismatching types {}, {}, and {}", a.Type(), b.Type(), c.Type());
    }
    switch (a.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPFma32, a, b, c);
    case Type::F64:
        return Inst<F64>(Opcode::FPFma64, a, b, c);
    default:
        ThrowInvalidType(a.Type());
    }
}

F32F64 IREmitter::FPAbs(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPAbs32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPAbs64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPNeg(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPNeg32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPNeg64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPAbsNeg(const F32F64& value, bool abs, bool neg) {
    F32F64 result{value};
    if (abs) {
        result = FPAbs(result);
    }
    if (neg) {
        result = FPNeg(result);
    }
    return result;
}

F32 IREmitter::FPCos(const F32& value) {
    return Inst<F32>(Opcode::FPCos, value);
}

F32 IREmitter::FPSin(const F32& value) {
    return Inst<F32>(Opcode::FPSin, value);
}

F32 IREmitter::FPExp2(const F32& value) {
    return Inst<F32>(Opcode::FPExp2, value);
}

F32 IREmitter::FPLog2(const F32& value) {
    return Inst<F32>(Opcode::FPLog2, value);
}

F32F64 IREmitter::FPRecip(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPRecip32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPRecip64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPRecipSqrt(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPRecipSqrt32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPRecipSqrt64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32 IREmitter::FPSqrt(const F32& value) {
    return Inst<F32>(Opcode::FPSqrt, value);
}

F32F64 IREmitter::FPSaturate(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPSaturate32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPSaturate64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPClamp(const F32F64& value, const F32F64& min_value, const F32F64& max_value) {
    if (value.Type() != min_value.Type() || value.Type() != max_value.Type()) {
        UNREACHABLE_MSG("Mismatching types {}, {}, and {}", value.Type(), min_value.Type(),
                              max_value.Type());
    }
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPClamp32, value, min_value, max_value);
    case Type::F64:
        return Inst<F64>(Opcode::FPClamp64, value, min_value, max_value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPRoundEven(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPRoundEven32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPRoundEven64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPFloor(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPFloor32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPFloor64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPCeil(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPCeil32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPCeil64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32F64 IREmitter::FPTrunc(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPTrunc32, value);
    case Type::F64:
        return Inst<F64>(Opcode::FPTrunc64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

F32 IREmitter::Fract(const F32& value) {
    return Inst<F32>(Opcode::FPFract, value);
}

U1 IREmitter::FPEqual(const F32F64& lhs, const F32F64& rhs, bool ordered) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<U1>(ordered ? Opcode::FPOrdEqual32 : Opcode::FPUnordEqual32, lhs, rhs);
    case Type::F64:
        return Inst<U1>(ordered ? Opcode::FPOrdEqual64 : Opcode::FPUnordEqual64, lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::FPNotEqual(const F32F64& lhs, const F32F64& rhs, bool ordered) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<U1>(ordered ? Opcode::FPOrdNotEqual32 : Opcode::FPUnordNotEqual32, lhs, rhs);
    case Type::F64:
        return Inst<U1>(ordered ? Opcode::FPOrdNotEqual64 : Opcode::FPUnordNotEqual64, lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::FPLessThan(const F32F64& lhs, const F32F64& rhs, bool ordered) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<U1>(ordered ? Opcode::FPOrdLessThan32 : Opcode::FPUnordLessThan32, lhs, rhs);
    case Type::F64:
        return Inst<U1>(ordered ? Opcode::FPOrdLessThan64 : Opcode::FPUnordLessThan64, lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::FPGreaterThan(const F32F64& lhs, const F32F64& rhs, bool ordered) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<U1>(ordered ? Opcode::FPOrdGreaterThan32 : Opcode::FPUnordGreaterThan32, lhs,
                        rhs);
    case Type::F64:
        return Inst<U1>(ordered ? Opcode::FPOrdGreaterThan64 : Opcode::FPUnordGreaterThan64, lhs,
                        rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::FPLessThanEqual(const F32F64& lhs, const F32F64& rhs, bool ordered) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<U1>(ordered ? Opcode::FPOrdLessThanEqual32 : Opcode::FPUnordLessThanEqual32,
                        lhs, rhs);
    case Type::F64:
        return Inst<U1>(ordered ? Opcode::FPOrdLessThanEqual64 : Opcode::FPUnordLessThanEqual64,
                        lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::FPGreaterThanEqual(const F32F64& lhs, const F32F64& rhs, bool ordered) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<U1>(ordered ? Opcode::FPOrdGreaterThanEqual32
                                : Opcode::FPUnordGreaterThanEqual32,
                        lhs, rhs);
    case Type::F64:
        return Inst<U1>(ordered ? Opcode::FPOrdGreaterThanEqual64
                                : Opcode::FPUnordGreaterThanEqual64,
                        lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::FPIsNan(const F32F64& value) {
    switch (value.Type()) {
    case Type::F32:
        return Inst<U1>(Opcode::FPIsNan32, value);
    case Type::F64:
        return Inst<U1>(Opcode::FPIsNan64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

U1 IREmitter::FPOrdered(const F32F64& lhs, const F32F64& rhs) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    return LogicalAnd(LogicalNot(FPIsNan(lhs)), LogicalNot(FPIsNan(rhs)));
}

U1 IREmitter::FPUnordered(const F32F64& lhs, const F32F64& rhs) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    return LogicalOr(FPIsNan(lhs), FPIsNan(rhs));
}

F32F64 IREmitter::FPMax(const F32F64& lhs, const F32F64& rhs) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPMax32, lhs, rhs);
    case Type::F64:
        return Inst<F64>(Opcode::FPMax64, lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

F32F64 IREmitter::FPMin(const F32F64& lhs, const F32F64& rhs) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::F32:
        return Inst<F32>(Opcode::FPMin32, lhs, rhs);
    case Type::F64:
        return Inst<F64>(Opcode::FPMin64, lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U32U64 IREmitter::IAdd(const U32U64& a, const U32U64& b) {
    if (a.Type() != b.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", a.Type(), b.Type());
    }
    switch (a.Type()) {
    case Type::U32:
        return Inst<U32>(Opcode::IAdd32, a, b);
    case Type::U64:
        return Inst<U64>(Opcode::IAdd64, a, b);
    default:
        ThrowInvalidType(a.Type());
    }
}

U32U64 IREmitter::ISub(const U32U64& a, const U32U64& b) {
    if (a.Type() != b.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", a.Type(), b.Type());
    }
    switch (a.Type()) {
    case Type::U32:
        return Inst<U32>(Opcode::ISub32, a, b);
    case Type::U64:
        return Inst<U64>(Opcode::ISub64, a, b);
    default:
        ThrowInvalidType(a.Type());
    }
}

IR::Value IREmitter::IMulExt(const U32& a, const U32& b, bool is_signed) {
    return Inst(is_signed ? Opcode::SMulExt : Opcode::UMulExt, a, b);
}

U32 IREmitter::IMul(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::IMul32, a, b);
}

U32 IREmitter::IDiv(const U32& a, const U32& b, bool is_signed) {
    return Inst<U32>(is_signed ? Opcode::SDiv32 : Opcode::UDiv32, a, b);
}

U32U64 IREmitter::INeg(const U32U64& value) {
    switch (value.Type()) {
    case Type::U32:
        return Inst<U32>(Opcode::INeg32, value);
    case Type::U64:
        return Inst<U64>(Opcode::INeg64, value);
    default:
        ThrowInvalidType(value.Type());
    }
}

U32 IREmitter::IAbs(const U32& value) {
    return Inst<U32>(Opcode::IAbs32, value);
}

U32U64 IREmitter::ShiftLeftLogical(const U32U64& base, const U32& shift) {
    switch (base.Type()) {
    case Type::U32:
        return Inst<U32>(Opcode::ShiftLeftLogical32, base, shift);
    case Type::U64:
        return Inst<U64>(Opcode::ShiftLeftLogical64, base, shift);
    default:
        ThrowInvalidType(base.Type());
    }
}

U32U64 IREmitter::ShiftRightLogical(const U32U64& base, const U32& shift) {
    switch (base.Type()) {
    case Type::U32:
        return Inst<U32>(Opcode::ShiftRightLogical32, base, shift);
    case Type::U64:
        return Inst<U64>(Opcode::ShiftRightLogical64, base, shift);
    default:
        ThrowInvalidType(base.Type());
    }
}

U32U64 IREmitter::ShiftRightArithmetic(const U32U64& base, const U32& shift) {
    switch (base.Type()) {
    case Type::U32:
        return Inst<U32>(Opcode::ShiftRightArithmetic32, base, shift);
    case Type::U64:
        return Inst<U64>(Opcode::ShiftRightArithmetic64, base, shift);
    default:
        ThrowInvalidType(base.Type());
    }
}

U32 IREmitter::BitwiseAnd(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::BitwiseAnd32, a, b);
}

U32 IREmitter::BitwiseOr(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::BitwiseOr32, a, b);
}

U32 IREmitter::BitwiseXor(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::BitwiseXor32, a, b);
}

U32 IREmitter::BitFieldInsert(const U32& base, const U32& insert, const U32& offset,
                              const U32& count) {
    return Inst<U32>(Opcode::BitFieldInsert, base, insert, offset, count);
}

U32 IREmitter::BitFieldExtract(const U32& base, const U32& offset, const U32& count,
                               bool is_signed) {
    return Inst<U32>(is_signed ? Opcode::BitFieldSExtract : Opcode::BitFieldUExtract, base, offset,
                     count);
}

U32 IREmitter::BitReverse(const U32& value) {
    return Inst<U32>(Opcode::BitReverse32, value);
}

U32 IREmitter::BitCount(const U32& value) {
    return Inst<U32>(Opcode::BitCount32, value);
}

U32 IREmitter::BitwiseNot(const U32& value) {
    return Inst<U32>(Opcode::BitwiseNot32, value);
}

U32 IREmitter::FindSMsb(const U32& value) {
    return Inst<U32>(Opcode::FindSMsb32, value);
}

U32 IREmitter::FindUMsb(const U32& value) {
    return Inst<U32>(Opcode::FindUMsb32, value);
}

U32 IREmitter::SMin(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::SMin32, a, b);
}

U32 IREmitter::UMin(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::UMin32, a, b);
}

U32 IREmitter::IMin(const U32& a, const U32& b, bool is_signed) {
    return is_signed ? SMin(a, b) : UMin(a, b);
}

U32 IREmitter::SMax(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::SMax32, a, b);
}

U32 IREmitter::UMax(const U32& a, const U32& b) {
    return Inst<U32>(Opcode::UMax32, a, b);
}

U32 IREmitter::IMax(const U32& a, const U32& b, bool is_signed) {
    return is_signed ? SMax(a, b) : UMax(a, b);
}

U32 IREmitter::SClamp(const U32& value, const U32& min, const U32& max) {
    return Inst<U32>(Opcode::SClamp32, value, min, max);
}

U32 IREmitter::UClamp(const U32& value, const U32& min, const U32& max) {
    return Inst<U32>(Opcode::UClamp32, value, min, max);
}

U1 IREmitter::ILessThan(const U32& lhs, const U32& rhs, bool is_signed) {
    return Inst<U1>(is_signed ? Opcode::SLessThan : Opcode::ULessThan, lhs, rhs);
}

U1 IREmitter::IEqual(const U32U64& lhs, const U32U64& rhs) {
    if (lhs.Type() != rhs.Type()) {
        UNREACHABLE_MSG("Mismatching types {} and {}", lhs.Type(), rhs.Type());
    }
    switch (lhs.Type()) {
    case Type::U32:
        return Inst<U1>(Opcode::IEqual, lhs, rhs);
    default:
        ThrowInvalidType(lhs.Type());
    }
}

U1 IREmitter::ILessThanEqual(const U32& lhs, const U32& rhs, bool is_signed) {
    return Inst<U1>(is_signed ? Opcode::SLessThanEqual : Opcode::ULessThanEqual, lhs, rhs);
}

U1 IREmitter::IGreaterThan(const U32& lhs, const U32& rhs, bool is_signed) {
    return Inst<U1>(is_signed ? Opcode::SGreaterThan : Opcode::UGreaterThan, lhs, rhs);
}

U1 IREmitter::INotEqual(const U32& lhs, const U32& rhs) {
    return Inst<U1>(Opcode::INotEqual, lhs, rhs);
}

U1 IREmitter::IGreaterThanEqual(const U32& lhs, const U32& rhs, bool is_signed) {
    return Inst<U1>(is_signed ? Opcode::SGreaterThanEqual : Opcode::UGreaterThanEqual, lhs, rhs);
}

U1 IREmitter::LogicalOr(const U1& a, const U1& b) {
    return Inst<U1>(Opcode::LogicalOr, a, b);
}

U1 IREmitter::LogicalAnd(const U1& a, const U1& b) {
    return Inst<U1>(Opcode::LogicalAnd, a, b);
}

U1 IREmitter::LogicalXor(const U1& a, const U1& b) {
    return Inst<U1>(Opcode::LogicalXor, a, b);
}

U1 IREmitter::LogicalNot(const U1& value) {
    return Inst<U1>(Opcode::LogicalNot, value);
}

U32U64 IREmitter::ConvertFToS(size_t bitsize, const F32F64& value) {
    switch (bitsize) {
    case 32:
        switch (value.Type()) {
        case Type::F32:
            return Inst<U32>(Opcode::ConvertS32F32, value);
        case Type::F64:
            return Inst<U32>(Opcode::ConvertS32F64, value);
        default:
            ThrowInvalidType(value.Type());
        }
    default:
        UNREACHABLE_MSG("Invalid destination bitsize {}", bitsize);
    }
}

U32U64 IREmitter::ConvertFToU(size_t bitsize, const F32F64& value) {
    switch (bitsize) {
    case 32:
        switch (value.Type()) {
        case Type::F32:
            return Inst<U32>(Opcode::ConvertU32F32, value);
        default:
            ThrowInvalidType(value.Type());
        }
    default:
        UNREACHABLE_MSG("Invalid destination bitsize {}", bitsize);
    }
}

U32U64 IREmitter::ConvertFToI(size_t bitsize, bool is_signed, const F32F64& value) {
    return is_signed ? ConvertFToS(bitsize, value) : ConvertFToU(bitsize, value);
}

F32F64 IREmitter::ConvertSToF(size_t dest_bitsize, size_t src_bitsize, const Value& value) {
    switch (dest_bitsize) {
    case 32:
        switch (src_bitsize) {
        case 32:
            return Inst<F32>(Opcode::ConvertF32S32, value);
        }
        break;
    case 64:
        switch (src_bitsize) {
        case 32:
            return Inst<F64>(Opcode::ConvertF64S32, value);
        }
        break;
    }
    UNREACHABLE_MSG("Invalid bit size combination dst={} src={}", dest_bitsize, src_bitsize);
}

F32F64 IREmitter::ConvertUToF(size_t dest_bitsize, size_t src_bitsize, const Value& value) {
    switch (dest_bitsize) {
    case 32:
        switch (src_bitsize) {
        case 32:
            return Inst<F32>(Opcode::ConvertF32U32, value);
        }
        break;
    case 64:
        switch (src_bitsize) {
        case 32:
            return Inst<F64>(Opcode::ConvertF64U32, value);
        }
        break;
    }
    UNREACHABLE_MSG("Invalid bit size combination dst={} src={}", dest_bitsize, src_bitsize);
}

F32F64 IREmitter::ConvertIToF(size_t dest_bitsize, size_t src_bitsize, bool is_signed,
                              const Value& value) {
    return is_signed ? ConvertSToF(dest_bitsize, src_bitsize, value)
                     : ConvertUToF(dest_bitsize, src_bitsize, value);
}

U32U64 IREmitter::UConvert(size_t result_bitsize, const U32U64& value) {
    throw NotImplementedException("Conversion from {} to {} bits", value.Type(), result_bitsize);
}

F16F32F64 IREmitter::FPConvert(size_t result_bitsize, const F16F32F64& value) {
    switch (result_bitsize) {
    case 16:
        switch (value.Type()) {
        case Type::F32:
            return Inst<F16>(Opcode::ConvertF16F32, value);
        }
        break;
    case 32:
        switch (value.Type()) {
        case Type::F16:
            return Inst<F32>(Opcode::ConvertF32F16, value);
        }
        break;
    }
    throw NotImplementedException("Conversion from {} to {} bits", value.Type(), result_bitsize);
}

Value IREmitter::ImageSampleImplicitLod(const Value& handle, const Value& coords, const F32& bias,
                                        const Value& offset, const F32& lod_clamp,
                                        TextureInstInfo info) {
    const Value bias_lc{MakeLodClampPair(*this, bias, lod_clamp)};
    return Inst(Opcode::ImageSampleImplicitLod, Flags{info}, handle, coords, bias_lc, offset);
}

Value IREmitter::ImageSampleExplicitLod(const Value& handle, const Value& coords, const F32& lod,
                                        const Value& offset, TextureInstInfo info) {
    return Inst(Opcode::ImageSampleExplicitLod, Flags{info}, handle, coords, lod, offset);
}

F32 IREmitter::ImageSampleDrefImplicitLod(const Value& handle, const Value& coords, const F32& dref,
                                          const F32& bias, const Value& offset,
                                          const F32& lod_clamp, TextureInstInfo info) {
    const Value bias_lc{MakeLodClampPair(*this, bias, lod_clamp)};
    return Inst<F32>(Opcode::ImageSampleDrefImplicitLod, Flags{info}, handle, coords, dref, bias_lc,
                     offset);
}

F32 IREmitter::ImageSampleDrefExplicitLod(const Value& handle, const Value& coords, const F32& dref,
                                          const F32& lod, const Value& offset,
                                          TextureInstInfo info) {
    return Inst<F32>(Opcode::ImageSampleDrefExplicitLod, Flags{info}, handle, coords, dref, lod,
                     offset);
}

Value IREmitter::ImageGather(const Value& handle, const Value& coords, const Value& offset,
                             const Value& offset2, TextureInstInfo info) {
    return Inst(Opcode::ImageGather, Flags{info}, handle, coords, offset, offset2);
}

Value IREmitter::ImageGatherDref(const Value& handle, const Value& coords, const Value& offset,
                                 const Value& offset2, const F32& dref, TextureInstInfo info) {
    return Inst(Opcode::ImageGatherDref, Flags{info}, handle, coords, offset, offset2, dref);
}

Value IREmitter::ImageFetch(const Value& handle, const Value& coords, const Value& offset,
                            const U32& lod, const U32& multisampling, TextureInstInfo info) {
    return Inst(Opcode::ImageFetch, Flags{info}, handle, coords, offset, lod, multisampling);
}

Value IREmitter::ImageQueryDimension(const Value& handle, const IR::U32& lod,
                                     const IR::U1& skip_mips) {
    return Inst(Opcode::ImageQueryDimensions, handle, lod, skip_mips);
}

Value IREmitter::ImageQueryDimension(const Value& handle, const IR::U32& lod,
                                     const IR::U1& skip_mips, TextureInstInfo info) {
    return Inst(Opcode::ImageQueryDimensions, Flags{info}, handle, lod, skip_mips);
}

Value IREmitter::ImageQueryLod(const Value& handle, const Value& coords, TextureInstInfo info) {
    return Inst(Opcode::ImageQueryLod, Flags{info}, handle, coords);
}

Value IREmitter::ImageGradient(const Value& handle, const Value& coords, const Value& derivatives,
                               const Value& offset, const F32& lod_clamp, TextureInstInfo info) {
    return Inst(Opcode::ImageGradient, Flags{info}, handle, coords, derivatives, offset, lod_clamp);
}

Value IREmitter::ImageRead(const Value& handle, const Value& coords, TextureInstInfo info) {
    return Inst(Opcode::ImageRead, Flags{info}, handle, coords);
}

void IREmitter::ImageWrite(const Value& handle, const Value& coords, const Value& color,
                           TextureInstInfo info) {
    Inst(Opcode::ImageWrite, Flags{info}, handle, coords, color);
}

} // namespace Shader::IR
