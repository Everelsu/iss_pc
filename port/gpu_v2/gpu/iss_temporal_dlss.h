// SPDX-License-Identifier: GPL-2.0-or-later
// NVIDIA DLSS Super Resolution as a backend of the temporal upscaler (iss_temporal_upscaler.h), with
// the DLLs the PLAYER supplies. The port contains no NVIDIA code and ships no NVIDIA binary:
//   - tlg_dlss.dll, the bridge (gpu/dlss_bridge, MIT), is built locally from the player's copy of the
//     NVIDIA DLSS SDK (packaging/windows/build_dlss.sh) and loaded at run time;
//   - nvngx_dlss.dll is NVIDIA's own, copied by the player from github.com/NVIDIA/DLSS
//     (lib/Windows_x86_64/rel/) or from a game.
// Both are searched in `dlss_dll_dir` (tlg.ini / TLG_DLSS_DIR), then <user data>/dlss, then next to the
// executable. When anything is missing, or the GPU is not an NVIDIA RTX, DLSS is simply not offered:
// the renderer uses FSR 3.1 and logs why. Nothing here can stop the game from starting.
//
// Chosen with iss_temporal_backend=1 (tlg.ini / TLG_ISS_TEMPORAL_BACKEND). The Vulkan extensions NGX
// needs are enabled only then, and only when the DLLs were found.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "iss_temporal_upscaler.h"

namespace Vulkan::IssTemporal::Dlss {

/// Whether the player asked for DLSS (iss_temporal_backend = 1) with the upscaler on.
bool Selected();

/// Called while the Vulkan instance / device are made. Add what NGX needs to the lists (names of
/// extensions the driver offers). Quietly do nothing unless DLSS is selected and its DLLs load.
void AppendInstanceExtensions(std::vector<const char*>& enabled);
/// The returned names live until the process ends (the device creation keeps pointers to them).
const std::vector<std::string>& RequiredDeviceExtensions(vk::Instance instance, vk::PhysicalDevice physical);
/// After the device exists: starts NGX. Failure is remembered (Problem) and DLSS stays off.
void Initialize(vk::Instance instance, vk::PhysicalDevice physical, vk::Device device);

/// DLSS can run now (initialised, device and DLL accepted).
bool Usable();
/// Why it cannot (empty when it can or was not selected). Shown in the log and the menu.
std::string Problem();

/// The DLSS implementation of the backend interface; null when DLSS is not usable.
std::unique_ptr<TemporalBackend> CreateBackend(const Instance& instance);

/// The mode for an output/render size ratio: 0 DLAA, 1 Quality, 2 Balanced, 3 Performance, 4 Ultra Performance.
int QualityForScale(float output_over_render);

} // namespace Vulkan::IssTemporal::Dlss

namespace Vulkan::IssTemporal {

/// DLSS when selected and usable, else FSR 3.1. The one place that chooses.
std::unique_ptr<TemporalBackend> CreateSelectedBackend(const Instance& instance);

} // namespace Vulkan::IssTemporal
