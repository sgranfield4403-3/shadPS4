// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/exception.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/frontend/translate/translate.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/resource.h"

namespace Shader::Gcn {

std::array<bool, IR::NumScalarRegs> Translator::exec_contexts{};

Translator::Translator(IR::Block* block_, Info& info_)
    : ir{*block_, block_->begin()}, info{info_} {}

void Translator::EmitPrologue() {
    exec_contexts.fill(false);
    ir.Prologue();
    ir.SetExec(ir.Imm1(true));

    // Initialize user data.
    IR::ScalarReg dst_sreg = IR::ScalarReg::S0;
    for (u32 i = 0; i < info.num_user_data; i++) {
        ir.SetScalarReg(dst_sreg++, ir.GetUserData(dst_sreg));
    }

    IR::VectorReg dst_vreg = IR::VectorReg::V0;
    switch (info.stage) {
    case Stage::Vertex:
        // https://github.com/chaotic-cx/mesa-mirror/blob/72326e15/src/amd/vulkan/radv_shader_args.c#L146C1-L146C23
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::VertexId));
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::InstanceId));
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::PrimitiveId));
        break;
    case Stage::Fragment:
        // https://github.com/chaotic-cx/mesa-mirror/blob/72326e15/src/amd/vulkan/radv_shader_args.c#L258
        // The first two VGPRs are used for i/j barycentric coordinates. In the vast majority of
        // cases it will be only those two, but if shader is using both e.g linear and perspective
        // inputs it can be more For now assume that this isn't the case.
        dst_vreg = IR::VectorReg::V2;
        for (u32 i = 0; i < 4; i++) {
            ir.SetVectorReg(dst_vreg++, ir.GetAttribute(IR::Attribute::FragCoord, i));
        }
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::IsFrontFace));
        break;
    case Stage::Compute:
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::LocalInvocationId, 0));
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::LocalInvocationId, 1));
        ir.SetVectorReg(dst_vreg++, ir.GetAttributeU32(IR::Attribute::LocalInvocationId, 2));

        ir.SetScalarReg(dst_sreg++, ir.GetAttributeU32(IR::Attribute::WorkgroupId, 0));
        ir.SetScalarReg(dst_sreg++, ir.GetAttributeU32(IR::Attribute::WorkgroupId, 1));
        ir.SetScalarReg(dst_sreg++, ir.GetAttributeU32(IR::Attribute::WorkgroupId, 2));
        break;
    default:
        throw NotImplementedException("Unknown shader stage");
    }
}

IR::U32F32 Translator::GetSrc(const InstOperand& operand, bool force_flt) {
    // Input modifiers work on float values.
    force_flt |= operand.input_modifier.abs | operand.input_modifier.neg;

    IR::U32F32 value{};
    switch (operand.field) {
    case OperandField::ScalarGPR:
        if (operand.type == ScalarType::Float32 || force_flt) {
            value = ir.GetScalarReg<IR::F32>(IR::ScalarReg(operand.code));
        } else {
            value = ir.GetScalarReg<IR::U32>(IR::ScalarReg(operand.code));
        }
        break;
    case OperandField::VectorGPR:
        if (operand.type == ScalarType::Float32 || force_flt) {
            value = ir.GetVectorReg<IR::F32>(IR::VectorReg(operand.code));
        } else {
            value = ir.GetVectorReg<IR::U32>(IR::VectorReg(operand.code));
        }
        break;
    case OperandField::ConstZero:
        if (force_flt) {
            value = ir.Imm32(0.f);
        } else {
            value = ir.Imm32(0U);
        }
        break;
    case OperandField::SignedConstIntPos:
        ASSERT(!force_flt);
        value = ir.Imm32(operand.code - SignedConstIntPosMin + 1);
        break;
    case OperandField::SignedConstIntNeg:
        ASSERT(!force_flt);
        value = ir.Imm32(-s32(operand.code) + SignedConstIntNegMin - 1);
        break;
    case OperandField::LiteralConst:
        if (force_flt) {
            value = ir.Imm32(std::bit_cast<float>(operand.code));
        } else {
            value = ir.Imm32(operand.code);
        }
        break;
    case OperandField::ConstFloatPos_1_0:
        value = ir.Imm32(1.f);
        break;
    case OperandField::ConstFloatPos_0_5:
        value = ir.Imm32(0.5f);
        break;
    case OperandField::ConstFloatPos_2_0:
        value = ir.Imm32(2.0f);
        break;
    case OperandField::ConstFloatPos_4_0:
        value = ir.Imm32(4.0f);
        break;
    case OperandField::ConstFloatNeg_0_5:
        value = ir.Imm32(-0.5f);
        break;
    case OperandField::ConstFloatNeg_1_0:
        value = ir.Imm32(-1.0f);
        break;
    case OperandField::ConstFloatNeg_2_0:
        value = ir.Imm32(-2.0f);
        break;
    case OperandField::VccLo:
        if (force_flt) {
            value = ir.BitCast<IR::F32>(ir.GetVccLo());
        } else {
            value = ir.GetVccLo();
        }
        break;
    case OperandField::VccHi:
        if (force_flt) {
            value = ir.BitCast<IR::F32>(ir.GetVccHi());
        } else {
            value = ir.GetVccHi();
        }
        break;
    default:
        UNREACHABLE();
    }

    if (operand.input_modifier.abs) {
        value = ir.FPAbs(value);
    }
    if (operand.input_modifier.neg) {
        value = ir.FPNeg(value);
    }
    return value;
}

