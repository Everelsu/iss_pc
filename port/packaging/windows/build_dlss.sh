#!/usr/bin/env bash
# Builds tlg_dlss.dll (gpu/dlss_bridge, the only code that uses the NVIDIA DLSS SDK) with
# MSVC into out/tlg_dlss.dll. It is an OPTIONAL add-on, built on the machine that has the SDK: it links
# NVIDIA's static library (nvsdk_ngx_s.lib), so it is not part of the base package, and package.sh
# never ships it. NVIDIA's nvngx_dlss.dll is NOT copied anywhere either: the player gets it with the
# launcher's "Download DLSS from NVIDIA" button (launcher/dlss_setup.py), or copies it to <user data>/dlss.
# Needs Visual Studio 2022 (Build Tools, MSVC: MinGW/clang cannot link nvsdk_ngx_s.lib) and a checkout of
# https://github.com/NVIDIA/DLSS (any folder; it is never added to this repository):
#   DLSS_SDK_ROOT=F:/sdk/DLSS bash packaging/windows/build_dlss.sh
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
sdk=${DLSS_SDK_ROOT:?Set DLSS_SDK_ROOT to a checkout of github.com/NVIDIA/DLSS}
cmake=${CMAKE_EXE:-cmake}
# Only the Vulkan headers: MSYS2's C headers must not reach MSVC.
rm -rf out/vkinclude && mkdir -p out/vkinclude
cp -r /c/msys64/clang64/include/vulkan /c/msys64/clang64/include/vk_video out/vkinclude/
"$cmake" -S gpu/dlss_bridge -B out/dlss-bridge -G "Visual Studio 17 2022" -A x64 \
    -DDLSS_SDK_ROOT="$(cygpath -m "$sdk")" -DVULKAN_INCLUDE="$(cygpath -m "$PWD/out/vkinclude")"
"$cmake" --build out/dlss-bridge --config Release
cp out/dlss-bridge/Release/tlg_dlss.dll out/
echo "DLSS bridge ready: out/tlg_dlss.dll (put it in <user data>/dlss or next to the executable)"
