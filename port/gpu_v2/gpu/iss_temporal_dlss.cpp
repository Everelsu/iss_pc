// SPDX-License-Identifier: GPL-2.0-or-later
// NVIDIA DLSS backend of the temporal upscaler, through the optional tlg_dlss.dll bridge and the
// nvngx_dlss.dll the player supplies. See iss_temporal_dlss.h for what is loaded from where.
#include "iss_temporal_dlss.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <unordered_map>

#include "common/logging/log.h"
#include "tlg_settings.h"
#include "video_core/renderer_vulkan/vk_instance.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "../../gpu/dlss_bridge/tlg_dlss_bridge.h"
#endif

namespace Vulkan::IssTemporal {

std::unique_ptr<TemporalBackend> CreateSelectedBackend(const Instance& instance) {
    if (Dlss::Selected()) {
        if (auto backend = Dlss::CreateBackend(instance)) {
            return backend;
        }
        LOG_WARNING(Render_Vulkan, "IssTemporal: DLSS is not available ({}); using FSR 3.1",
                    Dlss::Problem().empty() ? "not ready" : Dlss::Problem());
    }
    return CreateFsr3Backend(instance);
}

} // namespace Vulkan::IssTemporal

namespace Vulkan::IssTemporal::Dlss {

bool Selected() {
    const auto& settings = TlgSettings::Get();
    return settings.iss_temporal && settings.iss_temporal_backend == 1;
}

int QualityForScale(float output_over_render) {
    // DLAA only for equal sizes: any real upscale needs at least the Quality mode.
    return output_over_render >= 2.9f    ? 4
           : output_over_render >= 1.95f ? 3
           : output_over_render >= 1.65f ? 2
           : output_over_render > 1.01f  ? 1
                                         : 0;
}

#ifdef _WIN32

namespace {

constexpr wchar_t BridgeName[] = L"tlg_dlss.dll";
constexpr wchar_t NgxName[] = L"nvngx_dlss.dll";
constexpr size_t MaxCachedViews = 64;

struct State {
    std::mutex mutex;
    bool loaded = false;      // the bridge was looked for (once)
    bool ready = false;       // NGX is initialised
    const TlgDlssApi* api = nullptr;
    HMODULE module = nullptr;
    std::string problem;
    std::vector<std::string> instance_extensions, device_extensions;
};

State& GetState() {
    static State state;
    return state;
}

void Fail(State& state, std::string_view reason) {
    if (state.problem.empty()) {
        state.problem = reason;
        LOG_WARNING(Render_Vulkan, "IssTemporal: DLSS unavailable: {}", reason);
    }
}

void BridgeLog(int warning, const char* message) {
    if (warning) {
        LOG_WARNING(Render_Vulkan, "DLSS: {}", message);
    } else {
        LOG_INFO(Render_Vulkan, "DLSS: {}", message);
    }
}

std::filesystem::path ExecutableDirectory() {
    std::wstring path(MAX_PATH, L'\0');
    path.resize(GetModuleFileNameW(nullptr, path.data(), DWORD(path.size())));
    return std::filesystem::path{path}.parent_path();
}

/// The folders searched for the DLLs, best first: the player's choice, <user data>/dlss, the executable.
std::vector<std::filesystem::path> SearchFolders() {
    std::vector<std::filesystem::path> folders;
    if (const std::string& chosen = TlgSettings::Get().dlss_dll_dir; !chosen.empty()) {
        folders.emplace_back(std::filesystem::u8path(chosen));
    }
    if (const char* user = std::getenv("TLG_GPU_USER_DIR"); user && user[0]) {
        folders.emplace_back(std::filesystem::path{user} / "dlss");
    }
    folders.push_back(ExecutableDirectory());
    return folders;
}

std::filesystem::path FindFile(const std::vector<std::filesystem::path>& folders, const wchar_t* name) {
    for (const auto& folder : folders) {
        std::error_code error;
        if (std::filesystem::is_regular_file(folder / name, error)) {
            return folder / name;
        }
    }
    return {};
}

/// "3.10.9.16" from the file's version resource, or empty. version.dll is loaded on demand.
std::string FileVersion(const std::filesystem::path& file) {
    const HMODULE version = LoadLibraryW(L"version.dll");
    if (!version) {
        return {};
    }
    using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using InfoFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using QueryFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    const auto size_fn = reinterpret_cast<SizeFn>(GetProcAddress(version, "GetFileVersionInfoSizeW"));
    const auto info_fn = reinterpret_cast<InfoFn>(GetProcAddress(version, "GetFileVersionInfoW"));
    const auto query_fn = reinterpret_cast<QueryFn>(GetProcAddress(version, "VerQueryValueW"));
    std::string result;
    if (size_fn && info_fn && query_fn) {
        DWORD handle{};
        if (const DWORD size = size_fn(file.c_str(), &handle)) {
            std::vector<char> data(size);
            void* block = nullptr;
            UINT length = 0;
            if (info_fn(file.c_str(), 0, size, data.data()) &&
                query_fn(data.data(), L"\\", &block, &length) && block && length >= sizeof(VS_FIXEDFILEINFO)) {
                const auto* fixed = static_cast<const VS_FIXEDFILEINFO*>(block);
                char text[64];
                std::snprintf(text, sizeof(text), "%u.%u.%u.%u", HIWORD(fixed->dwFileVersionMS),
                              LOWORD(fixed->dwFileVersionMS), HIWORD(fixed->dwFileVersionLS),
                              LOWORD(fixed->dwFileVersionLS));
                result = text;
            }
        }
    }
    FreeLibrary(version);
    return result;
}

/// Looks for the bridge and NGX once and configures the bridge. Under the state mutex.
void Load(State& state) {
    if (state.loaded) {
        return;
    }
    state.loaded = true;
    const auto folders = SearchFolders();
    const auto ngx = FindFile(folders, NgxName);
    const auto bridge = FindFile(folders, BridgeName);
    if (ngx.empty()) {
        return Fail(state, "nvngx_dlss.dll not found (copy NVIDIA's DLL into the DLSS folder)");
    }
    if (bridge.empty()) {
        return Fail(state, "tlg_dlss.dll not found (build it with packaging/windows/build_dlss.sh)");
    }
    const std::string version = FileVersion(ngx);
    LOG_INFO(Render_Vulkan, "IssTemporal: DLSS DLL {} (version {})", ngx.string(),
             version.empty() ? "unknown" : version);
    state.module = LoadLibraryW(bridge.c_str());
    const auto get_api = state.module ? reinterpret_cast<TlgDlssGetApiFn>(GetProcAddress(state.module, "TlgDlssGetApi"))
                                      : nullptr;
    state.api = get_api ? get_api() : nullptr;
    if (!state.api || state.api->abi != TLG_DLSS_BRIDGE_ABI) {
        state.api = nullptr;
        return Fail(state, "tlg_dlss.dll is not a bridge of this version");
    }
    // NGX writes its logs and model updates here.
    std::filesystem::path data = ngx.parent_path() / "ngx";
    if (const char* user = std::getenv("TLG_GPU_USER_DIR"); user && user[0]) {
        data = std::filesystem::path{user} / "dlss";
    }
    std::error_code error;
    std::filesystem::create_directories(data, error);
    if (!state.api->Configure(ngx.parent_path().c_str(), data.c_str(), BridgeLog)) {
        state.api = nullptr;
        return Fail(state, "the bridge could not use that nvngx_dlss.dll");
    }
}

/// Extensions NGX asks for that this Vulkan offers: names copied (the bridge's arrays are short-lived).
bool CollectExtensions(const VkExtensionProperties* required, uint32_t count,
                       const std::vector<vk::ExtensionProperties>& available, std::vector<std::string>& out,
                       std::string& missing) {
    out.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const auto match = std::ranges::find_if(available, [&](const auto& extension) {
            return std::strcmp(extension.extensionName.data(), required[i].extensionName) == 0 &&
                   extension.specVersion >= required[i].specVersion;
        });
        if (match == available.end()) {
            missing = required[i].extensionName;
            return false;
        }
        out.emplace_back(required[i].extensionName);
    }
    return true;
}

class DlssBackend final : public TemporalBackend {
public:
    explicit DlssBackend(const Instance& instance_) : instance{instance_}, api{GetState().api} {}

