#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

#include "math.h"
#include "renderer.h"
#include "sky.h"

namespace sf {

// Direction and appearance are fixed at startup; runtime animation only changes the
// camera, twinkle phase and warp projection. Stars are stored sorted by brightness.
struct Star {
    Vec3 dir;
    float size = 1.0f;
    float brightness = 1.0f;
    float warpSensitivity = 1.0f;
    float twinklePhase = 0.0f;
    float dopplerBias = 0.0f;
    uint32_t color = 0xFFFFFFFFu;
    bool spike = false;
    bool bokeh = false;
};

// Projection for one window inside the virtual panorama. centerX/centerY and focal are
// in that window's pixels; referenceWidth/referenceHeight are the union of all monitors.
struct View {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float focal = 0.0f;
    float referenceWidth = 0.0f;
    float referenceHeight = 0.0f;
};

// Owns the deterministic timeline, the star field and the current/pending sky texture.
// prepare() is cheap and is also called at the start of render(), so callers may use
// either entry point for offline rendering.
class Scene {
public:
    Scene();
    ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    void init(uint32_t seed);
    void setAsyncSky(bool enabled) { asyncSky_ = enabled; }
    void resize(int width, int height);
    void prepare(double timeSeconds);
    void render(Renderer& renderer, double timeSeconds, double prevTimeSeconds);
    void render(Renderer& renderer, double timeSeconds, double prevTimeSeconds, const View& view);

    int cycleSegmentCount(int cycle);
    double cycleStart(int cycle);
    double cycleLength(int cycle);

private:
    struct Segment {
        Vec3 axis;
        float angle = 0.0f;
    };

    // A cycle is N eased rotations followed by one warp. startQ stores the orientation
    // before each segment so rendering never has to replay the whole cycle.
    struct CycleSchedule {
        double start = 0.0;
        double length = 0.0;
        int segmentCount = 1;
        Quat qBase;
        Quat endQ;
        std::vector<Segment> segments;
        std::vector<Quat> startQ;
        NebulaParams nebula;
    };

    // Per-frame derived state. warp and fade are normalized; warpPrev is sampled a
    // short time in the past to draw motion streaks.
    struct FrameParams {
        Quat q;
        float warp = 0.0f;
        float warpPrev = 0.0f;
        float fade = 1.0f;
        int cycle = 0;
    };

    void ensureSchedule(int index);
    int cycleAt(double time);
    uint32_t skySeed(int cycle) const;
    void swapToCycle(int cycle);
    void startSkyBuild(int cycle);
    FrameParams params(double time);
    void generateStars(uint32_t seed);
    void renderBackground(Framebuffer& fb, const FrameParams& params, const View& view);
    void renderStars(Renderer& renderer, const FrameParams& params, double time,
                     const View& view) const;

    Sky sky_;
    std::unique_ptr<Sky> pendingSky_;
    std::thread skyThread_;
    std::atomic<int> readyCycle_{-1};
    int currentSkyCycle_ = -1;
    int buildingCycle_ = -1;
    bool asyncSky_ = true;

    std::vector<Star> stars_;
    std::vector<CycleSchedule> schedules_;
    std::vector<uint32_t> skyBuffer_;
    int skyBufferWidth_ = 0;
    int skyBufferHeight_ = 0;
    int width_ = 0;
    int height_ = 0;
    uint32_t seed_ = 0;
};

} // namespace sf
