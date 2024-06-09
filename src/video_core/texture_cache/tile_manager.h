// SPDX-FileCopyrightText: Copyright 2024 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#pragma once

#include "common/types.h"
#include "video_core/renderer_vulkan/vk_stream_buffer.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore {

class TextureCache;

/// Converts tiled texture data to linear format.
void ConvertTileToLinear(u8* dst, const u8* src, u32 width, u32 height, bool neo);

/// Converts image format to the one used internally by detiler.
vk::Format DemoteImageFormatForDetiling(vk::Format format);

enum DetilerType : u32 {
    Micro8x1,
    Micro32x1,
    Micro32x4,

    Max
};

struct DetilerContext {
    vk::UniquePipeline pl;
    vk::UniquePipelineLayout pl_layout;
};

class TileManager {
public:
    TileManager(const Vulkan::Instance& instance, Vulkan::Scheduler& scheduler);
    ~TileManager();

    bool TryDetile(Image& image);

private:
    const DetilerContext* GetDetiler(const Image& image) const;

private:
    const Vulkan::Instance& instance;
    Vulkan::Scheduler& scheduler;
    Vulkan::StreamBuffer staging;
    std::array<DetilerContext, DetilerType::Max> detilers;
};

} // namespace VideoCore
