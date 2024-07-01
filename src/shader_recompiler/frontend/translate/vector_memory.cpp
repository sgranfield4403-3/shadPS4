// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/frontend/translate/translate.h"

namespace Shader::Gcn {

void Translator::IMAGE_GET_RESINFO(const GcnInst& inst) {
    IR::VectorReg dst_reg{inst.dst[0].code};
    const IR::ScalarReg tsharp_reg{inst.src[2].code};
    const auto flags = ImageResFlags(inst.control.mimg.dmask);
    const bool has_mips = flags.test(ImageResComponent::MipCount);
    const IR::U32 lod = ir.GetVectorReg(IR::VectorReg(inst.src[0].code));
    const IR::Value tsharp = ir.GetScalarReg(tsharp_reg);
    const IR::Value size = ir.ImageQueryDimension(tsharp, lod, ir.Imm1(has_mips));

    if (flags.test(ImageResComponent::Width)) {
        ir.SetVectorReg(dst_reg++, IR::U32{ir.CompositeExtract(size, 0)});
    }
    if (flags.test(ImageResComponent::Height)) {
        ir.SetVectorReg(dst_reg++, IR::U32{ir.CompositeExtract(size, 1)});
    }
    if (flags.test(ImageResComponent::Depth)) {
        ir.SetVectorReg(dst_reg++, IR::U32{ir.CompositeExtract(size, 2)});
    }
    if (has_mips) {
        ir.SetVectorReg(dst_reg++, IR::U32{ir.CompositeExtract(size, 3)});
    }
}

void Translator::IMAGE_SAMPLE(const GcnInst& inst) {
    const auto& mimg = inst.control.mimg;
    if (mimg.da) {
        LOG_WARNING(Render_Vulkan, "Image instruction declares an array");
    }

    IR::VectorReg addr_reg{inst.src[0].code};
    IR::VectorReg dest_reg{inst.dst[0].code};
    const IR::ScalarReg tsharp_reg{inst.src[2].code * 4};
    const IR::ScalarReg sampler_reg{inst.src[3].code * 4};
    const auto flags = MimgModifierFlags(mimg.mod);

    // Load first dword of T# and S#. We will use them as the handle that will guide resource
    // tracking pass where to read the sharps. This will later also get patched to the SPIRV texture
    // binding index.
    const IR::Value handle =
        ir.CompositeConstruct(ir.GetScalarReg(tsharp_reg), ir.GetScalarReg(sampler_reg));

    // Load first address components as denoted in 8.2.4 VGPR Usage Sea Islands Series Instruction
    // Set Architecture
    const IR::Value offset =
        flags.test(MimgModifier::Offset) ? ir.GetVectorReg(addr_reg++) : IR::Value{};
    const IR::F32 bias =
        flags.test(MimgModifier::LodBias) ? ir.GetVectorReg<IR::F32>(addr_reg++) : IR::F32{};
    const IR::F32 dref =
        flags.test(MimgModifier::Pcf) ? ir.GetVectorReg<IR::F32>(addr_reg++) : IR::F32{};

    // Derivatives are tricky because their number depends on the texture type which is located in
    // T#. We don't have access to T# though until resource tracking pass. For now assume no
    // derivatives are present, otherwise we don't know where coordinates are placed in the address
    // stream.
    ASSERT_MSG(!flags.test(MimgModifier::Derivative), "Derivative image instruction");

    // Now we can load body components as noted in Table 8.9 Image Opcodes with Sampler
    // Since these are at most 4 dwords, we load them into a single uvec4 and place them
    // in coords field of the instruction. Then the resource tracking pass will patch the
    // IR instruction to fill in lod_clamp field.
    const IR::Value body = ir.CompositeConstruct(
        ir.GetVectorReg<IR::F32>(addr_reg), ir.GetVectorReg<IR::F32>(addr_reg + 1),
        ir.GetVectorReg<IR::F32>(addr_reg + 2), ir.GetVectorReg<IR::F32>(addr_reg + 3));

    const bool explicit_lod = flags.any(MimgModifier::Level0, MimgModifier::Lod);

    IR::TextureInstInfo info{};
    info.is_depth.Assign(flags.test(MimgModifier::Pcf));
    info.has_bias.Assign(flags.test(MimgModifier::LodBias));
    info.has_lod_clamp.Assign(flags.test(MimgModifier::LodClamp));
    info.force_level0.Assign(flags.test(MimgModifier::Level0));
    info.explicit_lod.Assign(explicit_lod);

    // Issue IR instruction, leaving unknown fields blank to patch later.
    const IR::Value texel = [&]() -> IR::Value {
        const IR::F32 lod = flags.test(MimgModifier::Level0) ? ir.Imm32(0.f) : IR::F32{};
        if (!flags.test(MimgModifier::Pcf)) {
            if (explicit_lod) {
                return ir.ImageSampleExplicitLod(handle, body, lod, offset, info);
            } else {
                return ir.ImageSampleImplicitLod(handle, body, bias, offset, {}, info);
            }
        }
        if (explicit_lod) {
            return ir.ImageSampleDrefExplicitLod(handle, body, dref, lod, offset, info);
        }
        return ir.ImageSampleDrefImplicitLod(handle, body, dref, bias, offset, {}, info);
    }();

    for (u32 i = 0; i < 4; i++) {
        if (((mimg.dmask >> i) & 1) == 0) {
            continue;
        }
        IR::F32 value;
        if (flags.test(MimgModifier::Pcf)) {
            value = i < 3 ? IR::F32{texel} : ir.Imm32(1.0f);
        } else {
            value = IR::F32{ir.CompositeExtract(texel, i)};
        }
        ir.SetVectorReg(dest_reg++, value);
    }
}

void Translator::IMAGE_LOAD(bool has_mip, const GcnInst& inst) {
    const auto& mimg = inst.control.mimg;
    IR::VectorReg addr_reg{inst.src[0].code};
    IR::VectorReg dest_reg{inst.dst[0].code};
    const IR::ScalarReg tsharp_reg{inst.src[2].code * 4};

    const IR::Value handle = ir.GetScalarReg(tsharp_reg);
    const IR::Value body =
        ir.CompositeConstruct(ir.GetVectorReg(addr_reg), ir.GetVectorReg(addr_reg + 1),
                              ir.GetVectorReg(addr_reg + 2), ir.GetVectorReg(addr_reg + 3));

    IR::TextureInstInfo info{};
    info.explicit_lod.Assign(has_mip);
    const IR::Value texel = ir.ImageFetch(handle, body, {}, {}, {}, info);

    for (u32 i = 0; i < 4; i++) {
        if (((mimg.dmask >> i) & 1) == 0) {
            continue;
        }
        IR::F32 value = IR::F32{ir.CompositeExtract(texel, i)};
        ir.SetVectorReg(dest_reg++, value);
    }
}

void Translator::IMAGE_STORE(const GcnInst& inst) {
    const auto& mimg = inst.control.mimg;
    IR::VectorReg addr_reg{inst.src[0].code};
    IR::VectorReg data_reg{inst.dst[0].code};
    const IR::ScalarReg tsharp_reg{inst.src[2].code * 4};

    const IR::Value handle = ir.GetScalarReg(tsharp_reg);
    const IR::Value body =
        ir.CompositeConstruct(ir.GetVectorReg(addr_reg), ir.GetVectorReg(addr_reg + 1),
                              ir.GetVectorReg(addr_reg + 2), ir.GetVectorReg(addr_reg + 3));

    ASSERT(mimg.dmask == 0xF);
    const IR::Value value = ir.CompositeConstruct(
        ir.GetVectorReg<IR::F32>(data_reg), ir.GetVectorReg<IR::F32>(data_reg + 1),
        ir.GetVectorReg<IR::F32>(data_reg + 2), ir.GetVectorReg<IR::F32>(data_reg + 3));
    ir.ImageWrite(handle, body, value, {});
}

void Translator::BUFFER_LOAD_FORMAT(u32 num_dwords, bool is_typed, const GcnInst& inst) {
    const auto& mtbuf = inst.control.mtbuf;
    const IR::VectorReg vaddr{inst.src[0].code};
    const IR::ScalarReg sharp{inst.src[2].code * 4};
    const IR::Value address = [&] -> IR::Value {
        if (mtbuf.idxen && mtbuf.offen) {
            return ir.CompositeConstruct(ir.GetVectorReg(vaddr), ir.GetVectorReg(vaddr + 1));
        }
        if (mtbuf.idxen || mtbuf.offen) {
            return ir.GetVectorReg(vaddr);
        }
        return {};
    }();
    const IR::Value soffset{GetSrc(inst.src[3])};
    ASSERT_MSG(soffset.IsImmediate() && soffset.U32() == 0, "Non immediate offset not supported");

    IR::BufferInstInfo info{};
    info.index_enable.Assign(mtbuf.idxen);
    info.offset_enable.Assign(mtbuf.offen);
    info.inst_offset.Assign(mtbuf.offset);
    info.is_typed.Assign(is_typed);
    if (is_typed) {
        info.dmft.Assign(static_cast<AmdGpu::DataFormat>(mtbuf.dfmt));
        info.nfmt.Assign(static_cast<AmdGpu::NumberFormat>(mtbuf.nfmt));
    }

    const IR::Value value = ir.LoadBuffer(num_dwords, ir.GetScalarReg(sharp), address, info);
    const IR::VectorReg dst_reg{inst.src[1].code};
    if (num_dwords == 1) {
        ir.SetVectorReg(dst_reg, IR::F32{value});
        return;
    }
    for (u32 i = 0; i < num_dwords; i++) {
        ir.SetVectorReg(dst_reg + i, IR::F32{ir.CompositeExtract(value, i)});
    }
}

void Translator::BUFFER_STORE_FORMAT(u32 num_dwords, bool is_typed, const GcnInst& inst) {
    const auto& mtbuf = inst.control.mtbuf;
    const IR::VectorReg vaddr{inst.src[0].code};
    const IR::ScalarReg sharp{inst.src[2].code * 4};
    const IR::Value address = [&] -> IR::Value {
        if (mtbuf.idxen && mtbuf.offen) {
            return ir.CompositeConstruct(ir.GetVectorReg(vaddr), ir.GetVectorReg(vaddr + 1));
        }
        if (mtbuf.idxen || mtbuf.offen) {
            return ir.GetVectorReg(vaddr);
        }
        return {};
    }();
    const IR::Value soffset{GetSrc(inst.src[3])};
    ASSERT_MSG(soffset.IsImmediate() && soffset.U32() == 0, "Non immediate offset not supported");

    IR::BufferInstInfo info{};
    info.index_enable.Assign(mtbuf.idxen);
    info.offset_enable.Assign(mtbuf.offen);
    info.inst_offset.Assign(mtbuf.offset);
    info.is_typed.Assign(is_typed);
    if (is_typed) {
        info.dmft.Assign(static_cast<AmdGpu::DataFormat>(mtbuf.dfmt));
        info.nfmt.Assign(static_cast<AmdGpu::NumberFormat>(mtbuf.nfmt));
    }

    IR::Value value{};
    const IR::VectorReg src_reg{inst.src[1].code};
    switch (num_dwords) {
    case 1:
        value = ir.GetVectorReg<Shader::IR::F32>(src_reg);
        break;
    case 2:
        value = ir.CompositeConstruct(ir.GetVectorReg<Shader::IR::F32>(src_reg),
                                      ir.GetVectorReg<Shader::IR::F32>(src_reg + 1));
        break;
    case 3:
        value = ir.CompositeConstruct(ir.GetVectorReg<Shader::IR::F32>(src_reg),
                                      ir.GetVectorReg<Shader::IR::F32>(src_reg + 1),
                                      ir.GetVectorReg<Shader::IR::F32>(src_reg + 2));
        break;
    case 4:
        value = ir.CompositeConstruct(ir.GetVectorReg<Shader::IR::F32>(src_reg),
                                      ir.GetVectorReg<Shader::IR::F32>(src_reg + 1),
                                      ir.GetVectorReg<Shader::IR::F32>(src_reg + 2),
                                      ir.GetVectorReg<Shader::IR::F32>(src_reg + 3));
        break;
    }
    ir.StoreBuffer(num_dwords, ir.GetScalarReg(sharp), address, value, info);
}

} // namespace Shader::Gcn
