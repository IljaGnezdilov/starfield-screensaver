#include "starfield.h"

#include <ppl.h>

#include <algorithm>
#include <cmath>

#include "config.h"

namespace sf {

namespace {

constexpr float kDegToRad = kPi / 180.0f;

// Maps linear segment time to rotation progress. The first and last `ease` fractions
// use a smooth ramp; the middle moves at constant angular speed. This avoids visible
// acceleration at every segment boundary.
float segmentFraction(float u, float ease) {
    u = saturate(u);
    ease = clampf(ease, 0.0f, 0.49f);

    const auto rampIntegral = [](float x) {
        return x * x * x - 0.5f * x * x * x * x;
    };

    const float total = 1.0f - ease;
    float integral;
    if (u < ease) {
        integral = ease * rampIntegral(u / ease);
    } else if (u <= 1.0f - ease) {
        integral = ease * 0.5f + (u - ease);
    } else {
        integral = total - ease * rampIntegral((1.0f - u) / ease);
    }

    return integral / total;
}

inline uint32_t packRgb(int r, int g, int b) {
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

inline uint32_t lerpColor(uint32_t a, uint32_t b, float t) {
    const int ar = static_cast<int>((a >> 16) & 0xFFu);
    const int ag = static_cast<int>((a >> 8) & 0xFFu);
    const int ab = static_cast<int>(a & 0xFFu);
    const int br = static_cast<int>((b >> 16) & 0xFFu);
    const int bg = static_cast<int>((b >> 8) & 0xFFu);
    const int bb = static_cast<int>(b & 0xFFu);

    return packRgb(static_cast<int>(ar + (br - ar) * t), static_cast<int>(ag + (bg - ag) * t),
                   static_cast<int>(ab + (bb - ab) * t));
}

// Approximate relativistic color shift: negative values cool toward blue, positive
// values heat toward red. The shift is intentionally exaggerated for visual effect.
inline uint32_t dopplerColor(uint32_t color, float shift, float brightness) {
    float r = static_cast<float>((color >> 16) & 0xFFu);
    float g = static_cast<float>((color >> 8) & 0xFFu);
    float b = static_cast<float>(color & 0xFFu);

    if (shift < 0.0f) {
        const float t = std::min(1.0f, -shift) * 0.92f;
        r = lerp(r, 14.0f + r * 0.12f, t);
        g = lerp(g, 28.0f + g * 0.30f, t);
        b = lerp(b, 195.0f + b * 0.25f, t);
    } else {
        const float t = std::min(1.0f, shift) * 0.88f;
        r = lerp(r, 220.0f + r * 0.25f, t);
        g = lerp(g, 14.0f + g * 0.38f, t);
        b = lerp(b, b * 0.20f, t);
    }

    return packRgb(static_cast<int>(r * brightness), static_cast<int>(g * brightness),
                   static_cast<int>(b * brightness));
}

inline uint32_t saturateColor(uint32_t color, float amount) {
    float r = static_cast<float>((color >> 16) & 0xFFu);
    float g = static_cast<float>((color >> 8) & 0xFFu);
    float b = static_cast<float>(color & 0xFFu);
    const float lum = 0.30f * r + 0.59f * g + 0.11f * b;
    r = lum + (r - lum) * amount;
    g = lum + (g - lum) * amount;
    b = lum + (b - lum) * amount;
    return packRgb(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
}

inline uint32_t pickColor(Rng& rng) {
    const float u = rng.unit();
    if (u < 0.50f) {
        return 0xFFFFF8EEu;
    }
    if (u < 0.66f) {
        return 0xFFDDE8FFu;
    }
    if (u < 0.77f) {
        return 0xFFB4CEFFu;
    }
    if (u < 0.87f) {
        return 0xFFFFE7C2u;
    }
    if (u < 0.94f) {
        return 0xFFFFC286u;
    }
    if (u < 0.972f) {
        return 0xFF8FE2FFu;
    }
    if (u < 0.99f) {
        return 0xFFC2A6FFu;
    }
    return 0xFFFF9ADCu;
}

Vec3 makeDirection(Rng& rng, const Vec3& bandNormal, float bandProbability) {
    if (rng.unit() < bandProbability) {
        Vec3 best = rng.unitVector();
        float bestAbs = std::fabs(dot(best, bandNormal));
        for (int i = 0; i < 5; ++i) {
            const Vec3 candidate = rng.unitVector();
            const float candidateAbs = std::fabs(dot(candidate, bandNormal));
            if (candidateAbs < bestAbs) {
                best = candidate;
                bestAbs = candidateAbs;
            }
        }
        return best;
    }
    return rng.unitVector();
}

// Liang-Barsky clipping for warp trails. Keeps long streaks from producing hundreds of
// off-screen splats while preserving the visible portion.
bool clipSegment(float& x0, float& y0, float& x1, float& y1, float minX, float minY, float maxX,
                 float maxY) {
    float t0 = 0.0f;
    float t1 = 1.0f;
    const float dx = x1 - x0;
    const float dy = y1 - y0;

    const float p[4] = {-dx, dx, -dy, dy};
    const float q[4] = {x0 - minX, maxX - x0, y0 - minY, maxY - y0};

    for (int i = 0; i < 4; ++i) {
        if (std::fabs(p[i]) < 1.0e-6f) {
            if (q[i] < 0.0f) {
                return false;
            }
            continue;
        }

        const float r = q[i] / p[i];
        if (p[i] < 0.0f) {
            if (r > t1) {
                return false;
            }
            if (r > t0) {
                t0 = r;
            }
        } else {
            if (r < t0) {
                return false;
            }
            if (r < t1) {
                t1 = r;
            }
        }
    }

    const float nx0 = x0 + dx * t0;
    const float ny0 = y0 + dy * t0;
    const float nx1 = x0 + dx * t1;
    const float ny1 = y0 + dy * t1;

    x0 = nx0;
    y0 = ny0;
    x1 = nx1;
    y1 = ny1;
    return true;
}

} // namespace

Scene::Scene() = default;

Scene::~Scene() {
    if (skyThread_.joinable()) {
        skyThread_.join();
    }
}

void Scene::init(uint32_t seed) {
    seed_ = seed;
    schedules_.clear();
    pendingSky_.reset();
    readyCycle_.store(-1);
    currentSkyCycle_ = -1;
    buildingCycle_ = -1;

    // The first sky must exist before the first frame, so build it synchronously. All
    // later skies are prepared in the background one cycle ahead.
    ensureSchedule(0);
    sky_.generate(skySeed(0), schedules_[0].nebula);
    currentSkyCycle_ = 0;

    generateStars(seed);

    if (asyncSky_) {
        startSkyBuild(1);
    }
}

void Scene::resize(int width, int height) {
    width_ = std::max(1, width);
    height_ = std::max(1, height);
}

void Scene::ensureSchedule(int index) {
    if (index < 0) {
        index = 0;
    }

    while (static_cast<int>(schedules_.size()) <= index) {
        const int cycle = static_cast<int>(schedules_.size());

        // Per-cycle RNG keeps schedule generation deterministic regardless of how many
        // frames were rendered before this cycle was requested.
        Rng rng(seed_ ^ (static_cast<uint32_t>(cycle) * 2654435761u) ^ 0x1234ABCDu);

        CycleSchedule cs;
        cs.segmentCount = rng.rangeInt(cfg::kSettings.minSegments, cfg::kSettings.maxSegments);
        cs.start = schedules_.empty()
                       ? 0.0
                       : schedules_.back().start + schedules_.back().length;
        // Avoid repeating the same nebula archetype in consecutive cycles when more
        // than one type is enabled; it makes the sequence feel less repetitive.
        const int previousType =
            schedules_.empty()
                ? -1
                : static_cast<int>(schedules_.back().nebula.type);
        cs.nebula = makeNebula(seed_, cycle, previousType);

        const Quat look = quatFromTo(Vec3(0.0f, 0.0f, 1.0f), cs.nebula.center);
        const float roll = rng.range(-0.20f, 0.20f);
        cs.qBase = quatMul(quatAxisAngle(cs.nebula.center, roll), look);

        cs.segments.reserve(static_cast<size_t>(cs.segmentCount));
        cs.startQ.reserve(static_cast<size_t>(cs.segmentCount));

        Quat q = cs.qBase;
        for (int i = 0; i < cs.segmentCount; ++i) {
            const float phi = rng.range(0.0f, kTwoPi);
            const Vec3 axis(std::cos(phi), std::sin(phi), 0.0f);
            const float sign = rng.unit() < 0.5f ? -1.0f : 1.0f;
            const float angle =
                sign * rng.range(cfg::kSettings.segmentMinDeg, cfg::kSettings.segmentMaxDeg) *
                kDegToRad;

            cs.startQ.push_back(q);
            cs.segments.push_back({axis, angle});
            q = quatMul(q, quatAxisAngle(axis, angle));
        }

        cs.endQ = q;
        cs.length =
            static_cast<double>(cs.segmentCount) *
                static_cast<double>(cfg::kSettings.segmentSeconds) +
            static_cast<double>(cfg::kSettings.warpSeconds);
        schedules_.push_back(std::move(cs));
    }
}

int Scene::cycleAt(double time) {
    if (time < 0.0) {
        time = 0.0;
    }

    int cycle = 0;
    ensureSchedule(0);

    while (time >= schedules_[cycle].start + schedules_[cycle].length) {
        ++cycle;
        ensureSchedule(cycle);
    }
    while (cycle > 0 && time < schedules_[cycle].start) {
        --cycle;
    }

    return cycle;
}

uint32_t Scene::skySeed(int cycle) const {
    return seed_ ^ (static_cast<uint32_t>(cycle) * 0x85EBCA6Bu) ^ 0xC2B2AE35u;
}

void Scene::swapToCycle(int cycle) {
    // Called from the render thread. Joining is safe here because the build is started
    // at least one full cycle in advance; the fallback only matters after a long stall.
    if (skyThread_.joinable()) {
        skyThread_.join();
    }

    if (pendingSky_ != nullptr && readyCycle_.load() == cycle) {
        sky_ = std::move(*pendingSky_);
    } else {
        Sky fresh;
        fresh.generate(skySeed(cycle), schedules_[cycle].nebula);
        sky_ = std::move(fresh);
    }

    pendingSky_.reset();
    buildingCycle_ = -1;
    readyCycle_.store(-1);
    currentSkyCycle_ = cycle;
}

void Scene::startSkyBuild(int cycle) {
    // Only one pending sky is kept. prepare() calls this every frame, so the
    // buildingCycle_ check is what prevents spawning a thread per frame.
    if (!asyncSky_ || cycle < 0) {
        return;
    }
    if (buildingCycle_ == cycle) {
        return;
    }

    if (skyThread_.joinable()) {
        skyThread_.join();
    }

    ensureSchedule(cycle);

    pendingSky_ = std::make_unique<Sky>();
    Sky* target = pendingSky_.get();
    const uint32_t seed = skySeed(cycle);
    const NebulaParams nebula = schedules_[cycle].nebula;

    buildingCycle_ = cycle;
    readyCycle_.store(-1);

    skyThread_ = std::thread([this, target, seed, nebula, cycle]() {
        target->generate(seed, nebula);
        readyCycle_.store(cycle);
    });
}

int Scene::cycleSegmentCount(int cycle) {
    ensureSchedule(cycle);
    return schedules_[cycle].segmentCount;
}

double Scene::cycleStart(int cycle) {
    ensureSchedule(cycle);
    return schedules_[cycle].start;
}

double Scene::cycleLength(int cycle) {
    ensureSchedule(cycle);
    return schedules_[cycle].length;
}

void Scene::prepare(double timeSeconds) {
    const int cycle = cycleAt(timeSeconds);
    ensureSchedule(cycle + 1);

    if (cycle != currentSkyCycle_) {
        swapToCycle(cycle);
    }

    startSkyBuild(cycle + 1);
}

void Scene::generateStars(uint32_t seed) {
    // Star directions and base appearance are generated once per process. Settings
    // affect population sizes, brightness/size ranges and feature probabilities.
    const cfg::Settings& settings = cfg::kSettings;
    stars_.clear();
    stars_.reserve(static_cast<size_t>(settings.starsFaint + settings.starsMain +
                                       settings.starsBright + settings.starsFeatured));

    Rng rng(seed ^ 0xA5A5F00Du);
    const Vec3 bandNormal = galaxyBandNormal();
    const float band = clampf(settings.starBand, 0.0f, 1.0f);

    for (int i = 0; i < settings.starsFaint; ++i) {
        Star s;
        s.dir = makeDirection(rng, bandNormal, 0.25f * band);
        s.size = 0.30f + std::pow(rng.unit(), 3.0f) * 0.32f;
        s.brightness = rng.range(0.18f, 0.55f);
        s.warpSensitivity = 0.45f + rng.unit() * 1.05f;
        s.twinklePhase = rng.range(0.0f, kTwoPi);
        s.dopplerBias = rng.range(-0.2f, 0.2f);
        s.color = pickColor(rng);
        s.spike = false;
        stars_.push_back(s);
    }

    for (int i = 0; i < settings.starsMain; ++i) {
        Star s;
        s.dir = makeDirection(rng, bandNormal, 0.55f * band);
        s.size = 0.40f + std::pow(rng.unit(), 2.6f) * 1.25f;
        s.brightness = rng.range(0.55f, 1.25f);
        s.warpSensitivity = 0.40f + rng.unit() * 1.15f;
        s.twinklePhase = rng.range(0.0f, kTwoPi);
        s.dopplerBias = rng.range(-0.45f, 0.45f);
        s.color = pickColor(rng);
        s.spike = rng.unit() < settings.starSpikeMain;
        s.bokeh = rng.unit() < settings.starBokehMain;
        stars_.push_back(s);
    }

    for (int i = 0; i < settings.starsBright; ++i) {
        Star s;
        s.dir = makeDirection(rng, bandNormal, 0.40f * band);
        s.size = 1.00f + std::pow(rng.unit(), 2.0f) * 1.60f;
        s.brightness = rng.range(2.00f, 3.60f);
        s.warpSensitivity = 0.35f + rng.unit() * 1.20f;
        s.twinklePhase = rng.range(0.0f, kTwoPi);
        s.dopplerBias = rng.range(-0.5f, 0.5f);
        s.color = pickColor(rng);
        s.spike = true;
        s.bokeh = rng.unit() < settings.starBokehBright;
        if (s.bokeh) {
            const uint32_t bokehPalette[4] = {0xFF7FA8FFu, 0xFFC08BFFu, 0xFFFFC15Eu, 0xFFFF8FD8u};
            s.color = bokehPalette[rng.rangeInt(0, 3)];
        }
        stars_.push_back(s);
    }

    struct Featured {
        Vec3 dir;
        float size;
        float brightness;
        uint32_t color;
    };

    const Featured featured[3] = {
        {normalize(Vec3(0.26f, 0.01f, 1.0f)), 2.60f, 2.70f, 0xFFFF96DCu},
        {normalize(Vec3(-0.80f, 0.12f, 1.0f)), 2.05f, 2.15f, 0xFF7FE4FFu},
        {normalize(Vec3(-0.20f, 0.28f, 1.0f)), 1.85f, 1.95f, 0xFFFFBE82u},
    };

    // The three hero stars are hand-placed relative to the default view. The setting
    // only selects how many of them are enabled.
    const int featuredCount = std::min(3, settings.starsFeatured);
    for (int i = 0; i < featuredCount; ++i) {
        const Featured& f = featured[i];
        Star s;
        s.dir = f.dir;
        s.size = f.size;
        s.brightness = f.brightness;
        s.warpSensitivity = 1.15f;
        s.twinklePhase = rng.range(0.0f, kTwoPi);
        s.dopplerBias = 0.0f;
        s.color = f.color;
        s.spike = true;
        s.bokeh = true;
        stars_.push_back(s);
    }

    // Additive blending is order-independent for color, but drawing bright stars last
    // makes their glows and spikes read more cleanly over faint stars.
    std::sort(stars_.begin(), stars_.end(),
              [](const Star& a, const Star& b) { return a.brightness < b.brightness; });
}

Scene::FrameParams Scene::params(double time) {
    const int cycle = cycleAt(time);
    const CycleSchedule& cs = schedules_[cycle];

    FrameParams p;
    p.cycle = cycle;

    // local is clamped to the cycle's rotation window; once rotationSpan is over, the
    // warp keeps advancing until the cycle's fade-out and length end.
    const double local = std::max(0.0, time - cs.start);
    const double segmentSeconds = static_cast<double>(cfg::kSettings.segmentSeconds);
    const double rotationSpan = static_cast<double>(cs.segmentCount) * segmentSeconds;
    const double rotationLocal = std::min(local, rotationSpan);

    int segment = static_cast<int>(rotationLocal / segmentSeconds);
    if (segment >= cs.segmentCount) {
        segment = cs.segmentCount - 1;
    }
    if (segment < 0) {
        segment = 0;
    }

    const float u =
        static_cast<float>((rotationLocal - static_cast<double>(segment) * segmentSeconds) /
                           segmentSeconds);
    const float ease = cfg::kSettings.segmentEaseSeconds / cfg::kSettings.segmentSeconds;
    const float fraction = segmentFraction(u, ease);

    p.q = quatMul(cs.startQ[static_cast<size_t>(segment)],
                  quatAxisAngle(cs.segments[static_cast<size_t>(segment)].axis,
                                cs.segments[static_cast<size_t>(segment)].angle * fraction));

    // Warp is normalized 0..1 for the configured duration and allowed to overshoot to
    // 1.6 so the last part of the effect keeps accelerating until fade-out.
    p.warp = clampf(
        static_cast<float>((local - rotationSpan) / cfg::kSettings.warpSeconds), 0.0f, 1.6f);

    // Fade in at the start of a cycle and fade out before the next one; the product
    // keeps the screen black exactly on cycle boundaries.
    float fade = smoothstep(static_cast<float>(local) / cfg::kSettings.fadeInSeconds);
    const double fadeOutStart = cs.length - static_cast<double>(cfg::kSettings.fadeOutSeconds);
    if (local > fadeOutStart) {
        const float t =
            static_cast<float>((local - fadeOutStart) / cfg::kSettings.fadeOutSeconds);
        fade *= 1.0f - smoothstep(t);
    }
    p.fade = saturate(fade);

    // Sample warp 55 ms in the past. The difference between warpPrev and warp becomes
    // the streak length, so trails stay short when the motion is slow.
    const double trailTime = time - static_cast<double>(cfg::kStreakSeconds);
    const int trailCycle = cycleAt(trailTime);
    if (trailCycle == cycle) {
        const double trailLocal = std::max(0.0, trailTime - cs.start);
        const float trailWarp = clampf(
            static_cast<float>((trailLocal - rotationSpan) / cfg::kSettings.warpSeconds), 0.0f,
            1.6f);
        p.warpPrev = std::min(trailWarp, p.warp);
    } else {
        p.warpPrev = p.warp;
    }

    return p;
}

void Scene::renderBackground(Framebuffer& fb, const FrameParams& p, const View& view) {
    // Sky sampling is the expensive part, so render it into a quarter-resolution (and
    // at most kSkySamplePixels) buffer, then bilinearly upscale into the framebuffer.
    const double pixels = static_cast<double>(fb.width) * static_cast<double>(fb.height);
    const double target = std::min(pixels * 0.25, static_cast<double>(cfg::kSkySamplePixels));
    const double sampleScale = std::sqrt(std::max(0.02, target / pixels));

    const int bw = std::max(1, std::min(fb.width,
                                        static_cast<int>(std::lround(fb.width * sampleScale))));
    const int bh = std::max(1, std::min(fb.height,
                                        static_cast<int>(std::lround(fb.height * sampleScale))));
    skyBuffer_.resize(static_cast<size_t>(bw) * static_cast<size_t>(bh));
    skyBufferWidth_ = bw;
    skyBufferHeight_ = bh;

    const float toSourceX = static_cast<float>(fb.width) / static_cast<float>(bw);
    const float toSourceY = static_cast<float>(fb.height) / static_cast<float>(bh);
    const float refWidth = std::max(1.0f, view.referenceWidth);
    const float refHeight = std::max(1.0f, view.referenceHeight);

    const Quat q = p.q;
    const float warp = p.warp;

    concurrency::parallel_for(0, bh, [&](int y) {
        uint32_t* row = skyBuffer_.data() + static_cast<size_t>(y) * static_cast<size_t>(bw);
        const float srcY = (static_cast<float>(y) + 0.5f) * toSourceY - 0.5f;
        const float rdy = view.centerY - srcY;
        const float ny = rdy / (0.5f * refHeight);

        for (int x = 0; x < bw; ++x) {
            const float srcX = (static_cast<float>(x) + 0.5f) * toSourceX - 0.5f;
            const float rdx = srcX - view.centerX;
            const float nx = rdx / (0.5f * refWidth);
            // Pinhole camera ray in panorama space, then rotate it into the sky frame.
            const Vec3 ray(rdx / view.focal, rdy / view.focal, 1.0f);

            const Vec3 world = normalize(quatRotate(q, ray));
            const float lon = std::atan2(world.x, world.z);
            const float lat = std::asin(clampf(world.y, -1.0f, 1.0f));

            const float u = lon * (1.0f / kTwoPi) + 0.5f;
            const float v = 0.5f - lat * (1.0f / kPi);

            uint32_t color = sky_.sampleSharp(u, v);
            if (warp > 0.01f) {
                const uint32_t blurred = sky_.sampleBlur(u, v);
                color = lerpColor(color, blurred, warp * 0.9f);
            }

            // Vignette plus a warp-only color treatment: warm edges and a cool center
            // glow sell the tunnel effect without changing the underlying sky texture.
            const float edge = std::sqrt(nx * nx + ny * ny) * 0.70710678f;
            const float vig = 1.0f - 0.34f * edge * edge;
            const float warm = warp * (0.10f + 0.95f * edge * edge);
            const float cool = warp * (1.0f - std::min(1.0f, edge));
            const float gain = vig * (1.0f + 0.60f * warp);

            int r = static_cast<int>(static_cast<float>((color >> 16) & 0xFFu) * gain + warm * 76.0f +
                                     cool * 5.0f);
            int g = static_cast<int>(static_cast<float>((color >> 8) & 0xFFu) * gain + warm * 8.0f +
                                     cool * 8.0f);
            int b = static_cast<int>(static_cast<float>(color & 0xFFu) * gain + warm * 66.0f +
                                     cool * 38.0f);

            const float centerGlow = std::exp(-edge * edge * 6.0f) * warp * 46.0f;
            r += static_cast<int>(centerGlow * 0.08f);
            g += static_cast<int>(centerGlow * 0.34f);
            b += static_cast<int>(centerGlow);

            row[x] = packRgb(r, g, b);
        }
    });

    concurrency::parallel_for(0, fb.height, [&](int y) {
        const float sy = ((static_cast<float>(y) + 0.5f) * static_cast<float>(bh) /
                          static_cast<float>(fb.height)) -
                         0.5f;
        int y0 = static_cast<int>(std::floor(sy));
        const float ty = sy - static_cast<float>(y0);
        const int y1 = std::min(std::max(y0 + 1, 0), bh - 1);
        y0 = std::min(std::max(y0, 0), bh - 1);

        const uint32_t* row0 = skyBuffer_.data() + static_cast<size_t>(y0) * static_cast<size_t>(bw);
        const uint32_t* row1 = skyBuffer_.data() + static_cast<size_t>(y1) * static_cast<size_t>(bw);
        uint32_t* out = fb.scanline(y);

        for (int x = 0; x < fb.width; ++x) {
            const float sx = ((static_cast<float>(x) + 0.5f) * static_cast<float>(bw) /
                              static_cast<float>(fb.width)) -
                             0.5f;
            int x0 = static_cast<int>(std::floor(sx));
            const float tx = sx - static_cast<float>(x0);
            const int x1 = std::min(std::max(x0 + 1, 0), bw - 1);
            x0 = std::min(std::max(x0, 0), bw - 1);

            const uint32_t c00 = row0[x0];
            const uint32_t c10 = row0[x1];
            const uint32_t c01 = row1[x0];
            const uint32_t c11 = row1[x1];

            const auto mix = [&](int shift) -> int {
                const float a = static_cast<float>((c00 >> shift) & 0xFFu);
                const float b = static_cast<float>((c10 >> shift) & 0xFFu);
                const float c = static_cast<float>((c01 >> shift) & 0xFFu);
                const float d = static_cast<float>((c11 >> shift) & 0xFFu);
                return static_cast<int>(a * (1.0f - tx) * (1.0f - ty) + b * tx * (1.0f - ty) +
                                        c * (1.0f - tx) * ty + d * tx * ty + 0.5f);
            };

            out[x] = packRgb(mix(16), mix(8), mix(0));
        }
    });
}

void Scene::renderStars(Renderer& renderer, const FrameParams& p, double time,
                        const View& view) const {
    Framebuffer& fb = renderer.frame();
    const float focal = view.focal;
    const float cx = view.centerX;
    const float cy = view.centerY;
    const Quat inv = quatConj(p.q);

    const float warp = p.warp;
    const float warpPrev = p.warpPrev;
    const float timeF = static_cast<float>(time);
    const float halfDiag =
        std::sqrt(static_cast<float>(fb.width * fb.width + fb.height * fb.height));
    const float refRadius = std::max(1.0f, 0.5f * view.referenceHeight);
    const float pixelScale =
        clampf(view.referenceHeight / static_cast<float>(cfg::kReferenceHeight),
               cfg::kSettings.starScaleMin, cfg::kSettings.starScaleMax) *
        cfg::kSettings.starSize;
    const float brightnessGain = cfg::kSettings.starBrightness;
    const float dopplerGain = cfg::kSettings.starDoppler;
    const float warpGain = cfg::kSettings.starWarp;

    for (const Star& star : stars_) {
        // There is no depth buffer: stars are points on a unit sphere around the camera.
        // Cull everything behind the near plane before projecting.
        const Vec3 cam = quatRotate(inv, star.dir);
        if (cam.z <= 0.02f) {
            continue;
        }

        const float invZ = 1.0f / cam.z;
        const float sx = cx + focal * cam.x * invZ;
        const float sy = cy - focal * cam.y * invZ;

        if (sx < -halfDiag || sx > static_cast<float>(fb.width) + halfDiag || sy < -halfDiag ||
            sy > static_cast<float>(fb.height) + halfDiag) {
            continue;
        }

        const float rdx = sx - cx;
        const float rdy = sy - cy;
        const float sens = star.warpSensitivity * warpGain;

        // Push stars radially away from the view center. The squared warp term makes
        // the acceleration feel nonlinear; sampling it twice yields the trail segment.
        const float dispNow = warp * warp * cfg::kWarpDisplacement * sens;
        const float dispPrev = warpPrev * warpPrev * cfg::kWarpDisplacement * sens;

        float x1 = sx + rdx * dispNow;
        float y1 = sy + rdy * dispNow;
        float x0 = sx + rdx * dispPrev;
        float y0 = sy + rdy * dispPrev;

        const float edgeNorm = clampf(std::sqrt(rdx * rdx + rdy * rdy) / refRadius, 0.0f, 1.7f);

        // During warp the star becomes a clipped segment from its previous to current
        // position, widened by extra lines for bright stars and bokeh halos.
        if (warp > 0.004f) {
            const float edge = std::min(1.0f, edgeNorm);
            const float shift =
                warp * (-0.55f + 0.55f * edge + star.dopplerBias * 1.7f) * dopplerGain;
            uint32_t color = dopplerColor(star.color, shift, 1.0f);
            if (star.bokeh) {
                color = saturateColor(color, 1.35f);
            }
            const float alpha = star.brightness * (0.9f + 0.9f * warp) * brightnessGain;
            const float sigma = star.size * pixelScale * (1.0f + 0.85f * warp * sens);

            const float streakX0 = x0;
            const float streakY0 = y0;
            float cx0 = x0;
            float cy0 = y0;
            float cx1 = x1;
            float cy1 = y1;
            const float margin = 8.0f;
            const bool visible = clipSegment(cx0, cy0, cx1, cy1, -margin, -margin,
                                             static_cast<float>(fb.width) + margin,
                                             static_cast<float>(fb.height) + margin);

            float streakDx = 0.0f;
            float streakDy = 0.0f;
            float streakLen = 0.0f;
            if (visible) {
                streakDx = cx1 - cx0;
                streakDy = cy1 - cy0;
                streakLen = std::sqrt(streakDx * streakDx + streakDy * streakDy);
                if (streakLen > 1.0f) {
                    renderer.drawLine(cx0, cy0, cx1, cy1, color, alpha * 0.85f,
                                      star.brightness > 1.0f);
                }
            }

            if (star.brightness > 1.7f && visible && streakLen > 3.0f) {
                const float invLen = 1.0f / streakLen;
                const float perpX = -streakDy * invLen;
                const float perpY = streakDx * invLen;
                const float width = std::min(9.0f * pixelScale, sigma * 2.4f);
                for (int k = -3; k <= 3; ++k) {
                    const float offset = static_cast<float>(k) * width * 0.34f;
                    const float fade = 1.0f - 0.22f * std::fabs(static_cast<float>(k));
                    renderer.drawLine(cx0 + perpX * offset, cy0 + perpY * offset,
                                      cx1 + perpX * offset, cy1 + perpY * offset, color,
                                      alpha * 0.16f * fade, true);
                }
            }

            if (star.bokeh && visible && streakLen > 3.0f) {
                const float invLen = 1.0f / streakLen;
                const float perpX = -streakDy * invLen;
                const float perpY = streakDx * invLen;
                const float width = std::min(42.0f * pixelScale, sigma * 9.0f);
                for (int k = -5; k <= 5; ++k) {
                    const float offset = static_cast<float>(k) * width * 0.24f;
                    const float fade = 1.0f - 0.16f * std::fabs(static_cast<float>(k));
                    renderer.drawLine(cx0 + perpX * offset, cy0 + perpY * offset,
                                      cx1 + perpX * offset, cy1 + perpY * offset, color,
                                      std::min(0.26f, alpha * 0.03f) * fade, true);
                }
            }

            if (x1 > -margin && x1 < static_cast<float>(fb.width) + margin && y1 > -margin &&
                y1 < static_cast<float>(fb.height) + margin) {
                renderer.drawGlow(x1, y1, sigma, color, alpha * 0.65f);

                if (star.brightness > 1.7f) {
                    renderer.drawGlow(x1, y1, sigma * 2.6f, color, std::min(0.30f, alpha * 0.055f));
                    renderer.drawGlow(lerp(streakX0, x1, 0.80f), lerp(streakY0, y1, 0.80f),
                                      sigma * 2.0f, color, std::min(0.18f, alpha * 0.03f));
                    renderer.drawGlow(lerp(streakX0, x1, 0.58f), lerp(streakY0, y1, 0.58f),
                                      sigma * 1.5f, color, std::min(0.12f, alpha * 0.02f));
                } else if (star.brightness > 1.05f) {
                    renderer.drawGlow(x1, y1, sigma * 1.8f, color, alpha * 0.08f);
                }

                if (star.bokeh) {
                    renderer.drawGlow(x1, y1, sigma * 13.0f, color, std::min(0.30f, alpha * 0.04f));
                    renderer.drawGlow(lerp(streakX0, x1, 0.78f), lerp(streakY0, y1, 0.78f),
                                      sigma * 8.0f, color, std::min(0.22f, alpha * 0.028f));
                }
            }
        } else {
            const uint32_t color = dopplerColor(star.color,
                                                warp * star.dopplerBias * 0.2f * dopplerGain,
                                                1.0f);
            const float twinkle =
                1.0f + cfg::kSettings.starTwinkle * std::sin(timeF * 1.7f + star.twinklePhase);
            const float alpha = star.brightness * twinkle * brightnessGain;
            const float size = star.size * pixelScale;

            renderer.drawGlow(x1, y1, size, color, alpha * 0.95f);

            if (star.brightness > 1.15f) {
                renderer.drawGlow(x1, y1, size * 2.6f, color, alpha * 0.11f);
            }
            if (star.bokeh) {
                renderer.drawGlow(x1, y1, size * 4.2f, color, alpha * 0.09f);
            }
            if (star.spike) {
                renderer.drawSpikes(x1, y1, (star.size * 3.5f + 2.0f) * pixelScale, color,
                                    alpha * 0.50f);
            }
        }
    }
}

void Scene::render(Renderer& renderer, double timeSeconds, double prevTimeSeconds) {
    Framebuffer& fb = renderer.frame();
    View view;
    view.centerX = 0.5f * static_cast<float>(fb.width);
    view.centerY = 0.5f * static_cast<float>(fb.height);
    view.focal = 0.5f * static_cast<float>(fb.height) /
                 std::tan(cfg::kFovYDeg * 0.5f * kDegToRad);
    view.referenceWidth = static_cast<float>(fb.width);
    view.referenceHeight = static_cast<float>(fb.height);
    render(renderer, timeSeconds, prevTimeSeconds, view);
}

void Scene::render(Renderer& renderer, double timeSeconds, double prevTimeSeconds,
                   const View& view) {
    (void)prevTimeSeconds;

    Framebuffer& fb = renderer.frame();
    if (fb.width <= 0 || fb.height <= 0) {
        return;
    }

    View adjusted = view;
    if (adjusted.focal <= 0.0f || adjusted.referenceWidth <= 0.0f ||
        adjusted.referenceHeight <= 0.0f) {
        adjusted.centerX = 0.5f * static_cast<float>(fb.width);
        adjusted.centerY = 0.5f * static_cast<float>(fb.height);
        adjusted.focal = 0.5f * static_cast<float>(fb.height) /
                         std::tan(cfg::kFovYDeg * 0.5f * kDegToRad);
        adjusted.referenceWidth = static_cast<float>(fb.width);
        adjusted.referenceHeight = static_cast<float>(fb.height);
    }

    // prepare() may swap the sky; params() then samples the already-valid timeline.
    prepare(timeSeconds);
    const FrameParams p = params(timeSeconds);
    renderBackground(fb, p, adjusted);
    renderStars(renderer, p, timeSeconds, adjusted);

    // Fade the complete frame, not just the background, so stars disappear with it.
    if (p.fade < 0.999f) {
        for (uint32_t& px : fb.pixels) {
            const int r = static_cast<int>(static_cast<float>((px >> 16) & 0xFFu) * p.fade);
            const int g = static_cast<int>(static_cast<float>((px >> 8) & 0xFFu) * p.fade);
            const int b = static_cast<int>(static_cast<float>(px & 0xFFu) * p.fade);
            px = packRgb(r, g, b);
        }
    }
}

} // namespace sf
