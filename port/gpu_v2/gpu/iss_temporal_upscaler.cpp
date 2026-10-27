// SPDX-License-Identifier: GPL-2.0-or-later
// Temporal upscaling of the inFAMOUS image, renderer v2. See iss_temporal.h.
#include "iss_temporal_upscaler.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <string>
#include <vector>

#include "common/assert.h"
#include "common/logging/log.h"
#include "iss_temporal_math.h"
#include "tlg_settings.h"
#include "video_core/host_shaders/iss_camera_motion_comp.h"
#include "video_core/renderer_vulkan/vk_instance.h"
#include "video_core/renderer_vulkan/vk_platform.h"
#include "video_core/renderer_vulkan/vk_runtime.h"
#include "video_core/renderer_vulkan/vk_scheduler.h"
#include "video_core/renderer_vulkan/vk_shader_util.h"
#include "video_core/texture_cache/texture_cache.h"

#include "ffx_vk_portable.h"
#include "src/iss_temporal.h"

namespace Vulkan::IssTemporal {

namespace {

std::atomic<bool> g_available{false};

FfxVkPortableImage Describe(const BackendImage& image, FfxVkPortableResourceState state) {
    FfxVkPortableImage out{};
    out.structSize = sizeof(out);
    out.image = image.image;
    out.format = static_cast<VkFormat>(image.format);
    out.extent = {image.width, image.height};
    out.mipCount = 1;
    out.arrayLayers = 1;
    out.usage = static_cast<VkImageUsageFlags>(image.usage);
    out.aspect = static_cast<VkImageAspectFlags>(image.aspect);
    out.state = state;
    return out;
}

/// Push constants of iss_camera_motion.comp.
struct MotionPush {
    Mat4 reproject;
    float size[2];
    float jitter[2];
};
static_assert(sizeof(MotionPush) == 80);

/// The one convention nobody has checked in the running game is the sign of the hook's jitter.
/// TLG_ISS_TEMPORAL_FLIP=x, y or xy flips those components (for the motion pass and for the backend),
/// to try the other sign without a rebuild.
void ApplyJitterFlip(float jitter[2]) {
    static const char* flip = std::getenv("TLG_ISS_TEMPORAL_FLIP");
    for (const char* c = flip; c && *c; ++c) {
        if (*c == 'x') jitter[0] = -jitter[0];
        if (*c == 'y') jitter[1] = -jitter[1];
    }
}

std::string IssueNames(u64 issues) {
    std::string names;
    for (u32 bit = 0; bit < 64; ++bit) {
        if (issues & (1ull << bit)) {
            names += ' ';
            names += ffxVkPortableValidationIssueName(1ull << bit);
        }
    }
    return names;
}

/// FSR 3.1 through the portable Vulkan implementation.
class Fsr3Backend final : public TemporalBackend {
public:
    explicit Fsr3Backend(const Instance& instance_) : instance{instance_} {
        device_info.structSize = sizeof(device_info);
        device_info.instance = instance.GetInstance();
        device_info.physicalDevice = instance.GetPhysicalDevice();
        device_info.device = instance.GetDevice();
        device_info.getDeviceProcAddr = VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr;
        device_info.queue = instance.GetGraphicsQueue();
        device_info.queueFamilyIndex = instance.GetGraphicsQueueFamilyIndex();
        device_info.shaderFloat16Enabled = instance.IsShaderFloat16Supported();
        device_info.subgroupSizeControlEnabled = instance.IsSubgroupSizeControlEnabled();
        device_info.synchronization2Enabled = VK_TRUE;
        device_info.shaderStorageImageWriteWithoutFormatEnabled =
            instance.IsStorageImageWriteWithoutFormatEnabled();
    }

    ~Fsr3Backend() override {
        // The caller waited for the GPU (Upscaler::~Upscaler) before getting here.
        ffxVkPortableUpscaleContextDestroy(context);
    }

    const char* Name() const override {
        return "FSR 3.1";
    }

