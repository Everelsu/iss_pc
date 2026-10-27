#!/usr/bin/env bash
# Builds tlg_dlss.dll (gpu/dlss_bridge) WITHOUT Visual Studio: MSYS2's clang cross-compiles for the MSVC ABI
# against the Microsoft CRT and Windows SDK that `xwin` unpacks into a user folder (no administrator rights),
# and lld-link links NVIDIA's static nvsdk_ngx_s.lib. Same result as build_dlss.sh, which needs Visual Studio.
# Like build_dlss.sh it is an OPTIONAL add-on built on the machine that has the SDK: nothing from NVIDIA or
# Microsoft is ever added to this repository or to the packages.
#
# One time, in any folders outside the repository:
#   1. git clone --depth 1 --filter=blob:none --sparse https://github.com/NVIDIA/DLSS dlss_sdk
#      (cd dlss_sdk && git sparse-checkout set include lib/Windows_x86_64/x64)
#   2. xwin (https://github.com/Jake-Shadle/xwin, accepts Microsoft's licence for the CRT and the SDK):
#      xwin --accept-license --arch x86_64 splat --use-winsysroot-style --preserve-ms-arch-notation \
#           --disable-symlinks --output msvc_sysroot
# Then, from an MSYS2 CLANG64 shell:
#   DLSS_SDK_ROOT=<dlss_sdk> XWIN_SYSROOT=<msvc_sysroot> bash packaging/windows/build_dlss_xwin.sh
set -euo pipefail
cd -- "$(dirname -- "$0")/../.."
sdk=${DLSS_SDK_ROOT:?Set DLSS_SDK_ROOT to a checkout of github.com/NVIDIA/DLSS (include/ and lib/)}
sysroot=${XWIN_SYSROOT:?Set XWIN_SYSROOT to the xwin --use-winsysroot-style output}
sdk=$(cygpath -m "$sdk") && sysroot=$(cygpath -m "$sysroot")   # the compiler and linker are Windows programs
export MSYS2_ARG_CONV_EXCL="*"   # lld-link's /options must not be turned into paths
crt=$(ls -d "$sysroot"/VC/Tools/MSVC/* | sort -V | tail -1)
kit=$(ls -d "$sysroot/Windows Kits/10/Include/"* | sort -V | tail -1)
kitver=$(basename "$kit")
version=$(basename "$crt")
out=out/dlss-bridge
rm -rf "$out" && mkdir -p "$out/vkinclude"
# Only the Vulkan headers: MSYS2's C headers must not reach the MSVC ones.
cp -r /c/msys64/clang64/include/vulkan /c/msys64/clang64/include/vk_video "$out/vkinclude/"
clang++ --target=x86_64-pc-windows-msvc -std=c++20 -O2 -DNOMINMAX -DWIN32_LEAN_AND_MEAN \
    -fms-compatibility -fms-extensions -fms-compatibility-version=19.44 -fms-runtime-lib=static \
    -Wno-unknown-pragmas -Wno-format-security -nostdinc \
    -isystem "$crt/include" -isystem "$kit/ucrt" -isystem "$kit/um" -isystem "$kit/shared" \
    -I "$sdk/include" -I "$out/vkinclude" -c gpu/dlss_bridge/tlg_dlss_bridge.cpp -o "$out/tlg_dlss_bridge.obj"
# The static CRT (/MT, like the SDK's nvsdk_ngx_s.lib): no Visual C++ runtime install on the player's PC.
lld-link -dll -out:"$out/tlg_dlss.dll" -machine:x64 -subsystem:windows "$out/tlg_dlss_bridge.obj" \
    "$sdk/lib/Windows_x86_64/x64/nvsdk_ngx_s.lib" libcmt.lib libvcruntime.lib libucrt.lib oldnames.lib \
    kernel32.lib user32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib version.lib setupapi.lib \
    -libpath:"$crt/lib/x64" -libpath:"$sysroot/Windows Kits/10/Lib/$kitver/ucrt/x64" \
    -libpath:"$sysroot/Windows Kits/10/Lib/$kitver/um/x64"
cp "$out/tlg_dlss.dll" out/
echo "DLSS bridge ready: out/tlg_dlss.dll (put it in <user data>/dlss or next to the executable)"
