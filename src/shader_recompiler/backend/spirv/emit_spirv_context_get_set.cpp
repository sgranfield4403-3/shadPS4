// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"

#include <magic_enum.hpp>

namespace Shader::Backend::SPIRV {
namespace {

Id VsOutputAttrPointer(EmitContext& ctx, VsOutput output) {
    switch (output) {
    case VsOutput::ClipDist0:
    case VsOutput::ClipDist1:
    case VsOutput::ClipDist2:
    case VsOutput::ClipDist3:
    case VsOutput::ClipDist4:
    case VsOutput::ClipDist5:
    case VsOutput::ClipDist6:
    case VsOutput::ClipDist7: {
        const u32 index = u32(output) - u32(VsOutput::ClipDist0);
        const Id clip_num{ctx.ConstU32(index)};
        ASSERT_MSG(Sirit::ValidId(ctx.clip_distances), "Clip distance used but not defined");
        return ctx.OpAccessChain(ctx.output_f32, ctx.clip_distances, clip_num);
    }
    case VsOutput::CullDist0:
    case VsOutput::CullDist1:
    case VsOutput::CullDist2:
    case VsOutput::CullDist3:
    case VsOutput::CullDist4:
    case VsOutput::CullDist5:
    case VsOutput::CullDist6:
    case VsOutput::CullDist7: {
        const u32 index = u32(output) - u32(VsOutput::CullDist0);
        const Id cull_num{ctx.ConstU32(index)};
        ASSERT_MSG(Sirit::ValidId(ctx.cull_distances), "Cull distance used but not defined");
        return ctx.OpAccessChain(ctx.output_f32, ctx.cull_distances, cull_num);
    }
    default:
        UNREACHABLE();
    }
}

Id OutputAttrPointer(EmitContext& ctx, IR::Attribute attr, u32 element) {
    if (IR::IsParam(attr)) {
        const u32 index{u32(attr) - u32(IR::Attribute::Param0)};
        const auto& info{ctx.output_params.at(index)};
        if (info.num_components == 1) {
            return info.id;
        } else {
            return ctx.OpAccessChain(ctx.output_f32, info.id, ctx.ConstU32(element));
        }
    }
    switch (attr) {
    case IR::Attribute::Position0: {
        return ctx.OpAccessChain(ctx.output_f32, ctx.output_position, ctx.ConstU32(element));
    case IR::Attribute::Position1:
    case IR::Attribute::Position2:
    case IR::Attribute::Position3: {
        const u32 index = u32(attr) - u32(IR::Attribute::Position1);
        return VsOutputAttrPointer(ctx, ctx.info.vs_outputs[index][element]);
    }
    case IR::Attribute::RenderTarget0:
    case IR::Attribute::RenderTarget1:
    case IR::Attribute::RenderTarget2:
    case IR::Attribute::RenderTarget3:
    case IR::Attribute::RenderTarget4:
    case IR::Attribute::RenderTarget5:
    case IR::Attribute::RenderTarget6:
    case IR::Attribute::RenderTarget7: {
        const u32 index = u32(attr) - u32(IR::Attribute::RenderTarget0);
        if (ctx.frag_num_comp[index] > 1) {
            return ctx.OpAccessChain(ctx.output_f32, ctx.frag_color[index], ctx.ConstU32(element));
        } else {
            return ctx.frag_color[index];
        }
    }
    case IR::Attribute::Depth:
        return ctx.frag_depth;
    default:
        throw NotImplementedException("Read attribute {}", attr);
    }
    }
}
} // Anonymous namespace

Id EmitGetUserData(EmitContext& ctx, IR::ScalarReg reg) {
    return ctx.ConstU32(ctx.info.user_data[static_cast<size_t>(reg)]);
}

void EmitGetThreadBitScalarReg(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetThreadBitScalarReg(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitGetScalarRegister(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetScalarRegister(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitGetVectorRegister(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetVectorRegister(EmitContext& ctx) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitSetGotoVariable(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

void EmitGetGotoVariable(EmitContext&) {
    UNREACHABLE_MSG("Unreachable instruction");
}

Id EmitReadConst(EmitContext& ctx) {
    return ctx.u32_zero_value;
    UNREACHABLE_MSG("Unreachable instruction");
}

Id EmitReadConstBuffer(EmitContext& ctx, u32 handle, Id index) {
    auto& buffer = ctx.buffers[handle];
    if (!Sirit::ValidId(buffer.offset)) {
        buffer.offset = ctx.GetBufferOffset(handle);
    }
    const Id offset_dwords{ctx.OpShiftRightLogical(ctx.U32[1], buffer.offset, ctx.ConstU32(2U))};
    index = ctx.OpIAdd(ctx.U32[1], index, offset_dwords);
    const Id ptr{ctx.OpAccessChain(buffer.pointer_type, buffer.id, ctx.u32_zero_value, index)};
    return ctx.OpLoad(buffer.data_types->Get(1), ptr);
}

Id EmitReadConstBufferU32(EmitContext& ctx, u32 handle, Id index) {
    return ctx.OpBitcast(ctx.U32[1], EmitReadConstBuffer(ctx, handle, index));
}

Id EmitReadStepRate(EmitContext& ctx, int rate_idx) {
    return ctx.OpLoad(
        ctx.U32[1], ctx.OpAccessChain(ctx.TypePointer(spv::StorageClass::PushConstant, ctx.U32[1]),
                                      ctx.push_data_block,
                                      rate_idx == 0 ? ctx.u32_zero_value : ctx.u32_one_value));
}

Id EmitGetAttribute(EmitContext& ctx, IR::Attribute attr, u32 comp) {
    if (IR::IsParam(attr)) {
        const u32 index{u32(attr) - u32(IR::Attribute::Param0)};
        const auto& param{ctx.input_params.at(index)};
        if (param.buffer_handle < 0) {
            if (!ValidId(param.id)) {
                // Attribute is disabled or varying component is not written
                return ctx.ConstF32(comp == 3 ? 1.0f : 0.0f);
            }
            if (param.is_default) {
                return ctx.OpCompositeExtract(param.component_type, param.id, comp);
            }

            if (param.num_components > 1) {
                const Id pointer{
                    ctx.OpAccessChain(param.pointer_type, param.id, ctx.ConstU32(comp))};
                return ctx.OpLoad(param.component_type, pointer);
            } else {
                return ctx.OpLoad(param.component_type, param.id);
            }
        } else {
            const auto step_rate = EmitReadStepRate(ctx, param.id.value);
            const auto offset = ctx.OpIAdd(
                ctx.U32[1],
                ctx.OpIMul(
                    ctx.U32[1],
                    ctx.OpUDiv(ctx.U32[1], ctx.OpLoad(ctx.U32[1], ctx.instance_id), step_rate),
                    ctx.ConstU32(param.num_components)),
                ctx.ConstU32(comp));
            return EmitReadConstBuffer(ctx, param.buffer_handle, offset);
        }
    }
    switch (attr) {
    case IR::Attribute::FragCoord: {
        const Id coord = ctx.OpLoad(
            ctx.F32[1], ctx.OpAccessChain(ctx.input_f32, ctx.frag_coord, ctx.ConstU32(comp)));
        if (comp == 3) {
            return ctx.OpFDiv(ctx.F32[1], ctx.ConstF32(1.f), coord);
        }
        return coord;
    }
    default:
        throw NotImplementedException("Read attribute {}", attr);
    }
}

Id EmitGetAttributeU32(EmitContext& ctx, IR::Attribute attr, u32 comp) {
    switch (attr) {
    case IR::Attribute::VertexId:
        return ctx.OpLoad(ctx.U32[1], ctx.vertex_index);
    case IR::Attribute::InstanceId:
        return ctx.OpLoad(ctx.U32[1], ctx.instance_id);
    case IR::Attribute::InstanceId0:
        return EmitReadStepRate(ctx, 0);
    case IR::Attribute::InstanceId1:
        return EmitReadStepRate(ctx, 1);
    case IR::Attribute::WorkgroupId:
        return ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.workgroup_id), comp);
    case IR::Attribute::LocalInvocationId:
        return ctx.OpCompositeExtract(ctx.U32[1], ctx.OpLoad(ctx.U32[3], ctx.local_invocation_id),
                                      comp);
    case IR::Attribute::IsFrontFace:
        return ctx.OpSelect(ctx.U32[1], ctx.OpLoad(ctx.U1[1], ctx.front_facing), ctx.u32_one_value,
                            ctx.u32_zero_value);
    default:
        throw NotImplementedException("Read U32 attribute {}", attr);
    }
}

void EmitSetAttribute(EmitContext& ctx, IR::Attribute attr, Id value, u32 element) {
    const Id pointer{OutputAttrPointer(ctx, attr, element)};
    ctx.OpStore(pointer, ctx.OpBitcast(ctx.F32[1], value));
}

Id EmitLoadBufferU32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferF32(ctx, inst, handle, address);
}

template <u32 N>
static Id EmitLoadBufferF32xN(EmitContext& ctx, u32 handle, Id address) {
    auto& buffer = ctx.buffers[handle];
    if (!Sirit::ValidId(buffer.offset)) {
        buffer.offset = ctx.GetBufferOffset(handle);
    }
    address = ctx.OpIAdd(ctx.U32[1], address, buffer.offset);
    const Id index = ctx.OpShiftRightLogical(ctx.U32[1], address, ctx.ConstU32(2u));
    if constexpr (N == 1) {
        const Id ptr{ctx.OpAccessChain(buffer.pointer_type, buffer.id, ctx.u32_zero_value, index)};
        return ctx.OpLoad(buffer.data_types->Get(1), ptr);
    } else {
        boost::container::static_vector<Id, N> ids;
        for (u32 i = 0; i < N; i++) {
            const Id index_i = ctx.OpIAdd(ctx.U32[1], index, ctx.ConstU32(i));
            const Id ptr{
                ctx.OpAccessChain(buffer.pointer_type, buffer.id, ctx.u32_zero_value, index_i)};
            ids.push_back(ctx.OpLoad(buffer.data_types->Get(1), ptr));
        }
        return ctx.OpCompositeConstruct(buffer.data_types->Get(N), ids);
    }
}

Id EmitLoadBufferF32(EmitContext& ctx, IR::Inst*, u32 handle, Id address) {
    return EmitLoadBufferF32xN<1>(ctx, handle, address);
}

Id EmitLoadBufferF32x2(EmitContext& ctx, IR::Inst*, u32 handle, Id address) {
    return EmitLoadBufferF32xN<2>(ctx, handle, address);
}

Id EmitLoadBufferF32x3(EmitContext& ctx, IR::Inst*, u32 handle, Id address) {
    return EmitLoadBufferF32xN<3>(ctx, handle, address);
}

Id EmitLoadBufferF32x4(EmitContext& ctx, IR::Inst*, u32 handle, Id address) {
    return EmitLoadBufferF32xN<4>(ctx, handle, address);
}

static bool IsSignedInteger(AmdGpu::NumberFormat format) {
    switch (format) {
    case AmdGpu::NumberFormat::Unorm:
    case AmdGpu::NumberFormat::Uscaled:
    case AmdGpu::NumberFormat::Uint:
        return false;
    case AmdGpu::NumberFormat::Snorm:
    case AmdGpu::NumberFormat::Sscaled:
    case AmdGpu::NumberFormat::Sint:
    case AmdGpu::NumberFormat::SnormNz:
        return true;
    case AmdGpu::NumberFormat::Float:
    default:
        UNREACHABLE();
    }
}

static u32 UXBitsMax(u32 bit_width) {
    return (1u << bit_width) - 1u;
}

static u32 SXBitsMax(u32 bit_width) {
    return (1u << (bit_width - 1u)) - 1u;
}

static Id ConvertValue(EmitContext& ctx, Id value, AmdGpu::NumberFormat format, u32 bit_width) {
    switch (format) {
    case AmdGpu::NumberFormat::Unorm:
        return ctx.OpFDiv(ctx.F32[1], value, ctx.ConstF32(float(UXBitsMax(bit_width))));
    case AmdGpu::NumberFormat::Snorm:
        return ctx.OpFDiv(ctx.F32[1], value, ctx.ConstF32(float(SXBitsMax(bit_width))));
    case AmdGpu::NumberFormat::SnormNz:
        // (x * 2 + 1) / (Format::SMAX * 2)
        value = ctx.OpFMul(ctx.F32[1], value, ctx.ConstF32(2.f));
        value = ctx.OpFAdd(ctx.F32[1], value, ctx.ConstF32(1.f));
        return ctx.OpFDiv(ctx.F32[1], value, ctx.ConstF32(float(SXBitsMax(bit_width) * 2)));
    case AmdGpu::NumberFormat::Uscaled:
    case AmdGpu::NumberFormat::Sscaled:
    case AmdGpu::NumberFormat::Uint:
    case AmdGpu::NumberFormat::Sint:
    case AmdGpu::NumberFormat::Float:
        return value;
    default:
        UNREACHABLE_MSG("Unsupported number fromat for conversion: {}",
                        magic_enum::enum_name(format));
    }
}

static Id ComponentOffset(EmitContext& ctx, Id address, u32 stride, u32 bit_offset) {
    Id comp_offset = ctx.ConstU32(bit_offset);
    if (stride < 4) {
        // comp_offset += (address % 4) * 8;
        const Id byte_offset = ctx.OpUMod(ctx.U32[1], address, ctx.ConstU32(4u));
        const Id bit_offset = ctx.OpShiftLeftLogical(ctx.U32[1], byte_offset, ctx.ConstU32(3u));
        comp_offset = ctx.OpIAdd(ctx.U32[1], comp_offset, bit_offset);
    }
    return comp_offset;
}

static Id GetBufferFormatValue(EmitContext& ctx, u32 handle, Id address, u32 comp) {
    auto& buffer = ctx.buffers[handle];
    const auto format = buffer.buffer.GetDataFmt();
    switch (format) {
    case AmdGpu::DataFormat::FormatInvalid:
        return ctx.f32_zero_value;
    case AmdGpu::DataFormat::Format8:
    case AmdGpu::DataFormat::Format16:
    case AmdGpu::DataFormat::Format32:
    case AmdGpu::DataFormat::Format8_8:
    case AmdGpu::DataFormat::Format16_16:
    case AmdGpu::DataFormat::Format10_11_11:
    case AmdGpu::DataFormat::Format11_11_10:
    case AmdGpu::DataFormat::Format10_10_10_2:
    case AmdGpu::DataFormat::Format2_10_10_10:
    case AmdGpu::DataFormat::Format8_8_8_8:
    case AmdGpu::DataFormat::Format32_32:
    case AmdGpu::DataFormat::Format16_16_16_16:
    case AmdGpu::DataFormat::Format32_32_32:
    case AmdGpu::DataFormat::Format32_32_32_32: {
        const u32 num_components = AmdGpu::NumComponents(format);
        if (comp >= num_components) {
            return ctx.f32_zero_value;
        }

        // uint index = address / 4;
        Id index = ctx.OpShiftRightLogical(ctx.U32[1], address, ctx.ConstU32(2u));
        const u32 stride = buffer.buffer.GetStride();
        if (stride > 4) {
            const u32 index_offset = u32(AmdGpu::ComponentOffset(format, comp) / 32);
            if (index_offset > 0) {
                // index += index_offset;
                index = ctx.OpIAdd(ctx.U32[1], index, ctx.ConstU32(index_offset));
            }
        }
        const Id ptr = ctx.OpAccessChain(buffer.pointer_type, buffer.id, ctx.u32_zero_value, index);

        const u32 bit_offset = AmdGpu::ComponentOffset(format, comp) % 32;
        const u32 bit_width = AmdGpu::ComponentBits(format, comp);
        const auto num_format = buffer.buffer.GetNumberFmt();
        if (num_format == AmdGpu::NumberFormat::Float) {
            if (bit_width == 32) {
                return ctx.OpLoad(ctx.F32[1], ptr);
            } else if (bit_width == 16) {
                const Id comp_offset = ComponentOffset(ctx, address, stride, bit_offset);
                Id value = ctx.OpLoad(ctx.U32[1], ptr);
                value =
                    ctx.OpBitFieldSExtract(ctx.S32[1], value, comp_offset, ctx.ConstU32(bit_width));
                value = ctx.OpSConvert(ctx.U16, value);
                value = ctx.OpBitcast(ctx.F16[1], value);
                return ctx.OpFConvert(ctx.F32[1], value);
            } else {
                UNREACHABLE_MSG("Invalid float bit width {}", bit_width);
            }
        } else {
            Id value = ctx.OpLoad(ctx.U32[1], ptr);
            const bool is_signed = IsSignedInteger(num_format);
            if (bit_width < 32) {
                const Id comp_offset = ComponentOffset(ctx, address, stride, bit_offset);
                if (is_signed) {
                    value = ctx.OpBitFieldSExtract(ctx.S32[1], value, comp_offset,
                                                   ctx.ConstU32(bit_width));
                    value = ctx.OpConvertSToF(ctx.F32[1], value);
                } else {
                    value = ctx.OpBitFieldUExtract(ctx.U32[1], value, comp_offset,
                                                   ctx.ConstU32(bit_width));
                    value = ctx.OpConvertUToF(ctx.F32[1], value);
                }
            } else {
                if (is_signed) {
                    value = ctx.OpConvertSToF(ctx.F32[1], value);
                } else {
                    value = ctx.OpConvertUToF(ctx.F32[1], value);
                }
            }
            return ConvertValue(ctx, value, num_format, bit_width);
        }
        break;
    }
    default:
        UNREACHABLE_MSG("Invalid format for conversion: {}", magic_enum::enum_name(format));
    }
}

template <u32 N>
static Id EmitLoadBufferFormatF32xN(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    auto& buffer = ctx.buffers[handle];
    if (!Sirit::ValidId(buffer.offset)) {
        buffer.offset = ctx.GetBufferOffset(handle);
    }
    address = ctx.OpIAdd(ctx.U32[1], address, buffer.offset);
    if constexpr (N == 1) {
        return GetBufferFormatValue(ctx, handle, address, 0);
    } else {
        boost::container::static_vector<Id, N> ids;
        for (u32 i = 0; i < N; i++) {
            ids.push_back(GetBufferFormatValue(ctx, handle, address, i));
        }
        return ctx.OpCompositeConstruct(ctx.F32[N], ids);
    }
}

Id EmitLoadBufferFormatF32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferFormatF32xN<1>(ctx, inst, handle, address);
}

Id EmitLoadBufferFormatF32x2(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferFormatF32xN<2>(ctx, inst, handle, address);
}

Id EmitLoadBufferFormatF32x3(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferFormatF32xN<3>(ctx, inst, handle, address);
}

Id EmitLoadBufferFormatF32x4(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address) {
    return EmitLoadBufferFormatF32xN<4>(ctx, inst, handle, address);
}

template <u32 N>
static void EmitStoreBufferF32xN(EmitContext& ctx, u32 handle, Id address, Id value) {
    auto& buffer = ctx.buffers[handle];
    if (!Sirit::ValidId(buffer.offset)) {
        buffer.offset = ctx.GetBufferOffset(handle);
    }
    address = ctx.OpIAdd(ctx.U32[1], address, buffer.offset);
    const Id index = ctx.OpShiftRightLogical(ctx.U32[1], address, ctx.ConstU32(2u));
    if constexpr (N == 1) {
        const Id ptr{ctx.OpAccessChain(buffer.pointer_type, buffer.id, ctx.u32_zero_value, index)};
        ctx.OpStore(ptr, value);
    } else {
        for (u32 i = 0; i < N; i++) {
            const Id index_i = ctx.OpIAdd(ctx.U32[1], index, ctx.ConstU32(i));
            const Id ptr =
                ctx.OpAccessChain(buffer.pointer_type, buffer.id, ctx.u32_zero_value, index_i);
            ctx.OpStore(ptr, ctx.OpCompositeExtract(ctx.F32[1], value, i));
        }
    }
}

void EmitStoreBufferF32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferF32xN<1>(ctx, handle, address, value);
}

void EmitStoreBufferF32x2(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferF32xN<2>(ctx, handle, address, value);
}

void EmitStoreBufferF32x3(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferF32xN<3>(ctx, handle, address, value);
}

void EmitStoreBufferF32x4(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferF32xN<4>(ctx, handle, address, value);
}

void EmitStoreBufferU32(EmitContext& ctx, IR::Inst* inst, u32 handle, Id address, Id value) {
    EmitStoreBufferF32xN<1>(ctx, handle, address, value);
}

} // namespace Shader::Backend::SPIRV