    bool Prepare(u32 render_width, u32 render_height, u32 output_width, u32 output_height,
                 bool depth_inverted) override {
        ffxVkPortableUpscaleContextDestroy(context);
        context = nullptr;
        create_info = {};
        create_info.structSize = sizeof(create_info);
        // The game's anti-aliasing input is LDR; the depth convention is the game's.
        create_info.flags = depth_inverted ? FFX_VK_PORTABLE_CONTEXT_DEPTH_INVERTED : 0;
        create_info.maxRenderSize = {render_width, render_height};
        create_info.maxOutputSize = {output_width, output_height};
        if (const u64 issues = ffxVkPortableValidateUpscaleCreateInfo(&create_info)) {
            LOG_WARNING(Render_Vulkan, "IssTemporal: FSR 3 create info invalid:{}", IssueNames(issues));
            return false;
        }
        if (ffxVkPortableUpscaleContextCreate(&device_info, &create_info, &context) != FFX_VK_PORTABLE_OK) {
            LOG_WARNING(Render_Vulkan, "IssTemporal: FSR 3 context creation failed");
            context = nullptr;
            return false;
        }
        return true;
    }

    bool Record(vk::CommandBuffer cmdbuf, const DispatchParams& params) override {
        if (!context) {
            return false;
        }
        FfxVkPortableUpscaleDispatchInfo info{};
        info.structSize = sizeof(info);
        info.commandBuffer = cmdbuf;
        info.color = Describe(params.color, FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
        info.depth = Describe(params.depth, FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
        info.motionVectors = Describe(params.motion, FFX_VK_PORTABLE_RESOURCE_STATE_GENERIC_READ);
        info.output = Describe(params.output, FFX_VK_PORTABLE_RESOURCE_STATE_UNORDERED_ACCESS);
        // Optional inputs this backend does not use yet: a described, null image.
        info.exposure.structSize = sizeof(info.exposure);
        info.reactiveMask.structSize = sizeof(info.reactiveMask);
        info.transparencyAndCompositionMask.structSize = sizeof(info.transparencyAndCompositionMask);
        info.jitterOffset = {params.jitter[0], params.jitter[1]};
        info.motionVectorScale = {1.0f, 1.0f};
        info.renderSize = {params.color.width, params.color.height};
        info.outputSize = {params.output.width, params.output.height};
        info.frameTimeMilliseconds = params.frame_ms;
        info.preExposure = 1.0f;
        info.cameraNear = params.near_z;
        info.cameraFar = params.far_z;
        info.cameraVerticalFovRadians = params.fov_y;
        info.viewSpaceToMeters = 1.0f;
        info.sharpness = params.sharpness;
        info.enableSharpening = params.sharpen ? VK_TRUE : VK_FALSE;
        info.reset = params.reset ? VK_TRUE : VK_FALSE;
        info.frameId = params.frame_id;
        if (const u64 issues = ffxVkPortableValidateUpscaleDispatchInfo(&create_info, &info)) {
            LOG_WARNING(Render_Vulkan, "IssTemporal: FSR 3 dispatch invalid:{}", IssueNames(issues));
            return false;
        }
        if (ffxVkPortableUpscaleContextRecordDispatch(context, &info) != FFX_VK_PORTABLE_OK) {
            LOG_WARNING(Render_Vulkan, "IssTemporal: FSR 3 dispatch failed");
            return false;
        }
        return true;
    }

private:
    const Instance& instance;
    FfxVkPortableDeviceInfo device_info{};
    FfxVkPortableUpscaleCreateInfo create_info{};
    FfxVkPortableUpscaleContext* context = nullptr;
};

} // namespace

std::unique_ptr<TemporalBackend> CreateFsr3Backend(const Instance& instance) {
    return std::make_unique<Fsr3Backend>(instance);
}

bool Available() {
    return g_available.load(std::memory_order_relaxed);
}

void Probe(const Instance& instance) {
    FfxVkPortableDeviceCapabilities caps{};
    caps.structSize = sizeof(caps);
    const bool queried = ffxVkPortableQueryDeviceCapabilities(instance.GetPhysicalDevice(), &caps) ==
                         FFX_VK_PORTABLE_OK;
    const bool ok = queried && caps.fsr3ComputePrerequisites && caps.requiredFsr3StorageFormats &&
                    instance.IsStorageImageWriteWithoutFormatEnabled();
    g_available.store(ok, std::memory_order_relaxed);
    LOG_INFO(Render_Vulkan, "IssTemporal: FSR 3.1 {}", ok ? "available" : "unavailable on this device");
    // The game side keeps its own anti-aliasing until the renderer says it can take over.
    iss_temporal_set_renderer_ready(ok && TlgSettings::Get().iss_temporal ? 1 : 0);
}

// -------------------------------------------------------------------------------------------------


struct Upscaler::Impl {
    Impl(const Instance& instance_, Scheduler& scheduler_, Runtime& runtime_,
         VideoCore::TextureCache& texture_cache_)
        : instance{instance_}, scheduler{scheduler_}, runtime{runtime_}, texture_cache{texture_cache_} {}

