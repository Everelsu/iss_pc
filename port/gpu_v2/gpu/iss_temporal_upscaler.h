// SPDX-License-Identifier: GPL-2.0-or-later
// Temporal upscaling of the inFAMOUS image for renderer v2. Where the game would run its own
// anti-aliasing (SMAA T2x / FXAA) the game hook (src/iss_temporal_hook.h) leaves a PM4 NOP marker in
// the command buffer; when the command processor reaches it, Upscaler::OnMarker takes the game's
// anti-aliasing input (LDR colour), the scene depth and the camera, makes motion vectors from the
// depth, runs the upscaler backend and writes the result into the game's anti-aliasing output. The
// game then goes on with its film grain and UI as if its own anti-aliasing had run.
//
// The backend (TemporalBackend) is the part that differs between upscalers: FSR 3.1 is implemented;
// DLSS (with the DLL the player supplies) is another implementation of the same interface.
//
// Off by default (iss_temporal in tlg.ini / TLG_ISS_TEMPORAL). With it off nothing here runs.
#pragma once

#include <memory>

#include "common/types.h"
#include "iss_temporal_math.h"
#include "video_core/renderer_vulkan/vk_common.h"
#include "video_core/texture_cache/image.h"

namespace VideoCore {
class TextureCache;
}

namespace Vulkan {
class Instance;
class Scheduler;
class Runtime;
} // namespace Vulkan

namespace Vulkan::IssTemporal {

/// An image handed to a backend: the handle and what the backend needs to describe it.
struct BackendImage {
    vk::Image image{};
    vk::Format format{};
    vk::ImageUsageFlags usage{};
    vk::ImageAspectFlags aspect{};
    u32 width = 0;
    u32 height = 0;
};

/// One frame for a backend. The images are in the General layout.
struct DispatchParams {
    BackendImage color;   ///< LDR scene colour, jittered, render size
    BackendImage depth;   ///< scene depth, render size
    BackendImage motion;  ///< RG16F, render pixels, previous minus current, jitter free
    BackendImage output;  ///< RGBA16F, output size (storage)
    float jitter[2] = {}; ///< render pixels, the image shift of this frame (x right, y down)
    float frame_ms = 16.6f;
    float near_z = 0.1f;
    float far_z = 1.0e7f;
    float fov_y = 1.0f;
    float sharpness = 0.0f; ///< 0..1
    bool sharpen = false;
    bool reset = false;          ///< discard the history (first frame, camera cut, size change)
    bool depth_inverted = false; ///< the far plane is 0
    u64 frame_id = 0;
};

/// A temporal upscaling implementation.
class TemporalBackend {
public:
    virtual ~TemporalBackend() = default;
    virtual const char* Name() const = 0;
    /// Makes (or remakes) the context for these sizes. False when it cannot be made; the reason is logged.
    virtual bool Prepare(u32 render_width, u32 render_height, u32 output_width, u32 output_height,
                         bool depth_inverted) = 0;
    /// Records the upscale into `cmdbuf`. False when the backend refuses the frame.
    virtual bool Record(vk::CommandBuffer cmdbuf, const DispatchParams& params) = 0;
};

/// FSR 3.1, the portable native Vulkan implementation (third_party/fsr-vulkan, MIT).
std::unique_ptr<TemporalBackend> CreateFsr3Backend(const Instance& instance);

/// Whether this device can run the temporal upscaler at all. Evaluated once, when the rasterizer is made.
bool Available();
/// Checks the device and tells the game side (iss_temporal_set_renderer_ready) whether markers will be
/// consumed: with the setting on and a capable device only.
void Probe(const Instance& instance);

class Upscaler {
public:
    Upscaler(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
             VideoCore::TextureCache& texture_cache);
    ~Upscaler();

    /// The game's marker reached the command processor (graphics lane, draw pipe drained): `payload`
    /// is the NOP's body, `dwords` its length. Does nothing unless the setting is on. `depth_targets`
    /// finds the scene depth when the marker does not name it.
    void OnMarker(const u32* payload, u32 dwords, const DepthTargets& depth_targets);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan::IssTemporal