void Translator::SetDst(const InstOperand& operand, const IR::U32F32& value) {
    IR::U32F32 result = value;
    if (operand.output_modifier.multiplier != 0.f) {
        result = ir.FPMul(result, ir.Imm32(operand.output_modifier.multiplier));
    }
    if (operand.output_modifier.clamp) {
        result = ir.FPSaturate(value);
    }
    switch (operand.field) {
    case OperandField::ScalarGPR:
        return ir.SetScalarReg(IR::ScalarReg(operand.code), result);
    case OperandField::VectorGPR:
        return ir.SetVectorReg(IR::VectorReg(operand.code), result);
    case OperandField::VccLo:
        return ir.SetVccLo(result);
    case OperandField::VccHi:
        return ir.SetVccHi(result);
    case OperandField::M0:
        break;
    default:
        UNREACHABLE();
    }
}

void Translator::EmitFetch(const GcnInst& inst) {
    // Read the pointer to the fetch shader assembly.
    const u32 sgpr_base = inst.src[0].code;
    const u32* code;
    std::memcpy(&code, &info.user_data[sgpr_base], sizeof(code));

    // Parse the assembly to generate a list of attributes.
    const auto attribs = ParseFetchShader(code);
    for (const auto& attrib : attribs) {
        const IR::Attribute attr{IR::Attribute::Param0 + attrib.semantic};
        IR::VectorReg dst_reg{attrib.dest_vgpr};
        for (u32 i = 0; i < attrib.num_elements; i++) {
            ir.SetVectorReg(dst_reg++, ir.GetAttribute(attr, i));
        }

        // Read the V# of the attribute to figure out component number and type.
        const auto buffer = info.ReadUd<AmdGpu::Buffer>(attrib.sgpr_base, attrib.dword_offset);
        const u32 num_components = AmdGpu::NumComponents(buffer.data_format);
        info.vs_inputs.push_back({
            .fmt = buffer.num_format,
            .binding = attrib.semantic,
            .num_components = std::min<u16>(attrib.num_elements, num_components),
            .sgpr_base = attrib.sgpr_base,
            .dword_offset = attrib.dword_offset,
        });
    }
}

