#pragma once

#include <cstdint>

namespace cfg {

// Fixed presentation constants. Changing them changes the established look, so they
// are intentionally not exposed in the settings dialog or registry.
constexpr float kFovYDeg = 60.0f;

constexpr int kSkyWidth = 1536;
constexpr int kSkyHeight = 768;

constexpr int kReferenceHeight = 1080;

// The software renderer caps the internal framebuffer at 16 megapixels and stretches
// it to the window. The sky is sampled into a much smaller intermediate buffer.
constexpr int kMaxRenderPixels = 16000000;
constexpr int kSkySamplePixels = 1200000;

constexpr float kWarpDisplacement = 2.9f;
constexpr float kStreakSeconds = 0.055f;

constexpr int kStarTotalCap = 120000;

// User-editable settings loaded from HKCU before the scene starts. Values use the
// same units as the UI: seconds for time, degrees for angles, pixels for star scale.
struct Settings {
    int64_t seed = -1;  // -1 means derive a random seed from UTC milliseconds.

    // Cycle timeline: each segment rotates by a random angle, then a warp plays.
    float segmentSeconds = 5.0f;
    int minSegments = 1;
    int maxSegments = 5;
    float segmentMinDeg = 16.0f;
    float segmentMaxDeg = 40.0f;
    float warpSeconds = 1.0f;

    // Derived by deriveTimings(); never persisted because they must stay proportional
    // to segmentSeconds and warpSeconds.
    float segmentEaseSeconds = 0.5f;
    float fadeInSeconds = 0.7f;
    float fadeOutSeconds = 0.4f;

    // Nebula shape and color multipliers.
    float nebulaIntensity = 1.0f;
    float nebulaSize = 1.0f;
    int nebulaTypes = 7;  // Bit mask: 1 = emission, 2 = shell, 4 = burst.

    float nebulaDust = 1.0f;
    float nebulaWarp = 1.0f;
    float nebulaEdge = 1.0f;
    int nebulaPalette = -1;  // -1 picks a palette per cycle; 0..3 force one.
    int nebulaLobesMin = 2;
    int nebulaLobesMax = 4;
    int nebulaArcsMin = 1;
    int nebulaArcsMax = 3;
    int nebulaJetsMin = 2;
    int nebulaJetsMax = 5;

    // Star populations. The four counts together are capped at kStarTotalCap.
    int starsFaint = 26000;
    int starsMain = 4200;
    int starsBright = 140;
    int starsFeatured = 3;
    float starSize = 1.0f;
    float starBrightness = 1.0f;
    float starBand = 1.0f;
    float starTwinkle = 0.06f;
    float starSpikeMain = 0.10f;
    float starBokehMain = 0.10f;
    float starBokehBright = 0.22f;
    float starDoppler = 1.0f;
    float starWarp = 1.0f;
    float starScaleMin = 0.6f;
    float starScaleMax = 2.5f;
};

// Loaded once in wWinMain and treated as read-only by the scene after startup.
inline Settings kSettings;

} // namespace cfg
