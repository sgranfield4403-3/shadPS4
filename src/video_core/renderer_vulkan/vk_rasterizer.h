// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "video_core/renderer_vulkan/vk_pipeline_cache.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"

namespace AmdGpu {
struct Liverpool;
}

namespace Core {
class MemoryManager;
}

namespace VideoCore {
class TextureCache;
}

namespace Vulkan {

class Scheduler;
class GraphicsPipeline;

class Rasterizer {
public:
    explicit Rasterizer(const Instance& instance, Scheduler& scheduler,
                        VideoCore::TextureCache& texture_cache, AmdGpu::Liverpool* liverpool);
    ~Rasterizer();

    void Draw(bool is_indexed);

private:
    u32 SetupIndexBuffer(bool& is_indexed);
    void MapMemory(VAddr addr, size_t size);

    void UpdateDynamicState();
    void UpdateViewportScissorState();
    void UpdateDepthStencilState();

private:
    const Instance& instance;
    Scheduler& scheduler;
    VideoCore::TextureCache& texture_cache;
    AmdGpu::Liverpool* liverpool;
    Core::MemoryManager* memory;
    PipelineCache pipeline_cache;
    StreamBuffer vertex_index_buffer;
};

} // namespace Vulkan