void Translate(IR::Block* block, std::span<const GcnInst> inst_list, Info& info) {
    if (inst_list.empty()) {
        return;
    }
    Translator translator{block, info};
    for (const auto& inst : inst_list) {
        switch (inst.opcode) {
        case Opcode::S_MOV_B32:
            translator.S_MOV(inst);
            break;
        case Opcode::S_MUL_I32:
            translator.S_MUL_I32(inst);
            break;
        case Opcode::V_MAD_F32:
            translator.V_MAD_F32(inst);
            break;
        case Opcode::V_MOV_B32:
            translator.V_MOV(inst);
            break;
        case Opcode::V_MAC_F32:
            translator.V_MAC_F32(inst);
            break;
        case Opcode::V_MUL_F32:
            translator.V_MUL_F32(inst);
            break;
        case Opcode::V_AND_B32:
            translator.V_AND_B32(inst);
            break;
        case Opcode::V_LSHLREV_B32:
            translator.V_LSHLREV_B32(inst);
            break;
        case Opcode::V_ADD_I32:
            translator.V_ADD_I32(inst);
            break;
        case Opcode::V_CVT_F32_I32:
            translator.V_CVT_F32_I32(inst);
            break;
        case Opcode::V_CVT_F32_U32:
            translator.V_CVT_F32_U32(inst);
            break;
        case Opcode::V_RCP_F32:
            translator.V_RCP_F32(inst);
            break;
        case Opcode::S_SWAPPC_B64:
            ASSERT(info.stage == Stage::Vertex);
            translator.EmitFetch(inst);
            break;
        case Opcode::S_WAITCNT:
            break;
        case Opcode::S_LOAD_DWORDX4:
            translator.S_LOAD_DWORD(4, inst);
            break;
        case Opcode::S_LOAD_DWORDX8:
            translator.S_LOAD_DWORD(8, inst);
            break;
        case Opcode::S_BUFFER_LOAD_DWORD:
            translator.S_BUFFER_LOAD_DWORD(1, inst);
            break;
        case Opcode::S_BUFFER_LOAD_DWORDX2:
            translator.S_BUFFER_LOAD_DWORD(2, inst);
            break;
        case Opcode::S_BUFFER_LOAD_DWORDX4:
            translator.S_BUFFER_LOAD_DWORD(4, inst);
            break;
        case Opcode::S_BUFFER_LOAD_DWORDX8:
            translator.S_BUFFER_LOAD_DWORD(8, inst);
            break;
        case Opcode::S_BUFFER_LOAD_DWORDX16:
            translator.S_BUFFER_LOAD_DWORD(16, inst);
            break;
        case Opcode::EXP:
            translator.EXP(inst);
            break;
        case Opcode::V_INTERP_P2_F32:
            translator.V_INTERP_P2_F32(inst);
            break;
        case Opcode::V_CVT_PKRTZ_F16_F32:
            translator.V_CVT_PKRTZ_F16_F32(inst);
            break;
        case Opcode::V_FRACT_F32:
            translator.V_FRACT_F32(inst);
            break;
        case Opcode::V_ADD_F32:
            translator.V_ADD_F32(inst);
            break;
        case Opcode::V_CVT_OFF_F32_I4:
            translator.V_CVT_OFF_F32_I4(inst);
            break;
        case Opcode::V_MED3_F32:
            translator.V_MED3_F32(inst);
            break;
        case Opcode::V_FLOOR_F32:
            translator.V_FLOOR_F32(inst);
            break;
        case Opcode::V_SUB_F32:
            translator.V_SUB_F32(inst);
            break;
        case Opcode::V_FMA_F32:
        case Opcode::V_MADAK_F32: // Yes these can share the opcode
            translator.V_FMA_F32(inst);
            break;
        case Opcode::IMAGE_SAMPLE_C_LZ:
        case Opcode::IMAGE_SAMPLE_LZ:
        case Opcode::IMAGE_SAMPLE:
            translator.IMAGE_SAMPLE(inst);
            break;
        case Opcode::IMAGE_STORE:
            translator.IMAGE_STORE(inst);
            break;
        case Opcode::IMAGE_LOAD_MIP:
            translator.IMAGE_LOAD_MIP(inst);
            break;
        case Opcode::V_CMP_GE_I32:
            translator.V_CMP_U32(ConditionOp::GE, true, false, inst);
            break;
        case Opcode::V_CMP_EQ_I32:
            translator.V_CMP_U32(ConditionOp::EQ, true, false, inst);
            break;
        case Opcode::V_CMP_NE_U32:
            translator.V_CMP_U32(ConditionOp::LG, false, false, inst);
            break;
        case Opcode::V_CMP_EQ_U32:
            translator.V_CMP_U32(ConditionOp::EQ, false, false, inst);
            break;
        case Opcode::V_CMP_F_U32:
            translator.V_CMP_U32(ConditionOp::F, false, false, inst);
            break;
        case Opcode::V_CMP_LT_U32:
            translator.V_CMP_U32(ConditionOp::LT, false, false, inst);
            break;
        case Opcode::V_CMP_GT_U32:
            translator.V_CMP_U32(ConditionOp::GT, false, false, inst);
            break;
        case Opcode::V_CMP_GE_U32:
            translator.V_CMP_U32(ConditionOp::GE, false, false, inst);
            break;
        case Opcode::V_CMP_TRU_U32:
            translator.V_CMP_U32(ConditionOp::TRU, false, false, inst);
            break;
        case Opcode::V_CMP_NEQ_F32:
            translator.V_CMP_F32(ConditionOp::LG, false, inst);
            break;
        case Opcode::V_CMP_F_F32:
            translator.V_CMP_F32(ConditionOp::F, false, inst);
            break;
        case Opcode::V_CMP_LT_F32:
            translator.V_CMP_F32(ConditionOp::LT, false, inst);
            break;
        case Opcode::V_CMP_EQ_F32:
            translator.V_CMP_F32(ConditionOp::EQ, false, inst);
            break;
        case Opcode::V_CMP_LE_F32:
            translator.V_CMP_F32(ConditionOp::LE, false, inst);
            break;
        case Opcode::V_CMP_GT_F32:
            translator.V_CMP_F32(ConditionOp::GT, false, inst);
            break;
        case Opcode::V_CMP_LG_F32:
            translator.V_CMP_F32(ConditionOp::LG, false, inst);
            break;
        case Opcode::V_CMP_GE_F32:
            translator.V_CMP_F32(ConditionOp::GE, false, inst);
            break;
        case Opcode::V_CMP_NLE_F32:
            translator.V_CMP_F32(ConditionOp::GT, false, inst);
            break;
        case Opcode::S_CMP_LG_U32:
            translator.S_CMP(ConditionOp::LG, false, inst);
            break;
        case Opcode::S_CMP_LT_I32:
            translator.S_CMP(ConditionOp::LT, true, inst);
            break;
        case Opcode::S_CMP_LG_I32:
            translator.S_CMP(ConditionOp::LG, true, inst);
            break;
        case Opcode::S_CMP_GT_I32:
            translator.S_CMP(ConditionOp::GT, true, inst);
            break;
        case Opcode::S_CMP_EQ_I32:
            translator.S_CMP(ConditionOp::EQ, true, inst);
            break;
        case Opcode::S_CMP_EQ_U32:
            translator.S_CMP(ConditionOp::EQ, false, inst);
            break;
        case Opcode::S_LSHL_B32:
            translator.S_LSHL_B32(inst);
            break;
        case Opcode::V_CNDMASK_B32:
            translator.V_CNDMASK_B32(inst);
            break;
        case Opcode::TBUFFER_LOAD_FORMAT_XYZ:
            translator.BUFFER_LOAD_FORMAT(3, true, inst);
            break;
        case Opcode::TBUFFER_LOAD_FORMAT_XYZW:
            translator.BUFFER_LOAD_FORMAT(4, true, inst);
            break;
        case Opcode::BUFFER_LOAD_FORMAT_X:
            translator.BUFFER_LOAD_FORMAT(1, false, inst);
            break;
        case Opcode::BUFFER_STORE_FORMAT_X:
            translator.BUFFER_STORE_FORMAT(1, false, inst);
            break;
        case Opcode::V_MAX_F32:
            translator.V_MAX_F32(inst);
            break;
        case Opcode::V_RSQ_F32:
            translator.V_RSQ_F32(inst);
            break;
        case Opcode::S_ANDN2_B64:
            translator.S_ANDN2_B64(inst);
            break;
        case Opcode::V_SIN_F32:
            translator.V_SIN_F32(inst);
            break;
        case Opcode::V_COS_F32:
            translator.V_COS_F32(inst);
            break;
        case Opcode::V_LOG_F32:
            translator.V_LOG_F32(inst);
            break;
        case Opcode::V_EXP_F32:
            translator.V_EXP_F32(inst);
            break;
        case Opcode::V_SQRT_F32:
            translator.V_SQRT_F32(inst);
            break;
        case Opcode::V_MIN_F32:
            translator.V_MIN_F32(inst);
            break;
        case Opcode::V_MIN_I32:
            translator.V_MIN_I32(inst);
            break;
        case Opcode::V_MIN3_F32:
            translator.V_MIN3_F32(inst);
            break;
        case Opcode::V_MADMK_F32:
            translator.V_MADMK_F32(inst);
            break;
        case Opcode::V_CUBEMA_F32:
            translator.V_CUBEMA_F32(inst);
            break;
        case Opcode::V_CUBESC_F32:
            translator.V_CUBESC_F32(inst);
            break;
        case Opcode::V_CUBETC_F32:
            translator.V_CUBETC_F32(inst);
            break;
        case Opcode::V_CUBEID_F32:
            translator.V_CUBEID_F32(inst);
            break;
        case Opcode::V_CVT_U32_F32:
            translator.V_CVT_U32_F32(inst);
            break;
        case Opcode::V_CVT_I32_F32:
            translator.V_CVT_I32_F32(inst);
            break;
        case Opcode::V_SUBREV_F32:
            translator.V_SUBREV_F32(inst);
            break;
        case Opcode::S_AND_SAVEEXEC_B64:
            translator.S_AND_SAVEEXEC_B64(inst);
            break;
        case Opcode::S_MOV_B64:
            translator.S_MOV_B64(inst);
            break;
        case Opcode::V_SUBREV_I32:
            translator.V_SUBREV_I32(inst);
            break;

        case Opcode::V_CMPX_F_F32:
            translator.V_CMP_F32(ConditionOp::F, true, inst);
            break;
        case Opcode::V_CMPX_LT_F32:
            translator.V_CMP_F32(ConditionOp::LT, true, inst);
            break;
        case Opcode::V_CMPX_EQ_F32:
            translator.V_CMP_F32(ConditionOp::EQ, true, inst);
            break;
        case Opcode::V_CMPX_LE_F32:
            translator.V_CMP_F32(ConditionOp::LE, true, inst);
            break;
        case Opcode::V_CMPX_GT_F32:
            translator.V_CMP_F32(ConditionOp::GT, true, inst);
            break;
        case Opcode::V_CMPX_LG_F32:
            translator.V_CMP_F32(ConditionOp::LG, true, inst);
            break;
        case Opcode::V_CMPX_GE_F32:
            translator.V_CMP_F32(ConditionOp::GE, true, inst);
            break;
        case Opcode::V_CMPX_NGE_F32:
            translator.V_CMP_F32(ConditionOp::LT, true, inst);
            break;
        case Opcode::V_CMPX_NLG_F32:
            translator.V_CMP_F32(ConditionOp::EQ, true, inst);
            break;
        case Opcode::V_CMPX_NGT_F32:
            translator.V_CMP_F32(ConditionOp::LE, true, inst);
            break;
        case Opcode::V_CMPX_NLE_F32:
            translator.V_CMP_F32(ConditionOp::GT, true, inst);
            break;
        case Opcode::V_CMPX_NEQ_F32:
            translator.V_CMP_F32(ConditionOp::LG, true, inst);
            break;
        case Opcode::V_CMPX_NLT_F32:
            translator.V_CMP_F32(ConditionOp::GE, true, inst);
            break;
        case Opcode::V_CMPX_TRU_F32:
            translator.V_CMP_F32(ConditionOp::TRU, true, inst);
            break;
        case Opcode::V_CMP_LE_U32:
            translator.V_CMP_U32(ConditionOp::LE, false, false, inst);
            break;
        case Opcode::V_CMP_GT_I32:
            translator.V_CMP_U32(ConditionOp::GT, true, false, inst);
            break;
        case Opcode::V_CMP_LT_I32:
            translator.V_CMP_U32(ConditionOp::LT, true, false, inst);
            break;
        case Opcode::V_CMPX_LT_I32:
            translator.V_CMP_U32(ConditionOp::LT, true, true, inst);
            break;
        case Opcode::V_CMPX_F_U32:
            translator.V_CMP_U32(ConditionOp::F, false, true, inst);
            break;
        case Opcode::V_CMPX_LT_U32:
            translator.V_CMP_U32(ConditionOp::LT, false, true, inst);
            break;
        case Opcode::V_CMPX_EQ_U32:
            translator.V_CMP_U32(ConditionOp::EQ, false, true, inst);
            break;
        case Opcode::V_CMPX_LE_U32:
            translator.V_CMP_U32(ConditionOp::LE, false, true, inst);
            break;
        case Opcode::V_CMPX_GT_U32:
            translator.V_CMP_U32(ConditionOp::GT, false, true, inst);
            break;
        case Opcode::V_CMPX_NE_U32:
            translator.V_CMP_U32(ConditionOp::LG, false, true, inst);
            break;
        case Opcode::V_CMPX_GE_U32:
            translator.V_CMP_U32(ConditionOp::GE, false, true, inst);
            break;
        case Opcode::V_CMPX_TRU_U32:
            translator.V_CMP_U32(ConditionOp::TRU, false, true, inst);
            break;
        case Opcode::S_OR_B64:
            translator.S_OR_B64(false, inst);
            break;
        case Opcode::S_NOR_B64:
            translator.S_OR_B64(true, inst);
            break;
        case Opcode::S_AND_B64:
            translator.S_AND_B64(inst);
            break;
        case Opcode::V_LSHRREV_B32:
            translator.V_LSHRREV_B32(inst);
            break;
        case Opcode::S_ADD_I32:
            translator.S_ADD_I32(inst);
            break;
        case Opcode::V_MUL_LO_I32:
            translator.V_MUL_LO_I32(inst);
            break;
        case Opcode::V_SAD_U32:
            translator.V_SAD_U32(inst);
            break;
        case Opcode::V_BFE_U32:
            translator.V_BFE_U32(inst);
            break;
        case Opcode::V_MAD_I32_I24:
            translator.V_MAD_I32_I24(inst);
            break;
        case Opcode::V_MUL_I32_I24:
            translator.V_MUL_I32_I24(inst);
            break;
        case Opcode::V_SUB_I32:
            translator.V_SUB_I32(inst);
            break;
        case Opcode::V_LSHR_B32:
            translator.V_LSHR_B32(inst);
            break;
        case Opcode::V_ASHRREV_I32:
            translator.V_ASHRREV_I32(inst);
            break;
        case Opcode::V_MAD_U32_U24:
            translator.V_MAD_U32_U24(inst);
            break;
        case Opcode::S_AND_B32:
            translator.S_AND_B32(inst);
            break;
        case Opcode::S_LSHR_B32:
            translator.S_LSHR_B32(inst);
            break;
        case Opcode::S_CSELECT_B32:
            translator.S_CSELECT_B32(inst);
            break;
        case Opcode::S_CSELECT_B64:
            translator.S_CSELECT_B64(inst);
            break;
        case Opcode::S_BFE_U32:
            translator.S_BFE_U32(inst);
            break;
        case Opcode::V_RNDNE_F32:
            translator.V_RNDNE_F32(inst);
            break;
        case Opcode::V_BCNT_U32_B32:
            translator.V_BCNT_U32_B32(inst);
            break;
        case Opcode::V_MAX3_F32:
            translator.V_MAX3_F32(inst);
            break;
        case Opcode::DS_SWIZZLE_B32:
            translator.DS_SWIZZLE_B32(inst);
            break;
        case Opcode::V_MUL_LO_U32:
            translator.V_MUL_LO_U32(inst);
            break;
        case Opcode::S_BFM_B32:
            translator.S_BFM_B32(inst);
            break;
        case Opcode::S_NOP:
        case Opcode::S_CBRANCH_EXECZ:
        case Opcode::S_CBRANCH_SCC0:
        case Opcode::S_CBRANCH_SCC1:
        case Opcode::S_CBRANCH_VCCNZ:
        case Opcode::S_CBRANCH_VCCZ:
        case Opcode::S_BRANCH:
        case Opcode::S_WQM_B64:
        case Opcode::V_INTERP_P1_F32:
        case Opcode::S_ENDPGM:
            break;
        default:
            const u32 opcode = u32(inst.opcode);
            UNREACHABLE_MSG("Unknown opcode {}", opcode);
        }
    }
}

} // namespace Shader::Gcn
