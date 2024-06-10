// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include <span>
#include "shader_recompiler/frontend/instruction.h"
#include "shader_recompiler/ir/basic_block.h"
#include "shader_recompiler/ir/ir_emitter.h"
#include "shader_recompiler/runtime_info.h"

namespace Shader {
struct Info;
}

namespace Shader::Gcn {

enum class ConditionOp : u32 {
    F,
    EQ,
    LG,
    GT,
    GE,
    LT,
    LE,
    TRU,
};

class Translator {
public:
    explicit Translator(IR::Block* block_, Info& info);

    void EmitPrologue();
    void EmitFetch(const GcnInst& inst);

    // Scalar ALU
    void S_MOV(const GcnInst& inst);
    void S_MUL_I32(const GcnInst& inst);
    void S_CMP(ConditionOp cond, bool is_signed, const GcnInst& inst);
    void S_ANDN2_B64(const GcnInst& inst);
    void S_AND_SAVEEXEC_B64(const GcnInst& inst);
    void S_MOV_B64(const GcnInst& inst);
    void S_OR_B64(bool negate, const GcnInst& inst);
    void S_AND_B64(const GcnInst& inst);
    void S_ADD_I32(const GcnInst& inst);
    void S_AND_B32(const GcnInst& inst);
    void S_LSHR_B32(const GcnInst& inst);
    void S_CSELECT_B32(const GcnInst& inst);
    void S_CSELECT_B64(const GcnInst& inst);
    void S_BFE_U32(const GcnInst& inst);
    void S_LSHL_B32(const GcnInst& inst);
    void S_BFM_B32(const GcnInst& inst);

    // Scalar Memory
    void S_LOAD_DWORD(int num_dwords, const GcnInst& inst);
    void S_BUFFER_LOAD_DWORD(int num_dwords, const GcnInst& inst);

    // Vector ALU
    void V_MOV(const GcnInst& inst);
    void V_SAD(const GcnInst& inst);
    void V_MAC_F32(const GcnInst& inst);
    void V_CVT_PKRTZ_F16_F32(const GcnInst& inst);
    void V_MUL_F32(const GcnInst& inst);
    void V_CNDMASK_B32(const GcnInst& inst);
    void V_AND_B32(const GcnInst& inst);
    void V_LSHLREV_B32(const GcnInst& inst);
    void V_ADD_I32(const GcnInst& inst);
    void V_CVT_F32_I32(const GcnInst& inst);
    void V_CVT_F32_U32(const GcnInst& inst);
    void V_MAD_F32(const GcnInst& inst);
    void V_FRACT_F32(const GcnInst& inst);
    void V_ADD_F32(const GcnInst& inst);
    void V_CVT_OFF_F32_I4(const GcnInst& inst);
    void V_MED3_F32(const GcnInst& inst);
    void V_FLOOR_F32(const GcnInst& inst);
    void V_SUB_F32(const GcnInst& inst);
    void V_RCP_F32(const GcnInst& inst);
    void V_FMA_F32(const GcnInst& inst);
    void V_CMP_F32(ConditionOp op, bool set_exec, const GcnInst& inst);
    void V_MAX_F32(const GcnInst& inst);
    void V_RSQ_F32(const GcnInst& inst);
    void V_SIN_F32(const GcnInst& inst);
    void V_LOG_F32(const GcnInst& inst);
    void V_EXP_F32(const GcnInst& inst);
    void V_SQRT_F32(const GcnInst& inst);
    void V_MIN_F32(const GcnInst& inst);
    void V_MIN3_F32(const GcnInst& inst);
    void V_MADMK_F32(const GcnInst& inst);
    void V_CUBEMA_F32(const GcnInst& inst);
    void V_CUBESC_F32(const GcnInst& inst);
    void V_CUBETC_F32(const GcnInst& inst);
    void V_CUBEID_F32(const GcnInst& inst);
    void V_CVT_U32_F32(const GcnInst& inst);
    void V_SUBREV_F32(const GcnInst& inst);
    void V_SUBREV_I32(const GcnInst& inst);
    void V_CMP_U32(ConditionOp op, bool is_signed, bool set_exec, const GcnInst& inst);
    void V_LSHRREV_B32(const GcnInst& inst);
    void V_MUL_LO_I32(const GcnInst& inst);
    void V_SAD_U32(const GcnInst& inst);
    void V_BFE_U32(const GcnInst& inst);
    void V_MAD_I32_I24(const GcnInst& inst);
    void V_MUL_I32_I24(const GcnInst& inst);
    void V_SUB_I32(const GcnInst& inst);
    void V_LSHR_B32(const GcnInst& inst);
    void V_ASHRREV_I32(const GcnInst& inst);
    void V_MAD_U32_U24(const GcnInst& inst);
    void V_RNDNE_F32(const GcnInst& inst);
    void V_BCNT_U32_B32(const GcnInst& inst);
    void V_COS_F32(const GcnInst& inst);
    void V_MAX3_F32(const GcnInst& inst);
    void V_CVT_I32_F32(const GcnInst& inst);
    void V_MIN_I32(const GcnInst& inst);
    void V_MUL_LO_U32(const GcnInst& inst);

    // Vector Memory
    void BUFFER_LOAD_FORMAT(u32 num_dwords, bool is_typed, const GcnInst& inst);
    void BUFFER_STORE_FORMAT(u32 num_dwords, bool is_typed, const GcnInst& inst);

    // Vector interpolation
    void V_INTERP_P2_F32(const GcnInst& inst);

    // Data share
    void DS_SWIZZLE_B32(const GcnInst& inst);
    void DS_READ(int bit_size, bool is_signed, bool is_pair, const GcnInst& inst);
    void DS_WRITE(int bit_size, bool is_signed, bool is_pair, const GcnInst& inst);

    // MIMG
    void IMAGE_GET_RESINFO(const GcnInst& inst);
    void IMAGE_SAMPLE(const GcnInst& inst);
    void IMAGE_STORE(const GcnInst& inst);
    void IMAGE_LOAD_MIP(const GcnInst& inst);

    // Export
    void EXP(const GcnInst& inst);

private:
    IR::U32F32 GetSrc(const InstOperand& operand, bool flt_zero = false);
    void SetDst(const InstOperand& operand, const IR::U32F32& value);

private:
    IR::IREmitter ir;
    Info& info;
    static std::array<bool, IR::NumScalarRegs> exec_contexts;
};

void Translate(IR::Block* block, std::span<const GcnInst> inst_list, Info& info);

} // namespace Shader::Gcn