    ~DlssBackend() override {
        // The caller waited for the GPU (Upscaler::~Upscaler) before getting here.
        if (feature_made) {
            api->ReleaseFeature();
        }
    }

    const char* Name() const override {
        return "DLSS";
    }

    bool Prepare(u32 render_width, u32 render_height, u32 output_width, u32 output_height,
                 bool depth_inverted) override {
        // The caller waited for the GPU before remaking the context.
        if (feature_made) {
            api->ReleaseFeature();
            feature_made = false;
        }
        views.clear();
        feature.input_width = render_width;
        feature.input_height = render_height;
        feature.output_width = output_width;
        feature.output_height = output_height;
        feature.quality = QualityForScale(float(output_width) / float(std::max(render_width, 1u)));
        feature.depth_inverted = depth_inverted ? 1 : 0;
        feature.preset = 0; // the driver's default model
        feature.hdr = 0;    // the game's anti-aliasing input is tonemapped
        LOG_INFO(Render_Vulkan, "IssTemporal: DLSS mode {} for {}x{} to {}x{}", feature.quality, render_width,
                 render_height, output_width, output_height);
        return true;
    }

    bool Record(vk::CommandBuffer cmdbuf, const DispatchParams& params) override {
        if (!feature_made) {
            // The feature is made on the command buffer, on first use after Prepare.
            if (!api->CreateFeature(cmdbuf, &feature)) {
                LOG_WARNING(Render_Vulkan, "IssTemporal: the DLSS feature could not be created");
                return false;
            }
            feature_made = true;
        }
        TlgDlssEvaluate evaluate{};
        if (!Describe(params.color, evaluate.color) || !Describe(params.depth, evaluate.depth) ||
            !Describe(params.motion, evaluate.motion) || !Describe(params.output, evaluate.output)) {
            return false;
        }
        evaluate.jitter_x = params.jitter[0];
        evaluate.jitter_y = params.jitter[1];
        evaluate.reset = params.reset ? 1 : 0;
        evaluate.frame_ms = params.frame_ms;
        evaluate.sharpness = params.sharpen ? params.sharpness : 0.0f;
        return api->Evaluate(cmdbuf, &evaluate) != 0;
    }

private:
    struct ViewKey {
        VkImage image;
        VkFormat format;
        VkImageAspectFlags aspect;
        bool operator==(const ViewKey&) const = default;
    };
    struct ViewKeyHash {
        size_t operator()(const ViewKey& key) const {
            return std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(key.image)) ^
                   (std::hash<uint32_t>{}(uint32_t(key.format)) << 1) ^ (std::hash<uint32_t>{}(key.aspect) << 7);
        }
    };

