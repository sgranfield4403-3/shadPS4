// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <utility>
#include <boost/container/small_vector.hpp>
#include <boost/container/static_vector.hpp>

#include "common/assert.h"
#include "common/io_file.h"
#include "common/scope_exit.h"
#include "shader_recompiler/backend/spirv/emit_spirv_quad_rect.h"
#include "shader_recompiler/frontend/fetch_shader.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/amdgpu/resource.h"
#include "video_core/buffer_cache/buffer_cache.h"
#include "video_core/renderer_vulkan/vk_graphics_pipeline.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

namespace Vulkan {

using Shader::Backend::SPIRV::AuxShaderType;

GraphicsPipeline::GraphicsPipeline(
    const Instance& instance_, Scheduler& scheduler_, DescriptorHeap& desc_heap_,
    const GraphicsPipelineKey& key_, vk::PipelineCache pipeline_cache,
    std::span<const Shader::Info*, MaxShaderStages> infos,
    std::span<const Shader::RuntimeInfo, MaxShaderStages> runtime_infos,
    std::optional<const Shader::Gcn::FetchShaderData> fetch_shader_,
    std::span<const vk::ShaderModule> modules)
    : Pipeline{instance_, scheduler_, desc_heap_, pipeline_cache}, key{key_},
      fetch_shader{std::move(fetch_shader_)} {
    const vk::Device device = instance.GetDevice();
    std::ranges::copy(infos, stages.begin());
    BuildDescSetLayout();

    const vk::PushConstantRange push_constants = {
        .stageFlags = gp_stage_flags,
        .offset = 0,
        .size = sizeof(Shader::PushData),
    };

    const vk::DescriptorSetLayout set_layout = *desc_layout;
    const vk::PipelineLayoutCreateInfo layout_info = {
        .setLayoutCount = 1U,
        .pSetLayouts = &set_layout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push_constants,
    };
    auto [layout_result, layout] = instance.GetDevice().createPipelineLayoutUnique(layout_info);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create graphics pipeline layout: {}", vk::to_string(layout_result));
    pipeline_layout = std::move(layout);

    boost::container::static_vector<vk::VertexInputBindingDescription, 32> vertex_bindings;
    boost::container::static_vector<vk::VertexInputAttributeDescription, 32> vertex_attributes;
    if (fetch_shader && !instance.IsVertexInputDynamicState()) {
        const auto& vs_info = GetStage(Shader::LogicalStage::Vertex);
        for (const auto& attrib : fetch_shader->attributes) {
            if (attrib.UsesStepRates()) {
                // Skip attribute binding as the data will be pulled by shader
                continue;
            }

            const auto buffer = attrib.GetSharp(vs_info);
            if (buffer.GetSize() == 0) {
                continue;
            }
            vertex_attributes.push_back({
                .location = attrib.semantic,
                .binding = attrib.semantic,
                .format = LiverpoolToVK::SurfaceFormat(buffer.GetDataFmt(), buffer.GetNumberFmt()),
                .offset = 0,
            });
            vertex_bindings.push_back({
                .binding = attrib.semantic,
                .stride = buffer.GetStride(),
                .inputRate =
                    attrib.GetStepRate() == Shader::Gcn::VertexAttribute::InstanceIdType::None
                        ? vk::VertexInputRate::eVertex
                        : vk::VertexInputRate::eInstance,
            });
        }
    }

    const vk::PipelineVertexInputStateCreateInfo vertex_input_info = {
        .vertexBindingDescriptionCount = static_cast<u32>(vertex_bindings.size()),
        .pVertexBindingDescriptions = vertex_bindings.data(),
        .vertexAttributeDescriptionCount = static_cast<u32>(vertex_attributes.size()),
        .pVertexAttributeDescriptions = vertex_attributes.data(),
    };

    auto prim_restart = key.enable_primitive_restart != 0;
    if (prim_restart && IsPrimitiveListTopology() && !instance.IsListRestartSupported()) {
        LOG_WARNING(Render_Vulkan,
                    "Primitive restart is enabled for list topology but not supported by driver.");
        prim_restart = false;
    }
    const vk::PipelineInputAssemblyStateCreateInfo input_assembly = {
        .topology = LiverpoolToVK::PrimitiveType(key.prim_type),
        .primitiveRestartEnable = prim_restart,
    };
    ASSERT_MSG(!prim_restart || key.primitive_restart_index == 0xFFFF ||
                   key.primitive_restart_index == 0xFFFFFFFF,
               "Primitive restart index other than -1 is not supported yet");
    const bool is_rect_list = key.prim_type == AmdGpu::PrimitiveType::RectList;
    const bool is_quad_list = key.prim_type == AmdGpu::PrimitiveType::QuadList;
    const auto& fs_info = runtime_infos[u32(Shader::LogicalStage::Fragment)].fs_info;
    const vk::PipelineTessellationStateCreateInfo tessellation_state = {
        .patchControlPoints = is_rect_list ? 3U : (is_quad_list ? 4U : key.patch_control_points),
    };

    const vk::PipelineRasterizationStateCreateInfo raster_state = {
        .depthClampEnable = false,
        .rasterizerDiscardEnable = false,
        .polygonMode = LiverpoolToVK::PolygonMode(key.polygon_mode),
        .cullMode = LiverpoolToVK::IsPrimitiveCulled(key.prim_type)
                        ? LiverpoolToVK::CullMode(key.cull_mode)
                        : vk::CullModeFlagBits::eNone,
        .frontFace = key.front_face == Liverpool::FrontFace::Clockwise
                         ? vk::FrontFace::eClockwise
                         : vk::FrontFace::eCounterClockwise,
        .depthBiasEnable = key.depth_bias_enable,
        .lineWidth = 1.0f,
    };

    const vk::PipelineMultisampleStateCreateInfo multisampling = {
        .rasterizationSamples =
            LiverpoolToVK::NumSamples(key.num_samples, instance.GetFramebufferSampleCounts()),
        .sampleShadingEnable = false,
    };

    const vk::Viewport viewport = {
        .x = 0.0f,
        .y = 0.0f,
        .width = 1.0f,
        .height = 1.0f,
        .minDepth = 0.0f,
        .maxDepth = 1.0f,
    };

    const vk::Rect2D scissor = {
        .offset = {0, 0},
        .extent = {1, 1},
    };

    const vk::PipelineViewportDepthClipControlCreateInfoEXT clip_control = {
        .negativeOneToOne = key.clip_space == Liverpool::ClipSpace::MinusWToW,
    };

    const vk::PipelineViewportStateCreateInfo viewport_info = {
        .pNext = instance.IsDepthClipControlSupported() ? &clip_control : nullptr,
        .viewportCount = 1,
        .pViewports = &viewport,
        .scissorCount = 1,
        .pScissors = &scissor,
    };

    boost::container::static_vector<vk::DynamicState, 14> dynamic_states = {
        vk::DynamicState::eViewport,           vk::DynamicState::eScissor,
        vk::DynamicState::eBlendConstants,     vk::DynamicState::eDepthBounds,
        vk::DynamicState::eDepthBias,          vk::DynamicState::eStencilReference,
        vk::DynamicState::eStencilCompareMask, vk::DynamicState::eStencilWriteMask,
        vk::DynamicState::eStencilOpEXT,
    };

    if (instance.IsColorWriteEnableSupported()) {
        dynamic_states.push_back(vk::DynamicState::eColorWriteEnableEXT);
        dynamic_states.push_back(vk::DynamicState::eColorWriteMaskEXT);
    }
    if (instance.IsVertexInputDynamicState()) {
        dynamic_states.push_back(vk::DynamicState::eVertexInputEXT);
    } else {
        dynamic_states.push_back(vk::DynamicState::eVertexInputBindingStrideEXT);
    }

    const vk::PipelineDynamicStateCreateInfo dynamic_info = {
        .dynamicStateCount = static_cast<u32>(dynamic_states.size()),
        .pDynamicStates = dynamic_states.data(),
    };

    const vk::PipelineDepthStencilStateCreateInfo depth_info = {
        .depthTestEnable = key.depth_test_enable,
        .depthWriteEnable = key.depth_write_enable,
        .depthCompareOp = key.depth_compare_op,
        .depthBoundsTestEnable = key.depth_bounds_test_enable,
        .stencilTestEnable = key.stencil_test_enable,
    };

    boost::container::static_vector<vk::PipelineShaderStageCreateInfo, MaxShaderStages>
        shader_stages;
    auto stage = u32(Shader::LogicalStage::Vertex);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = modules[stage],
            .pName = "main",
        });
    }
    stage = u32(Shader::LogicalStage::Geometry);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eGeometry,
            .module = modules[stage],
            .pName = "main",
        });
    }
    stage = u32(Shader::LogicalStage::TessellationControl);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationControl,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (is_rect_list || is_quad_list) {
        const auto type = is_quad_list ? AuxShaderType::QuadListTCS : AuxShaderType::RectListTCS;
        auto tcs = Shader::Backend::SPIRV::EmitAuxilaryTessShader(type, fs_info);
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationControl,
            .module = CompileSPV(tcs, instance.GetDevice()),
            .pName = "main",
        });
    }
    stage = u32(Shader::LogicalStage::TessellationEval);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
            .module = modules[stage],
            .pName = "main",
        });
    } else if (is_rect_list || is_quad_list) {
        auto tes =
            Shader::Backend::SPIRV::EmitAuxilaryTessShader(AuxShaderType::PassthroughTES, fs_info);
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eTessellationEvaluation,
            .module = CompileSPV(tes, instance.GetDevice()),
            .pName = "main",
        });
    }
    stage = u32(Shader::LogicalStage::Fragment);
    if (infos[stage]) {
        shader_stages.emplace_back(vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = modules[stage],
            .pName = "main",
        });
    }

    const vk::PipelineRenderingCreateInfoKHR pipeline_rendering_ci = {
        .colorAttachmentCount = key.num_color_attachments,
        .pColorAttachmentFormats = key.color_formats.data(),
        .depthAttachmentFormat = key.depth_format,
        .stencilAttachmentFormat = key.stencil_format,
    };

    std::array<vk::PipelineColorBlendAttachmentState, Liverpool::NumColorBuffers> attachments;
    for (u32 i = 0; i < key.num_color_attachments; i++) {
        const auto& control = key.blend_controls[i];
        const auto src_color = LiverpoolToVK::BlendFactor(control.color_src_factor);
        const auto dst_color = LiverpoolToVK::BlendFactor(control.color_dst_factor);
        const auto color_blend = LiverpoolToVK::BlendOp(control.color_func);
        attachments[i] = vk::PipelineColorBlendAttachmentState{
            .blendEnable = control.enable,
            .srcColorBlendFactor = src_color,
            .dstColorBlendFactor = dst_color,
            .colorBlendOp = color_blend,
            .srcAlphaBlendFactor = control.separate_alpha_blend
                                       ? LiverpoolToVK::BlendFactor(control.alpha_src_factor)
                                       : src_color,
            .dstAlphaBlendFactor = control.separate_alpha_blend
                                       ? LiverpoolToVK::BlendFactor(control.alpha_dst_factor)
                                       : dst_color,
            .alphaBlendOp = control.separate_alpha_blend
                                ? LiverpoolToVK::BlendOp(control.alpha_func)
                                : color_blend,
            .colorWriteMask =
                instance.IsColorWriteEnableSupported()
                    ? vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                          vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA
                    : key.write_masks[i],
        };

        // On GCN GPU there is an additional mask which allows to control color components exported
        // from a pixel shader. A situation possible, when the game may mask out the alpha channel,
        // while it is still need to be used in blending ops. For such cases, HW will default alpha
        // to 1 and perform the blending, while shader normally outputs 0 in the last component.
        // Unfortunatelly, Vulkan doesn't provide any control on blend inputs, so below we detecting
        // such cases and override alpha value in order to emulate HW behaviour.
        const auto has_alpha_masked_out =
            (key.cb_shader_mask.GetMask(i) & Liverpool::ColorBufferMask::ComponentA) == 0;
        const auto has_src_alpha_in_src_blend = src_color == vk::BlendFactor::eSrcAlpha ||
                                                src_color == vk::BlendFactor::eOneMinusSrcAlpha;
        const auto has_src_alpha_in_dst_blend = dst_color == vk::BlendFactor::eSrcAlpha ||
                                                dst_color == vk::BlendFactor::eOneMinusSrcAlpha;
        if (has_alpha_masked_out && has_src_alpha_in_src_blend) {
            attachments[i].srcColorBlendFactor = src_color == vk::BlendFactor::eSrcAlpha
                                                     ? vk::BlendFactor::eOne
                                                     : vk::BlendFactor::eZero; // 1-A
        }
        if (has_alpha_masked_out && has_src_alpha_in_dst_blend) {
            attachments[i].dstColorBlendFactor = dst_color == vk::BlendFactor::eSrcAlpha
                                                     ? vk::BlendFactor::eOne
                                                     : vk::BlendFactor::eZero; // 1-A
        }
    }

    const vk::PipelineColorBlendStateCreateInfo color_blending = {
        .logicOpEnable = false,
        .logicOp = vk::LogicOp::eCopy,
        .attachmentCount = key.num_color_attachments,
        .pAttachments = attachments.data(),
        .blendConstants = std::array{1.0f, 1.0f, 1.0f, 1.0f},
    };

    const vk::GraphicsPipelineCreateInfo pipeline_info = {
        .pNext = &pipeline_rendering_ci,
        .stageCount = static_cast<u32>(shader_stages.size()),
        .pStages = shader_stages.data(),
        .pVertexInputState = !instance.IsVertexInputDynamicState() ? &vertex_input_info : nullptr,
        .pInputAssemblyState = &input_assembly,
        .pTessellationState = &tessellation_state,
        .pViewportState = &viewport_info,
        .pRasterizationState = &raster_state,
        .pMultisampleState = &multisampling,
        .pDepthStencilState = &depth_info,
        .pColorBlendState = &color_blending,
        .pDynamicState = &dynamic_info,
        .layout = *pipeline_layout,
    };

    auto [pipeline_result, pipe] =
        device.createGraphicsPipelineUnique(pipeline_cache, pipeline_info);
    ASSERT_MSG(pipeline_result == vk::Result::eSuccess, "Failed to create graphics pipeline: {}",
               vk::to_string(pipeline_result));
    pipeline = std::move(pipe);
}

