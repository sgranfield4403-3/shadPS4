// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <xxhash.h>
#include "common/types.h"
#include "shader_recompiler/runtime_info.h"
#include "video_core/renderer_vulkan/liverpool_to_vk.h"
#include "video_core/renderer_vulkan/vk_common.h"

namespace Core {
class MemoryManager;
}

namespace VideoCore {
class TextureCache;
}

namespace Vulkan {

static constexpr u32 MaxVertexBufferCount = 32;
static constexpr u32 MaxShaderStages = 5;

class Instance;
class Scheduler;
class StreamBuffer;

using Liverpool = AmdGpu::Liverpool;

struct GraphicsPipelineKey {
    std::array<size_t, MaxShaderStages> stage_hashes;
    std::array<vk::Format, Liverpool::NumColorBuffers> color_formats;
    vk::Format depth_format;

    Liverpool::DepthControl depth;
    Liverpool::StencilControl stencil;
    Liverpool::StencilRefMask stencil_ref_front;
    Liverpool::StencilRefMask stencil_ref_back;
    Liverpool::PrimitiveType prim_type;
    Liverpool::PolygonMode polygon_mode;
    Liverpool::CullMode cull_mode;
    Liverpool::FrontFace front_face;
    u32 pad{};
    std::array<Liverpool::BlendControl, Liverpool::NumColorBuffers> blend_controls;
    std::array<vk::ColorComponentFlags, Liverpool::NumColorBuffers> write_masks;

    bool operator==(const GraphicsPipelineKey& key) const noexcept {
        return std::memcmp(this, &key, sizeof(key)) == 0;
    }
};
static_assert(std::has_unique_object_representations_v<GraphicsPipelineKey>);

class GraphicsPipeline {
public:
    explicit GraphicsPipeline(const Instance& instance, Scheduler& scheduler,
                              const GraphicsPipelineKey& key, vk::PipelineCache pipeline_cache,
                              std::span<const Shader::Info*, MaxShaderStages> infos,
                              std::array<vk::ShaderModule, MaxShaderStages> modules);
    ~GraphicsPipeline();

    void BindResources(Core::MemoryManager* memory, StreamBuffer& staging,
                       VideoCore::TextureCache& texture_cache) const;

    [[nodiscard]] vk::Pipeline Handle() const noexcept {
        return *pipeline;
    }

    [[nodiscard]] bool IsEmbeddedVs() const noexcept {
        static constexpr size_t EmbeddedVsHash = 0x59c556606a027efd;
        return key.stage_hashes[0] == EmbeddedVsHash;
    }

    [[nodiscard]] auto GetWriteMasks() const {
        return key.write_masks;
    }

private:
    void BuildDescSetLayout();
    void BindVertexBuffers(StreamBuffer& staging) const;

private:
    const Instance& instance;
    Scheduler& scheduler;
    vk::UniquePipeline pipeline;
    vk::UniquePipelineLayout pipeline_layout;
    vk::UniqueDescriptorSetLayout desc_layout;
    std::array<Shader::Info, MaxShaderStages> stages{};
    GraphicsPipelineKey key;
};

} // namespace Vulkan

template <>
struct std::hash<Vulkan::GraphicsPipelineKey> {
    std::size_t operator()(const Vulkan::GraphicsPipelineKey& key) const noexcept {
        return XXH3_64bits(&key, sizeof(key));
    }
};