    /// The bridge wants a view with the image; the views are kept until the next Prepare (the GPU is idle then).
    bool Describe(const BackendImage& image, TlgDlssImage& out) {
        const ViewKey key{static_cast<VkImage>(image.image), static_cast<VkFormat>(image.format),
                          static_cast<VkImageAspectFlags>(image.aspect)};
        auto found = views.find(key);
        if (found == views.end()) {
            if (views.size() >= MaxCachedViews) {
                LOG_WARNING(Render_Vulkan, "IssTemporal: too many images for DLSS views");
                return false;
            }
            const vk::ImageViewCreateInfo info{
                .image = image.image,
                .viewType = vk::ImageViewType::e2D,
                .format = image.format,
                .subresourceRange = {image.aspect, 0, 1, 0, 1},
            };
            auto [result, view] = instance.GetDevice().createImageViewUnique(info);
            if (result != vk::Result::eSuccess) {
                LOG_WARNING(Render_Vulkan, "IssTemporal: DLSS image view failed: {}", vk::to_string(result));
                return false;
            }
            found = views.emplace(key, std::move(view)).first;
        }
        out.image = key.image;
        out.view = static_cast<VkImageView>(*found->second);
        out.range = {key.aspect, 0, 1, 0, 1};
        out.format = key.format;
        out.width = image.width;
        out.height = image.height;
        return true;
    }

