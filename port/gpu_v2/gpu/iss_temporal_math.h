// SPDX-License-Identifier: GPL-2.0-or-later
// Pure parts of the temporal upscaler (iss_temporal.cpp): the marker the game hook puts into the command
// buffer, and the matrix that takes this frame's clip space to the previous frame's. No Vulkan here, so
// tests/test_iss_temporal.cpp can check them alone.
#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace Vulkan::IssTemporal {

/// A 4x4 matrix in the order GLSL reads it from memory: element (row, col) is m[col * 4 + row], and
/// clip = m * vec4(position, 1).
using Mat4 = std::array<float, 16>;

/// The PM4 NOP the game hook inserts where the game would have run its own anti-aliasing. The first
/// payload dword is the magic with the kind in its low byte; the other dwords are the frame.
inline constexpr std::uint32_t kMarkerMask = 0xFFFFFF00u;
inline constexpr std::uint32_t kMarker = 0x49545400u; // 'I', 'T', 'T', 0
inline constexpr std::uint32_t kKindResolve = 1;
inline constexpr std::uint32_t kVersion = 1;
inline constexpr std::uint32_t kPayloadDwords = 33;
/// Payload flags.
inline constexpr std::uint32_t kFlagCameraCut = 1u << 0;
inline constexpr std::uint32_t kFlagReversedDepth = 1u << 1;

struct Frame {
    std::uint32_t frame_id = 0;
    std::uint32_t flags = 0;
    std::uint32_t render_width = 0;
    std::uint32_t render_height = 0;
    /// Pixels of the render: a point that would sit at pixel q without jitter is rasterised at q + jitter
    /// (x to the right, y down).
    float jitter[2] = {};
    Mat4 view_proj{}; ///< this frame, without jitter
    float near_z = 0;
    float far_z = 0;
    float fov_y = 0;
    std::uint64_t color_addr = 0;  ///< the game's anti-aliasing input
    std::uint64_t depth_addr = 0;  ///< the scene depth
    std::uint64_t output_addr = 0; ///< the game's anti-aliasing output
};

/// Payload word layout (dwords after the NOP header, the magic included):
///  0 magic|kind  1 version  2 frame_id  3 flags  4 render_width  5 render_height  6-7 jitter x, y
///  8-23 view_proj  24 near  25 far  26 fov_y  27-28 color_addr  29-30 depth_addr  31-32 output_addr
inline bool ParseMarker(const std::uint32_t* payload, std::uint32_t dwords, Frame& out) {
    if (dwords < kPayloadDwords || (payload[0] & kMarkerMask) != kMarker ||
        (payload[0] & 0xFFu) != kKindResolve || payload[1] != kVersion) {
        return false;
    }
    const auto real = [&](std::uint32_t index) {
        float value;
        std::memcpy(&value, &payload[index], sizeof(value));
        return value;
    };
    const auto address = [&](std::uint32_t index) {
        return std::uint64_t{payload[index]} | (std::uint64_t{payload[index + 1]} << 32);
    };
    out.frame_id = payload[2];
    out.flags = payload[3];
    out.render_width = payload[4];
    out.render_height = payload[5];
    out.jitter[0] = real(6);
    out.jitter[1] = real(7);
    for (std::uint32_t i = 0; i < 16; ++i) {
        out.view_proj[i] = real(8 + i);
    }
    out.near_z = real(24);
    out.far_z = real(25);
    out.fov_y = real(26);
    out.color_addr = address(27);
    out.depth_addr = address(29);
    out.output_addr = address(31);
    for (const float value : out.view_proj) {
        if (!std::isfinite(value)) {
            return false;
        }
    }
    return std::isfinite(out.jitter[0]) && std::isfinite(out.jitter[1]) && out.render_width != 0 &&
           out.render_height != 0;
}

/// Inverse of a general 4x4 matrix, in double precision. False when it is singular.
inline bool Invert(const std::array<double, 16>& m, std::array<double, 16>& inv) {
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] +
             m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] -
             m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] +
             m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] -
              m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] -
             m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] +
             m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] -
             m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] +
              m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] +
             m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] -
             m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] +
              m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] -
              m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] -
             m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] +
             m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] -
              m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] +
              m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    const double det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    if (!(std::fabs(det) > 1e-300) || !std::isfinite(det)) {
        return false;
    }
    for (double& value : inv) {
        value /= det;
    }
    return true;
}

/// The matrix taking this frame's clip space to the previous frame's: previous * inverse(current). Composed
/// in double precision so the large world translations cancel before the result is rounded to float.
inline bool Reproject(const Mat4& current, const Mat4& previous, Mat4& out) {
    std::array<double, 16> cur, prev, inv;
    for (int i = 0; i < 16; ++i) {
        cur[i] = current[i];
        prev[i] = previous[i];
    }
    if (!Invert(cur, inv)) {
        return false;
    }
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            double sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += prev[k * 4 + row] * inv[col * 4 + k];
            }
            out[col * 4 + row] = static_cast<float>(sum);
        }
    }
    return true;
}

/// What iss_camera_motion.comp computes for one pixel, on the CPU (tests): motion in pixels, previous
/// minus current, for the pixel centre (x, y) of a size_x by size_y render with depth `depth`.
inline bool MotionOfPixel(const Mat4& reproject, float x, float y, float depth, float size_x,
                          float size_y, const float jitter[2], float motion[2]) {
    const float scene_u = (x + 0.5f - jitter[0]) / size_x;
    const float scene_v = (y + 0.5f - jitter[1]) / size_y;
    const float ndc[4] = {scene_u * 2.0f - 1.0f, 1.0f - scene_v * 2.0f, depth, 1.0f};
    float clip[4];
    for (int row = 0; row < 4; ++row) {
        clip[row] = 0;
        for (int col = 0; col < 4; ++col) {
            clip[row] += reproject[col * 4 + row] * ndc[col];
        }
    }
    motion[0] = motion[1] = 0;
    if (!(clip[3] > 1e-6f)) {
        return false;
    }
    const float prev_u = (clip[0] / clip[3]) * 0.5f + 0.5f;
    const float prev_v = 0.5f - (clip[1] / clip[3]) * 0.5f;
    motion[0] = (prev_u - scene_u) * size_x;
    motion[1] = (prev_v - scene_v) * size_y;
    return true;
}

} // namespace Vulkan::IssTemporal
