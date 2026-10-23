#pragma once

#include <cstdint>
#include <vector>

#include "math.h"

namespace sf {

// Tilted normal of the Milky Way band. Stars and nebulae are biased toward this plane.
inline Vec3 galaxyBandNormal() {
    return normalize(Vec3(0.38f, 0.92f, 0.08f));
}

// u wraps around longitude, v runs from north (0) to south (1).
inline Vec3 equirectDirection(float u, float v) {
    const float lon = (u - 0.5f) * kTwoPi;
    const float lat = (0.5f - v) * kPi;
    const float cl = std::cos(lat);
    return Vec3(cl * std::sin(lon), std::sin(lat), cl * std::cos(lon));
}

enum class NebulaType {
    Emission = 0,
    Shell = 1,
    Burst = 2,
};

// Fully describes one procedural nebula. Shape fields are expressed in a local
// tangent plane around `center`, with radius as the unit of length. Counts are bounded
// by the fixed array sizes below; makeNebula() never exceeds them.
struct NebulaParams {
    NebulaType type = NebulaType::Emission;
    Vec3 center = Vec3(0.0f, 0.0f, 1.0f);
    float radius = 0.25f;
    float intensity = 1.0f;
    uint32_t seed = 0;
    uint32_t colorHot = 0xFFFFB4A0u;
    uint32_t colorMid = 0xFFD84A50u;
    uint32_t colorCool = 0xFF4A78C8u;
    uint32_t colorRim = 0xFF66C89Bu;

    Quat noiseRotation;
    float stretchX = 1.0f;
    float stretchY = 1.0f;
    float rotation = 0.0f;
    float offsetX = 0.0f;
    float offsetY = 0.0f;
    float warpAmount = 0.4f;
    float edgeAmount = 0.35f;

    // Emission/Burst lobes; capacity 4 matches the settings UI maximum.
    int lobeCount = 0;
    float lobeX[4] = {};
    float lobeY[4] = {};
    float lobeRadius[4] = {};
    float lobeWeight[4] = {};

    // Shell arcs; three is the maximum number of partial rings.
    int arcCount = 0;
    float arcCenter[3] = {};
    float arcHalfWidth[3] = {};
    float ringRadius = 0.72f;
    float ringWidth = 0.17f;

    // Burst jets; five is the settings UI maximum.
    int jetCount = 0;
    float jetAngle[5] = {};
    float jetWidth[5] = {};
    float jetLength[5] = {};
    float jetTwist[5] = {};
    float jetWeight[5] = {};

    float coreOffsetX = 0.0f;
    float coreOffsetY = 0.0f;
    float coreRadius = 0.32f;

    float dustAngle = 0.0f;
    float dustWidth = 0.35f;
    float dustStrength = 0.8f;
};

// Deterministically builds a nebula for a cycle, honoring the type mask, palette and
// size/count settings. avoidType prevents an immediate repeat when possible.
NebulaParams makeNebula(uint32_t seed, int cycle, int avoidType = -1);

// Equirectangular sky texture. `sharp` is used outside warp; `blurred` is blended in
// during warp to give the background a soft tunnel look.
class Sky {
public:
    void generate(uint32_t seed, const NebulaParams& nebula);

    inline uint32_t sampleSharp(float u, float v) const { return sample(sharp_, u, v); }
    inline uint32_t sampleBlur(float u, float v) const { return sample(blurred_, u, v); }

    int width() const { return width_; }
    int height() const { return height_; }

private:
    uint32_t sample(const std::vector<uint32_t>& tex, float u, float v) const;

    int width_ = 0;
    int height_ = 0;
    std::vector<uint32_t> sharp_;
    std::vector<uint32_t> blurred_;
};

} // namespace sf
