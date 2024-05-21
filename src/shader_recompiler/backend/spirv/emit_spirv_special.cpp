// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "shader_recompiler/backend/spirv/emit_spirv_instructions.h"
#include "shader_recompiler/backend/spirv/spirv_emit_context.h"

namespace Shader::Backend::SPIRV {

void EmitPrologue(EmitContext& ctx) {}

void EmitEpilogue(EmitContext& ctx) {}

void EmitEmitVertex(EmitContext& ctx, const IR::Value& stream) {
    throw NotImplementedException("Geometry streams");
}

void EmitEndPrimitive(EmitContext& ctx, const IR::Value& stream) {
    throw NotImplementedException("Geometry streams");
}

} // namespace Shader::Backend::SPIRV