GraphicsPipeline::~GraphicsPipeline() = default;

void GraphicsPipeline::BuildDescSetLayout() {
    boost::container::small_vector<vk::DescriptorSetLayoutBinding, 32> bindings;
    u32 binding{};

    for (const auto* stage : stages) {
        if (!stage) {
            continue;
        }
        if (stage->has_readconst) {
            bindings.push_back({
                .binding = binding++,
                .descriptorType = vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = gp_stage_flags,
            });
        }
        for (const auto& buffer : stage->buffers) {
            const auto sharp = buffer.GetSharp(*stage);
            bindings.push_back({
                .binding = binding++,
                .descriptorType = buffer.IsStorage(sharp) ? vk::DescriptorType::eStorageBuffer
                                                          : vk::DescriptorType::eUniformBuffer,
                .descriptorCount = 1,
                .stageFlags = gp_stage_flags,
            });
        }
        for (const auto& tex_buffer : stage->texture_buffers) {
            bindings.push_back({
                .binding = binding++,
                .descriptorType = tex_buffer.is_written ? vk::DescriptorType::eStorageTexelBuffer
                                                        : vk::DescriptorType::eUniformTexelBuffer,
                .descriptorCount = 1,
                .stageFlags = gp_stage_flags,
            });
        }
        for (const auto& image : stage->images) {
            bindings.push_back({
                .binding = binding++,
                .descriptorType = image.is_storage ? vk::DescriptorType::eStorageImage
                                                   : vk::DescriptorType::eSampledImage,
                .descriptorCount = 1,
                .stageFlags = gp_stage_flags,
            });
        }
        for (const auto& sampler : stage->samplers) {
            bindings.push_back({
                .binding = binding++,
                .descriptorType = vk::DescriptorType::eSampler,
                .descriptorCount = 1,
                .stageFlags = gp_stage_flags,
            });
        }
    }
    uses_push_descriptors = binding < instance.MaxPushDescriptors();
    const auto flags = uses_push_descriptors
                           ? vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptorKHR
                           : vk::DescriptorSetLayoutCreateFlagBits{};
    const vk::DescriptorSetLayoutCreateInfo desc_layout_ci = {
        .flags = flags,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    };
    auto [layout_result, layout] =
        instance.GetDevice().createDescriptorSetLayoutUnique(desc_layout_ci);
    ASSERT_MSG(layout_result == vk::Result::eSuccess,
               "Failed to create graphics descriptor set layout: {}", vk::to_string(layout_result));
    desc_layout = std::move(layout);
}

} // namespace Vulkan