    const Instance& instance;
    const TlgDlssApi* api;
    TlgDlssFeature feature{};
    bool feature_made = false;
    std::unordered_map<ViewKey, vk::UniqueImageView, ViewKeyHash> views;
};

} // namespace

void AppendInstanceExtensions(std::vector<const char*>& enabled) {
    if (!Selected()) {
        return;
    }
    State& state = GetState();
    std::lock_guard lock{state.mutex};
    Load(state);
    if (!state.api) {
        return;
    }
    uint32_t count = 0;
    const VkExtensionProperties* required = nullptr;
    if (!state.api->InstanceExtensions(&count, &required)) {
        return Fail(state, "the NGX instance extension query failed");
    }
    const auto [result, available] = vk::enumerateInstanceExtensionProperties();
    std::string missing;
    if (result != vk::Result::eSuccess ||
        !CollectExtensions(required, count, available, state.instance_extensions, missing)) {
        state.api = nullptr;
        return Fail(state, "a Vulkan instance extension NGX needs is missing: " + missing);
    }
    for (const auto& name : state.instance_extensions) {
        // The strings live in the process-wide state until exit.
        if (std::ranges::none_of(enabled, [&](const char* current) { return name == current; })) {
            enabled.push_back(name.c_str());
        }
    }
}

const std::vector<std::string>& RequiredDeviceExtensions(vk::Instance instance, vk::PhysicalDevice physical) {
    static const std::vector<std::string> none;
    State& state = GetState();
    std::lock_guard lock{state.mutex};
    if (!Selected() || !state.api) {
        return none;
    }
    if (physical.getProperties().vendorID != 0x10de) {
        state.api = nullptr;
        Fail(state, "not an NVIDIA GPU (DLSS needs a GeForce RTX)");
        return none;
    }
    uint32_t count = 0;
    const VkExtensionProperties* required = nullptr;
    if (!state.api->DeviceExtensions(instance, physical, &count, &required)) {
        state.api = nullptr;
        Fail(state, "this GPU or driver does not support DLSS (a GeForce RTX and a recent driver are needed)");
        return none;
    }
    const auto [result, available] = physical.enumerateDeviceExtensionProperties();
    std::string missing;
    if (result != vk::Result::eSuccess ||
        !CollectExtensions(required, count, available, state.device_extensions, missing)) {
        state.api = nullptr;
        Fail(state, "a Vulkan device extension NGX needs is missing: " + missing);
        return none;
    }
    return state.device_extensions;
}

void Initialize(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device) {
    State& state = GetState();
    std::lock_guard lock{state.mutex};
    if (!Selected() || !state.api || state.ready) {
        return;
    }
    if (!state.api->Initialize(instance, physical, device, VULKAN_HPP_DEFAULT_DISPATCHER.vkGetInstanceProcAddr,
                               VULKAN_HPP_DEFAULT_DISPATCHER.vkGetDeviceProcAddr)) {
        state.api = nullptr;
        return Fail(state, "NVIDIA NGX initialisation failed (update the GPU driver)");
    }
    state.ready = true;
    LOG_INFO(Render_Vulkan, "IssTemporal: DLSS ready");
}

bool Usable() {
    State& state = GetState();
    std::lock_guard lock{state.mutex};
    return state.ready && state.api;
}

std::string Problem() {
    State& state = GetState();
    std::lock_guard lock{state.mutex};
    return state.problem;
}

std::unique_ptr<TemporalBackend> CreateBackend(const Instance& instance) {
    return Usable() ? std::make_unique<DlssBackend>(instance) : nullptr;
}

#else // not Windows: the bridge is Windows only

void AppendInstanceExtensions(std::vector<const char*>&) {}
const std::vector<std::string>& RequiredDeviceExtensions(vk::Instance, vk::PhysicalDevice) {
    static const std::vector<std::string> none;
    return none;
}
void Initialize(vk::Instance, vk::PhysicalDevice, vk::Device) {}
bool Usable() {
    return false;
}
std::string Problem() {
    return Selected() ? "DLSS is Windows only" : std::string{};
}
std::unique_ptr<TemporalBackend> CreateBackend(const Instance&) {
    return nullptr;
}

#endif

} // namespace Vulkan::IssTemporal::Dlss
