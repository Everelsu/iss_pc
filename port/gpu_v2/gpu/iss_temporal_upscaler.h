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
#include "video_core/texture_cache/image_view.h"

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

/// An image the draws after the marker are redirected to (see Upscaler::RedirectColor).
struct UiTarget {
    vk::ImageView view{};
    vk::ImageLayout layout = vk::ImageLayout::eGeneral;
    u32 width = 0;
    u32 height = 0;
};

/// The output-size image of a frame, for the presenter (DisplayOverride).
struct DisplayImage {
    std::shared_ptr<void> keep; ///< keeps the image alive: hold it until the presenter's commands are done
    u32 width = 0;
    u32 height = 0;
};

/// The presenter asks, with the address of the guest buffer it is about to show: the output-size image the
/// frame was drawn in instead, if the draws of this frame were redirected. Ends the redirection (the frame
/// is over); call it for every flip. The image itself stays in the General layout for the draws.
bool DisplayOverride(VAddr guest_address, DisplayImage& display);
/// Records, into the presenter's command buffer, a copy of that image into one the presenter's passes can
/// sample (ShaderReadOnly), and returns a view of it in the format and with the mapping asked for. Null when
/// it cannot be made: the presenter then shows the guest's buffer.
vk::ImageView RecordDisplayCopy(vk::CommandBuffer cmd, const DisplayImage& display, vk::Format view_format,
                                const vk::ComponentMapping& mapping);
/// Draws are being redirected right now.
bool Redirecting();

/// What a draw's target is to the redirection.
enum class UiFit {
    Other, ///< not the render size: the redirection leaves it alone
    Yes,   ///< the render size and it has a usable output-size twin
    No,    ///< the render size but it cannot be redirected
};

class Upscaler {
    friend bool DisplayOverride(VAddr, DisplayImage&);
    friend vk::ImageView RecordDisplayCopy(vk::CommandBuffer, const DisplayImage&, vk::Format,
                                           const vk::ComponentMapping&);
    friend bool Redirecting();

public:
    Upscaler(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
             VideoCore::TextureCache& texture_cache);
    ~Upscaler();

    /// The game's marker reached the command processor (graphics lane, draw pipe drained): `payload`
    /// is the NOP's body, `dwords` its length. Does nothing unless the setting is on. `depth_targets`
    /// finds the scene depth when the marker does not name it.
    void OnMarker(const u32* payload, u32 dwords, const DepthTargets& depth_targets);

    // ---- Rendering below the output size ---------------------------------------------------------
    // When the game renders at a size smaller than the output, the marker leaves the upscaled image in an
    // output-size twin of the anti-aliasing target. Until the flip every draw into a target of the render
    // size goes to an output-size twin of that target, with its viewport, scissor and screen-space
    // coordinates scaled by UiScale(); a texture that has a twin is read through it. The grain and the HUD
    // so draw at the output's resolution. All of these are called from the thread recording draws.

    /// Draws are being redirected (after a marker that upscaled to a larger size, before the flip).
    bool UiActive() const;
    /// Output size over render size (both axes the same within rounding): 1 when not redirecting.
    float UiScaleX() const;
    float UiScaleY() const;
    /// What the draw's targets are to the redirection. If any target of the render size is `No`, or a draw
    /// mixes `Yes` and `Other` targets, the draw cannot be redirected: AbortRedirect, which leaves the rest of
    /// the frame to the game's own images and the presenter's FSR 1.
    UiFit ClassifyColor(VideoCore::ImageId image, const VideoCore::ImageViewInfo& view_info);
    UiFit ClassifyDepth(VideoCore::ImageId image);
    void AbortRedirect(const char* why);
    /// A colour target about to be drawn into. False: draw into the guest's image as always.
    bool RedirectColor(VideoCore::ImageId image, const VideoCore::ImageViewInfo& view_info, UiTarget& target);
    /// The depth target of a redirected draw. False when the colour was not redirected or sizes differ.
    bool RedirectDepth(VideoCore::ImageId image, UiTarget& target);
    /// A texture about to be sampled: the twin's view when the image has one written this frame.
    bool RedirectSampled(VideoCore::ImageId image, const VideoCore::ImageViewInfo& view_info, vk::ImageView& view,
                         vk::ImageLayout& layout);
    /// Orders the writes of the twins before what the next draw does with them (ends the render pass).
    void UiBarrier();
    /// A draw sampled a twin since the last UiBarrier (its pass must start after the writes of the twin).
    bool UiSampledPending() const;
    /// Extra mip LOD bias for the scene's material textures while the render size is below the output, or 0.
    float SceneMipBias() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

} // namespace Vulkan::IssTemporal