    ~Impl() {
        // Nothing recorded may still use what is destroyed below and by the members after it.
        scheduler.Finish();
        if (depth_view) {
            instance.GetDevice().destroyImageView(depth_view);
        }
    }

    bool CreateMotionPipeline();
    bool EnsureTargets(u32 render_width, u32 render_height, u32 output_width, u32 output_height);
    vk::ImageView DepthView(const VideoCore::Image& depth);
    void Resolve(const Frame& frame, const DepthTargets& depth_targets);
    void RecordMotion(vk::CommandBuffer cmdbuf, vk::ImageView depth, const Mat4& reproject,
                      const float jitter[2]);
    void Skip(const char* why);
    void Fail(const char* why);

    const Instance& instance;
    Scheduler& scheduler;
    Runtime& runtime;
    VideoCore::TextureCache& texture_cache;

    std::unique_ptr<TemporalBackend> backend;
    /// The backend or a pipeline could not be made: the game keeps its own anti-aliasing from now on.
    bool failed = false;
    bool context_ready = false;
    bool context_inverted = false;

    // The camera motion pass.
    vk::UniqueDescriptorSetLayout motion_set_layout;
    vk::UniquePipelineLayout motion_pipeline_layout;
    vk::UniquePipeline motion_pipeline;

    // Our images: motion vectors (render size) and the upscaler's output (output size).
    VideoCore::UniqueImage motion_image;
    VideoCore::UniqueImage output_image;
    vk::UniqueImageView motion_view;
    u32 render_width = 0, render_height = 0, output_width = 0, output_height = 0;
    bool targets_ready = false;

    // The depth view in use. The depth image changes with the render targets, so it is keyed by image.
    vk::ImageView depth_view{};
    vk::Image depth_view_image{};
    u64 depth_view_uid = 0;
    vk::Format depth_view_format{};

    // The previous frame.
    Mat4 previous_view_proj{};
    u32 previous_frame_id = 0;
    bool have_previous = false;
    std::chrono::steady_clock::time_point last_dispatch{};

