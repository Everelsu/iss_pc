// SPDX-License-Identifier: GPL-2.0-or-later
// The pure parts of the temporal upscaler (gpu_v2/gpu/iss_temporal_math.h): the game's marker, the matrix
// that reprojects this frame's clip space into the previous one, and the motion it gives a pixel.
#include "../gpu_v2/gpu/iss_temporal_math.h"
#include "../src/iss_temporal.h" // the game side: the marker both sides must agree on

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using namespace Vulkan::IssTemporal;

static int failures = 0;
#define CHECK(condition)                                                                       \
    do {                                                                                       \
        if (!(condition)) {                                                                    \
            std::printf("FAIL line %d: %s\n", __LINE__, #condition);                           \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)
#define CHECK_NEAR(a, b, eps)                                                                  \
    do {                                                                                       \
        if (!(std::fabs(double(a) - double(b)) <= (eps))) {                                    \
            std::printf("FAIL line %d: %s = %g, expected %g\n", __LINE__, #a, double(a),       \
                        double(b));                                                            \
            ++failures;                                                                        \
        }                                                                                      \
    } while (0)

static_assert(kMarker == ISS_TEMPORAL_MARKER_MAGIC, "the marker magic");
static_assert(kKindResolve == ISS_TEMPORAL_MARKER_KIND_RESOLVE, "the marker kind");
static_assert(kVersion == ISS_TEMPORAL_MARKER_VERSION, "the marker version");
static_assert(kPayloadDwords == ISS_TEMPORAL_MARKER_PAYLOAD_DWORDS, "the payload length");
static_assert(kFlagCameraCut == ISS_TEMPORAL_FLAG_CAMERA_CUT, "the camera cut flag");
static_assert(kFlagReversedDepth == ISS_TEMPORAL_FLAG_REVERSED_DEPTH, "the reversed depth flag");

namespace {

// clip = m * vec4(p, 1): a perspective projection (D3D style: z in 0..1, y up) times a view that
// translates the world by (-tx, -ty, -tz).
Mat4 ViewProj(float tx, float ty, float tz, bool infinite_far = false) {
    const float f = 1.5f, aspect = 16.0f / 9.0f, n = 0.1f, far_z = 1000.0f;
    Mat4 p{};
    p[0] = f / aspect;
    p[5] = f;
    p[10] = infinite_far ? 1.0f : far_z / (far_z - n);
    p[11] = 1.0f;
    p[14] = infinite_far ? -n : -n * far_z / (far_z - n);
    Mat4 v{};
    v[0] = v[5] = v[10] = v[15] = 1.0f;
    v[12] = -tx;
    v[13] = -ty;
    v[14] = -tz;
    Mat4 out{};
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float sum = 0;
            for (int k = 0; k < 4; ++k) {
                sum += p[k * 4 + row] * v[col * 4 + k];
            }
            out[col * 4 + row] = sum;
        }
    }
    return out;
}

void Project(const Mat4& m, const double world[3], double clip[4]) {
    for (int row = 0; row < 4; ++row) {
        clip[row] = m[12 + row];
        for (int col = 0; col < 3; ++col) {
            clip[row] += double(m[col * 4 + row]) * world[col];
        }
    }
}

void TestMarker() {
    std::uint32_t payload[kPayloadDwords] = {};
    payload[0] = kMarker | kKindResolve;
    payload[1] = kVersion;
    payload[2] = 77;
    payload[3] = kFlagCameraCut;
    payload[4] = 1920;
    payload[5] = 1080;
    const float jitter[2] = {0.25f, -0.125f};
    std::memcpy(&payload[6], jitter, sizeof(jitter));
    const Mat4 m = ViewProj(1, 2, 3);
    std::memcpy(&payload[8], m.data(), sizeof(float) * 16);
    const float cam[3] = {0.1f, 1.0e7f, 0.9f};
    std::memcpy(&payload[24], cam, sizeof(cam));
    payload[27] = 0x11111111u;
    payload[28] = 0x2u;
    payload[29] = 0x33333333u;
    payload[30] = 0x4u;
    payload[31] = 0x55555555u;
    payload[32] = 0x6u;

    Frame frame;
    CHECK(ParseMarker(payload, kPayloadDwords, frame));
    CHECK(frame.frame_id == 77 && frame.flags == kFlagCameraCut);
    CHECK(frame.render_width == 1920 && frame.render_height == 1080);
    CHECK(frame.jitter[0] == 0.25f && frame.jitter[1] == -0.125f);
    CHECK(frame.view_proj == m);
    CHECK(frame.far_z == 1.0e7f);
    CHECK(frame.color_addr == 0x211111111ull && frame.depth_addr == 0x433333333ull &&
          frame.output_addr == 0x655555555ull);

    CHECK(!ParseMarker(payload, kPayloadDwords - 1, frame)); // too short
    payload[1] = kVersion + 1;
    CHECK(!ParseMarker(payload, kPayloadDwords, frame)); // another version
    payload[1] = kVersion;
    payload[0] = kMarker | 9;
    CHECK(!ParseMarker(payload, kPayloadDwords, frame)); // another kind
    payload[0] = 0x49535300u | kKindResolve;
    CHECK(!ParseMarker(payload, kPayloadDwords, frame)); // the Castlesea markers are not ours
    payload[0] = kMarker | kKindResolve;
    const float nan = std::nanf("");
    std::memcpy(&payload[9], &nan, sizeof(nan));
    CHECK(!ParseMarker(payload, kPayloadDwords, frame)); // a broken matrix
}

void TestStill() {
    const Mat4 m = ViewProj(100, 5, -30);
    Mat4 r;
    CHECK(Reproject(m, m, r));
    const float no_jitter[2] = {0, 0};
    float motion[2];
    for (const float depth : {0.2f, 0.9f, 0.999f}) {
        CHECK(MotionOfPixel(r, 640, 360, depth, 1920, 1080, no_jitter, motion));
        CHECK_NEAR(motion[0], 0, 1e-2);
        CHECK_NEAR(motion[1], 0, 1e-2);
    }
}

void TestMovingCamera() {
    // The camera moves 0.5 units to the right between the frames: a point 10 units ahead slides left.
    const Mat4 previous = ViewProj(1000, 20, 5), current = ViewProj(1000.5f, 20, 5);
    Mat4 r;
    CHECK(Reproject(current, previous, r));
    const double world[3] = {1002.0, 21.0, 15.0};
    double cur[4], prev[4];
    Project(current, world, cur);
    Project(previous, world, prev);
    const double size_x = 1920, size_y = 1080;
    const auto pixel_of = [&](const double* clip, double& px, double& py) {
        px = (clip[0] / clip[3] * 0.5 + 0.5) * size_x;
        py = (0.5 - clip[1] / clip[3] * 0.5) * size_y;
    };
    double cx, cy, px, py;
    pixel_of(cur, cx, cy);
    pixel_of(prev, px, py);
    const float depth = float(cur[2] / cur[3]);

    // The pixel whose centre is exactly the point (no jitter), then with a jitter: the point is
    // rasterised at its unjittered place plus the jitter, and the vector must not change.
    for (const float jx : {0.0f, 0.25f, -0.375f}) {
        for (const float jy : {0.0f, -0.25f, 0.125f}) {
            const float jitter[2] = {jx, jy};
            float motion[2];
            CHECK(MotionOfPixel(r, float(cx + jx - 0.5), float(cy + jy - 0.5), depth, float(size_x),
                                float(size_y), jitter, motion));
            CHECK_NEAR(motion[0], px - cx, 0.05);
            CHECK_NEAR(motion[1], py - cy, 0.05);
        }
    }
    CHECK(px - cx > 1.0); // the test would pass on a zero vector otherwise
}

void TestFarPlane() {
    // With an infinite far plane, depth 1 is a point at infinity: it does not move with the camera.
    const Mat4 previous = ViewProj(500, 0, 0, true), current = ViewProj(900, 0, 0, true);
    Mat4 r;
    CHECK(Reproject(current, previous, r));
    const float no_jitter[2] = {0, 0};
    float motion[2];
    CHECK(MotionOfPixel(r, 960, 540, 1.0f, 1920, 1080, no_jitter, motion));
    CHECK(std::fabs(motion[0]) < 1.0f && std::fabs(motion[1]) < 1.0f);
}

void TestDepthTargets() {
    DepthTargets targets;
    CHECK(targets.Find(1920, 1080) == 0);
    targets.Note(0x1000, 1920, 1080);
    targets.Note(0x2000, 2048, 2048); // a shadow map
    CHECK(targets.Find(1920, 1080) == 0x1000);
    targets.Note(0x3000, 1920, 1080); // a newer target of the same size comes first
    CHECK(targets.Find(1920, 1080) == 0x3000);
    CHECK(targets.Find(2048, 2048) == 0x2000);
    {   // all of the size, newest first; drawing into an older one again moves it to the front
        std::uint64_t out[DepthTargets::kCandidates];
        CHECK(targets.Candidates(1920, 1080, out) == 2);
        CHECK(out[0] == 0x3000 && out[1] == 0x1000);
        targets.Note(0x1000, 1920, 1080);
        CHECK(targets.Candidates(1920, 1080, out) == 2);
        CHECK(out[0] == 0x1000 && out[1] == 0x3000);
        CHECK(targets.Candidates(640, 480, out) == 0);
    }
    for (std::uint32_t i = 0; i < 6; ++i) { // no more than kCandidates come back
        targets.Note(0x5000 + i, 1280, 720);
    }
    {
        std::uint64_t out[DepthTargets::kCandidates];
        CHECK(targets.Candidates(1280, 720, out) == DepthTargets::kCandidates);
        CHECK(out[0] == 0x5005 && out[3] == 0x5002);
    }
    for (std::uint32_t i = 0; i < 20; ++i) { // more targets than entries: the oldest go
        targets.Note(0x9000 + i, 100 + i, 100);
    }
    CHECK(targets.Find(119, 100) == 0x9000 + 19);
    CHECK(targets.Find(1920, 1080) == 0);
}

void TestSingular() {
    const Mat4 zero{};
    Mat4 r;
    CHECK(!Reproject(zero, ViewProj(0, 0, 0), r));
}

} // namespace

int main() {
    TestMarker();
    TestStill();
    TestMovingCamera();
    TestFarPlane();
    TestDepthTargets();
    TestSingular();
    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("iss temporal: ok");
    return EXIT_SUCCESS;
}