    // Reasons already logged (literals), so a persistent problem does not fill the log.
    std::vector<const char*> logged;
    u64 resolved = 0;
};

void Upscaler::Impl::Skip(const char* why) {
    if (std::find(logged.begin(), logged.end(), why) == logged.end()) {
        logged.push_back(why);
        LOG_WARNING(Render_Vulkan, "IssTemporal: frame left to the game: {}", why);
    }
}

void Upscaler::Impl::Fail(const char* why) {
    failed = true;
    g_available.store(false, std::memory_order_relaxed);
    iss_temporal_set_renderer_ready(0);
    LOG_WARNING(Render_Vulkan, "IssTemporal: off for this run ({}); the game keeps its own anti-aliasing", why);
}

bool Upscaler::Impl::CreateMotionPipeline() {
    const auto device = instance.GetDevice();
    const std::array<vk::DescriptorSetLayoutBinding, 2> bindings{{
        {.binding = 0,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eCompute},
        {.binding = 1,
         .descriptorType = vk::DescriptorType::eStorageImage,
         .descriptorCount = 1,
         .stageFlags = vk::ShaderStageFlagBits::eCompute},
    }};
    motion_set_layout = Check<"create iss temporal descriptor set layout">(device.createDescriptorSetLayoutUnique({
        .flags = vk::DescriptorSetLayoutCreateFlagBits::ePushDescriptor,
        .bindingCount = static_cast<u32>(bindings.size()),
        .pBindings = bindings.data(),
    }));
    const vk::PushConstantRange push{
        .stageFlags = vk::ShaderStageFlagBits::eCompute, .offset = 0, .size = sizeof(MotionPush)};
    motion_pipeline_layout = Check<"create iss temporal pipeline layout">(device.createPipelineLayoutUnique({
        .setLayoutCount = 1,
        .pSetLayouts = &motion_set_layout.get(),
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &push,
    }));
    const vk::ShaderModule module = CompileSPV(ISS_CAMERA_MOTION_COMP, device);
    if (!module) {
        return false;
    }
    SetObjectName(device, module, "iss_camera_motion.comp");
    auto pipeline = device.createComputePipelineUnique(
        {}, vk::ComputePipelineCreateInfo{
                .stage = {.stage = vk::ShaderStageFlagBits::eCompute, .module = module, .pName = "main"},
                .layout = motion_pipeline_layout.get(),
            });
    device.destroyShaderModule(module);
    if (pipeline.result != vk::Result::eSuccess) {
        LOG_WARNING(Render_Vulkan, "IssTemporal: camera motion pipeline failed: {}",
                    vk::to_string(pipeline.result));
        return false;
    }
    motion_pipeline = std::move(pipeline.value);
    SetObjectName(device, motion_pipeline.get(), "iss temporal camera motion");
    return true;
}

bool Upscaler::Impl::EnsureTargets(u32 render_w, u32 render_h, u32 output_w, u32 output_h) {
    if (targets_ready && render_w == render_width && render_h == render_height && output_w == output_width &&
        output_h == output_height) {
        return true;
    }
    // Images of the old size may still be in flight.
    scheduler.Finish();
    targets_ready = false;
    motion_view.reset();
    const auto device = instance.GetDevice();
    const auto allocator = instance.GetAllocator();
    motion_image = VideoCore::UniqueImage(device, allocator);
    motion_image.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16Sfloat,
        .extent = {render_w, render_h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    output_image = VideoCore::UniqueImage(device, allocator);
    output_image.Create(vk::ImageCreateInfo{
        .imageType = vk::ImageType::e2D,
        .format = vk::Format::eR16G16B16A16Sfloat,
        .extent = {output_w, output_h, 1},
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = vk::SampleCountFlagBits::e1,
        .tiling = vk::ImageTiling::eOptimal,
        .usage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                 vk::ImageUsageFlagBits::eTransferSrc,
        .initialLayout = vk::ImageLayout::eUndefined,
    });
    if (!motion_image || !output_image) {
        return false;
    }
    SetObjectName(device, vk::Image(motion_image), "iss temporal motion vectors");
    SetObjectName(device, vk::Image(output_image), "iss temporal output");
    motion_view = Check<"create iss temporal motion view">(device.createImageViewUnique({
        .image = motion_image,
        .viewType = vk::ImageViewType::e2D,
        .format = vk::Format::eR16G16Sfloat,
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
    }));
    render_width = render_w;
    render_height = render_h;
    output_width = output_w;
    output_height = output_h;
    targets_ready = true;
    return true;
}

vk::ImageView Upscaler::Impl::DepthView(const VideoCore::Image& depth) {
    const vk::Image handle = depth.GetImage();
    const vk::Format format = depth.info.pixel_format;
    if (depth_view && depth_view_image == handle && depth_view_uid == depth.image_uid &&
        depth_view_format == format) {
        return depth_view;
    }
    const auto device = instance.GetDevice();
    if (depth_view) {
        // A recorded pass may still use the old view.
        scheduler.DeferOperation([device, view = depth_view] { device.destroyImageView(view); });
    }
    depth_view = Check<"create iss temporal depth view">(device.createImageView({
        .image = handle,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1},
    }));
    depth_view_image = handle;
    depth_view_uid = depth.image_uid;
    depth_view_format = format;
    return depth_view;
}

void Upscaler::Impl::RecordMotion(vk::CommandBuffer cmdbuf, vk::ImageView depth, const Mat4& reproject,
                                  const float jitter[2]) {
    MotionPush push{};
    push.reproject = reproject;
    push.size[0] = static_cast<float>(render_width);
    push.size[1] = static_cast<float>(render_height);
    push.jitter[0] = jitter[0];
    push.jitter[1] = jitter[1];
    const vk::DescriptorImageInfo depth_info{.imageView = depth, .imageLayout = vk::ImageLayout::eGeneral};
    const vk::DescriptorImageInfo motion_info{.imageView = *motion_view,
                                              .imageLayout = vk::ImageLayout::eGeneral};
    const std::array<vk::WriteDescriptorSet, 2> writes{{
        {.dstBinding = 0,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eSampledImage,
         .pImageInfo = &depth_info},
        {.dstBinding = 1,
         .descriptorCount = 1,
         .descriptorType = vk::DescriptorType::eStorageImage,
         .pImageInfo = &motion_info},
    }};
    cmdbuf.bindPipeline(vk::PipelineBindPoint::eCompute, *motion_pipeline);
    cmdbuf.pushDescriptorSetKHR(vk::PipelineBindPoint::eCompute, *motion_pipeline_layout, 0, writes);
    cmdbuf.pushConstants(*motion_pipeline_layout, vk::ShaderStageFlagBits::eCompute, 0, sizeof(push), &push);
    cmdbuf.dispatch((render_width + 7) / 8, (render_height + 7) / 8, 1);
}

void Upscaler::Impl::Resolve(const Frame& frame, const DepthTargets& depth_targets) {
    if (failed) {
        return;
    }
    // The three images, by the address they start at. The hook may not know the scene depth (address 0).
    const u64 depth_addr = frame.depth_addr ? frame.depth_addr
                                            : depth_targets.Find(frame.render_width, frame.render_height);
    const auto color_id = texture_cache.FindImageFromRange(frame.color_addr, 1, false);
    const auto depth_id = depth_addr ? texture_cache.FindImageFromRange(depth_addr, 1, false) : VideoCore::ImageId{};
    const auto out_id = texture_cache.FindImageFromRange(frame.output_addr, 1, false);
    if (!color_id || !depth_id || !out_id) {
        Skip("an image is not registered (colour, depth or output; no depth target of the render size seen?)");
        return;
    }
    auto& color = texture_cache.GetImage(color_id);
    auto& depth = texture_cache.GetImage(depth_id);
    auto& target = texture_cache.GetImage(out_id);
    const u32 w = frame.render_width, h = frame.render_height;
    const u32 ow = target.info.size.width, oh = target.info.size.height;
    if (color.info.size.width != w || color.info.size.height != h || depth.info.size.width != w ||
        depth.info.size.height != h) {
        Skip("the colour or depth size is not the render size the game announced");
        return;
    }
    if (ow != w || oh != h) {
        // Rendering below the output needs the grain and the UI at output size: a later step.
        Skip("the output is not the render size (an internal resolution below the output is not supported yet)");
        return;
    }
    if (color.info.num_samples != 1 || depth.info.num_samples != 1 || target.info.num_samples != 1) {
        Skip("multisampled image");
        return;
    }
    if (!(color.usage_flags & vk::ImageUsageFlagBits::eSampled) ||
        !(depth.usage_flags & vk::ImageUsageFlagBits::eSampled) ||
        !(target.usage_flags & vk::ImageUsageFlagBits::eTransferDst)) {
        Skip("an image lacks the usage the upscaler needs (sampled colour and depth, transfer destination output)");
        return;
    }
    const auto format_props = instance.GetPhysicalDevice().getFormatProperties(target.info.pixel_format);
    if (!(format_props.optimalTilingFeatures & vk::FormatFeatureFlagBits::eBlitDst)) {
        Skip("the output format cannot be a blit destination");
        return;
    }

    if (!motion_pipeline && !CreateMotionPipeline()) {
        Fail("the camera motion pipeline could not be made");
        return;
    }
    if (!backend) {
        backend = CreateFsr3Backend(instance);
    }
    float jitter[2] = {frame.jitter[0], frame.jitter[1]};
    ApplyJitterFlip(jitter);
    const bool inverted = (frame.flags & kFlagReversedDepth) != 0;
    const bool sizes_changed = !targets_ready || w != render_width || h != render_height || ow != output_width ||
                               oh != output_height;
    if (!EnsureTargets(w, h, ow, oh)) {
        Fail("the motion or output image could not be made");
        return;
    }
    // History starts again at a camera cut, after a gap in the frames, or when the context is new.
    bool reset = (frame.flags & kFlagCameraCut) != 0 || !have_previous ||
                 frame.frame_id != previous_frame_id + 1;
    if (!context_ready || sizes_changed || inverted != context_inverted) {
        scheduler.Finish();
        context_ready = backend->Prepare(w, h, ow, oh, inverted);
        context_inverted = inverted;
        if (!context_ready) {
            Fail("the upscaler context could not be made");
            return;
        }
        LOG_INFO(Render_Vulkan, "IssTemporal: {} at {}x{} ({} depth)", backend->Name(), w, h,
                 inverted ? "reversed" : "normal");
        reset = true;
    }
    Mat4 reproject;
    if (!have_previous || !Reproject(frame.view_proj, previous_view_proj, reproject)) {
        // No usable previous camera: no motion, and a new history.
        if (!Reproject(frame.view_proj, frame.view_proj, reproject)) {
            Skip("the camera matrix cannot be inverted");
            return;
        }
        reset = true;
    }

    const auto now = std::chrono::steady_clock::now();
    float frame_ms = std::chrono::duration<float, std::milli>(now - last_dispatch).count();
    if (!(frame_ms > 0.0f) || frame_ms > 200.0f) {
        frame_ms = 16.6f;
    }
    last_dispatch = now;

    // Outside any render pass, the inputs readable from compute.
    scheduler.EndRendering();
    runtime.Transit(&depth, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.Transit(&color, vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
                    vk::AccessFlagBits2::eShaderRead);
    runtime.FlushBarriers();
    const vk::ImageView depth_view_now = DepthView(depth);
    const auto cmdbuf = scheduler.CommandBuffer();

    const auto all = vk::PipelineStageFlagBits2::eAllCommands;
    const auto rw = vk::AccessFlagBits2::eShaderRead | vk::AccessFlagBits2::eShaderWrite;
    const auto barrier = [&](vk::Image image, vk::ImageLayout old_layout, vk::PipelineStageFlags2 src_stage,
                             vk::AccessFlags2 src_access, vk::ImageLayout new_layout,
                             vk::PipelineStageFlags2 dst_stage, vk::AccessFlags2 dst_access) {
        const vk::ImageMemoryBarrier2 b{
            .srcStageMask = src_stage,
            .srcAccessMask = src_access,
            .dstStageMask = dst_stage,
            .dstAccessMask = dst_access,
            .oldLayout = old_layout,
            .newLayout = new_layout,
            .image = image,
            .subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1},
        };
        cmdbuf.pipelineBarrier2({.imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &b});
    };
    // The contents of both images start again every frame.
    barrier(vk::Image(motion_image), vk::ImageLayout::eUndefined, all, vk::AccessFlagBits2::eNone,
            vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderWrite);
    barrier(vk::Image(output_image), vk::ImageLayout::eUndefined, all, vk::AccessFlagBits2::eNone,
            vk::ImageLayout::eGeneral, all, rw);

    RecordMotion(cmdbuf, depth_view_now, reproject, jitter);
    barrier(vk::Image(motion_image), vk::ImageLayout::eGeneral, vk::PipelineStageFlagBits2::eComputeShader,
            vk::AccessFlagBits2::eShaderWrite, vk::ImageLayout::eGeneral, all,
            vk::AccessFlagBits2::eShaderRead);

    DispatchParams params;
    params.color = {color.GetImage(), color.info.pixel_format, color.usage_flags,
                    vk::ImageAspectFlagBits::eColor, w, h};
    params.depth = {depth.GetImage(), depth.info.pixel_format, depth.usage_flags,
                    vk::ImageAspectFlagBits::eDepth, w, h};
    params.motion = {vk::Image(motion_image), vk::Format::eR16G16Sfloat,
                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
                     vk::ImageAspectFlagBits::eColor, w, h};
    params.output = {vk::Image(output_image), vk::Format::eR16G16B16A16Sfloat,
                     vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled |
                         vk::ImageUsageFlagBits::eTransferSrc,
                     vk::ImageAspectFlagBits::eColor, ow, oh};
    params.jitter[0] = jitter[0];
    params.jitter[1] = jitter[1];
    params.frame_ms = frame_ms;
    params.near_z = frame.near_z;
    params.far_z = frame.far_z;
    params.fov_y = frame.fov_y;
    params.sharpen = TlgSettings::Get().sharpen;
    params.sharpness = std::min(TlgSettings::Get().sharpness.load(), 1.0f);
    params.reset = reset;
    params.depth_inverted = inverted;
    params.frame_id = frame.frame_id;
    if (!backend->Record(cmdbuf, params)) {
        // The dispatch is not in the command buffer: this frame has no anti-aliasing; the next may.
        Skip("the backend refused the frame");
        have_previous = false;
        return;
    }

    // The result goes into the game's anti-aliasing output; the game carries on from there.
    runtime.Transit(&target, vk::ImageLayout::eTransferDstOptimal, vk::PipelineStageFlagBits2::eAllTransfer,
                    vk::AccessFlagBits2::eTransferWrite);
    runtime.FlushBarriers();
    barrier(vk::Image(output_image), vk::ImageLayout::eGeneral, all, rw, vk::ImageLayout::eTransferSrcOptimal,
            vk::PipelineStageFlagBits2::eAllTransfer, vk::AccessFlagBits2::eTransferRead);
    const vk::ImageBlit region{
        .srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .srcOffsets = std::array<vk::Offset3D, 2>{vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<s32>(ow), static_cast<s32>(oh), 1}},
        .dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1},
        .dstOffsets = std::array<vk::Offset3D, 2>{vk::Offset3D{0, 0, 0}, vk::Offset3D{static_cast<s32>(ow), static_cast<s32>(oh), 1}},
    };
    cmdbuf.blitImage(vk::Image(output_image), vk::ImageLayout::eTransferSrcOptimal, target.GetImage(),
                     vk::ImageLayout::eTransferDstOptimal, region, vk::Filter::eNearest);
    target.flags |= VideoCore::ImageFlagBits::GpuModified;
    target.flags &= ~VideoCore::ImageFlagBits::Dirty;

    previous_view_proj = frame.view_proj;
    previous_frame_id = frame.frame_id;
    have_previous = true;
    if (resolved++ == 0) {
        LOG_INFO(Render_Vulkan, "IssTemporal: first frame resolved ({}x{}, {})", w, h, backend->Name());
    }
}

Upscaler::Upscaler(const Instance& instance, Scheduler& scheduler, Runtime& runtime,
                   VideoCore::TextureCache& texture_cache)
    : impl{std::make_unique<Impl>(instance, scheduler, runtime, texture_cache)} {}

Upscaler::~Upscaler() = default;

void Upscaler::OnMarker(const u32* payload, u32 dwords, const DepthTargets& depth_targets) {
    if (!TlgSettings::Get().iss_temporal) {
        return;
    }
    Frame frame;
    if (!ParseMarker(payload, dwords, frame)) {
        impl->Skip("a malformed marker");
        return;
    }
    impl->Resolve(frame, depth_targets);
}

} // namespace Vulkan::IssTemporal
