#include "sky.h"

#include <ppl.h>

#include <algorithm>
#include <cmath>

#include "config.h"

namespace sf {

namespace {

inline float hashToUnit(uint32_t h) {
    return static_cast<float>(h >> 8) * (1.0f / 16777216.0f);
}

inline uint32_t hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x8DA6B343u;
    h ^= static_cast<uint32_t>(y) * 0xD8163841u;
    h ^= static_cast<uint32_t>(z) * 0xCB1AB31Fu;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

inline float valueNoise(const Vec3& p, uint32_t seed) {
    const float fxp = std::floor(p.x);
    const float fyp = std::floor(p.y);
    const float fzp = std::floor(p.z);

    const int ix = static_cast<int>(fxp);
    const int iy = static_cast<int>(fyp);
    const int iz = static_cast<int>(fzp);

    float tx = p.x - fxp;
    float ty = p.y - fyp;
    float tz = p.z - fzp;
    tx = tx * tx * (3.0f - 2.0f * tx);
    ty = ty * ty * (3.0f - 2.0f * ty);
    tz = tz * tz * (3.0f - 2.0f * tz);

    const float v000 = hashToUnit(hash3(ix, iy, iz, seed));
    const float v100 = hashToUnit(hash3(ix + 1, iy, iz, seed));
    const float v010 = hashToUnit(hash3(ix, iy + 1, iz, seed));
    const float v110 = hashToUnit(hash3(ix + 1, iy + 1, iz, seed));
    const float v001 = hashToUnit(hash3(ix, iy, iz + 1, seed));
    const float v101 = hashToUnit(hash3(ix + 1, iy, iz + 1, seed));
    const float v011 = hashToUnit(hash3(ix, iy + 1, iz + 1, seed));
    const float v111 = hashToUnit(hash3(ix + 1, iy + 1, iz + 1, seed));

    const float x00 = lerp(v000, v100, tx);
    const float x10 = lerp(v010, v110, tx);
    const float x01 = lerp(v001, v101, tx);
    const float x11 = lerp(v011, v111, tx);

    const float y0 = lerp(x00, x10, ty);
    const float y1 = lerp(x01, x11, ty);

    return lerp(y0, y1, tz);
}

// Fractal value noise. Seeds are offset per octave so layers do not align into a grid.
float fbm(const Vec3& p, int octaves, float frequency, float gain, uint32_t seed) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    float freq = frequency;

    for (int i = 0; i < octaves; ++i) {
        sum += amp * valueNoise(p * freq, seed + static_cast<uint32_t>(i) * 1013u);
        norm += amp;
        amp *= gain;
        freq *= 2.03f;
    }

    return sum / norm;
}

inline float ridged(float n) {
    return 1.0f - std::fabs(2.0f * n - 1.0f);
}

inline uint32_t packRgb(int r, int g, int b) {
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (0xFFu << 24) | (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) |
           static_cast<uint32_t>(b);
}

inline Vec3 unpackColor(uint32_t c) {
    return Vec3(static_cast<float>((c >> 16) & 0xFFu), static_cast<float>((c >> 8) & 0xFFu),
                static_cast<float>(c & 0xFFu));
}

uint32_t tintedColor(Rng& rng, uint32_t base, float amount) {
    Vec3 c = unpackColor(base);
    c.x *= rng.range(1.0f - amount, 1.0f + amount);
    c.y *= rng.range(1.0f - amount, 1.0f + amount);
    c.z *= rng.range(1.0f - amount, 1.0f + amount);
    return packRgb(static_cast<int>(c.x), static_cast<int>(c.y), static_cast<int>(c.z));
}

float wrapAngle(float a) {
    a = std::fmod(a + kPi, kTwoPi);
    if (a < 0.0f) {
        a += kTwoPi;
    }
    return a - kPi;
}

// Evaluates one nebula at a sky direction. The direction is projected onto a tangent
// plane around n.center; all shape math uses those two coordinates. The three type
// branches share noise/core/dust but compose color differently.
void nebulaSample(const Vec3& dir, const NebulaParams& n, Vec3& add, float& dustOut) {
    const Vec3 offA(11.7f, -3.4f, 7.1f);
    const Vec3 offB(-5.2f, 9.8f, 2.3f);
    const Vec3 offC(3.1f, 1.7f, -2.4f);

    const Vec3 nd = quatRotate(n.noiseRotation, dir);
    const float fine = fbm(nd + offA, 4, 9.0f, 0.55f, n.seed + 5u);
    const float big = fbm(nd + offB, 3, 2.4f, 0.55f, n.seed + 17u);
    const float warpX = fbm(nd + offC, 3, 3.1f, 0.55f, n.seed + 31u) - 0.5f;
    const float warpY = fbm(nd + offA, 3, 2.7f, 0.55f, n.seed + 37u) - 0.5f;

    const Vec3 helper =
        std::fabs(n.center.y) > 0.9f ? Vec3(1.0f, 0.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);
    const Vec3 t1 = normalize(cross(n.center, helper));
    const Vec3 t2 = cross(n.center, t1);

    const float ax = std::atan2(dot(dir, t2), dot(dir, n.center));
    const float ay = std::atan2(dot(dir, t1), dot(dir, n.center));
    const float ca = std::cos(n.rotation);
    const float sa = std::sin(n.rotation);

    const float radius = std::max(0.05f, n.radius);
    float lx = (ax * ca + ay * sa) / n.stretchX - n.offsetX * radius;
    float ly = (-ax * sa + ay * ca) / n.stretchY - n.offsetY * radius;
    lx += warpX * n.warpAmount * radius;
    ly += warpY * n.warpAmount * radius;

    const float r = std::sqrt(lx * lx + ly * ly);
    const float theta = std::atan2(ly, lx);

    const float edgeNoise =
        fbm(Vec3(lx * (2.3f / radius), ly * (2.3f / radius), radius * 7.0f), 3, 1.7f, 0.55f,
            n.seed + 53u);
    const float rEff = r + (edgeNoise - 0.5f) * n.edgeAmount * radius;
    const float d = rEff / radius;

    const float filaments =
        ridged(fbm(nd * 3.4f + Vec3(warpX, warpY, 0.0f) * 1.8f + offA, 4, 2.3f, 0.55f,
                   n.seed + 43u));

    const Vec3 hot = unpackColor(n.colorHot) * (n.intensity * 0.9f);
    const Vec3 mid = unpackColor(n.colorMid) * (n.intensity * 0.9f);
    const Vec3 cool = unpackColor(n.colorCool) * (n.intensity * 0.9f);
    const Vec3 rim = unpackColor(n.colorRim) * (n.intensity * 0.9f);

    const float coreDx = lx - n.coreOffsetX * radius;
    const float coreDy = ly - n.coreOffsetY * radius;
    const float coreDist = std::sqrt(coreDx * coreDx + coreDy * coreDy);
    const float core = std::exp(-sqr(coreDist / std::max(0.08f, n.coreRadius * radius)));

    Vec3 color;
    float dust = 0.0f;

    if (n.type == NebulaType::Shell) {
        // Hollow shell with partial arcs and a bright core.
        const float ring =
            std::exp(-sqr((rEff - n.ringRadius * radius) / std::max(0.04f, n.ringWidth * radius)));
        float arcMask = 1.0f;
        if (n.arcCount > 0) {
            arcMask = 0.0f;
            for (int i = 0; i < n.arcCount; ++i) {
                const float da = std::fabs(wrapAngle(theta - n.arcCenter[i]));
                const float half = n.arcHalfWidth[i];
                const float edge = smoothstep((half - da) / (0.30f * half + 0.10f));
                arcMask = std::max(arcMask, edge);
            }
        }

        const float fil =
            std::pow(ridged(fbm(nd * 6.2f + Vec3(warpX, warpY, 0.0f) * 2.4f, 5, 1.6f, 0.55f,
                                n.seed + 61u)),
                     1.40f);
        const float fil2 =
            0.30f + 0.90f * ridged(fbm(nd * 12.0f + Vec3(warpX, warpY, 0.0f) * 3.2f, 4, 1.2f,
                                       0.55f, n.seed + 67u));
        const float halo = std::exp(-sqr(d * 0.95f));

        float density = ring * arcMask * (0.10f + 1.35f * fil * fil2) *
                            (0.55f + 0.55f * big) +
                        core * 0.55f * (0.4f + 0.6f * fine) + halo * 0.08f;

        const float dustCoord = lx * std::cos(n.dustAngle) + ly * std::sin(n.dustAngle);
        const float band = std::exp(-sqr(dustCoord / std::max(0.06f, n.dustWidth * radius)));
        dust = std::max(smoothstep((fine - 0.54f) / 0.16f),
                        smoothstep((big - 0.60f) / 0.14f) * 0.7f) *
               band * n.dustStrength * (0.4f + 0.6f * ring);
        density *= 1.0f - 0.72f * dust;

        color = rim * (ring * arcMask * fil * fil2 * 0.70f) +
                cool * (ring * arcMask * 0.28f + halo * 0.09f) + mid * (core * 0.95f) +
                hot * (core * core * 0.50f);
    } else if (n.type == NebulaType::Burst) {
        // Explosive remnant: radial jets, off-center lobes and a hot core.
        float jetSum = 0.0f;
        for (int i = 0; i < n.jetCount; ++i) {
            const float twist = (r / radius) * n.jetTwist[i];
            const float da = wrapAngle(theta - n.jetAngle[i] - twist);
            const float beam = std::exp(-sqr(da / std::max(0.05f, n.jetWidth[i])));
            const float radial =
                std::exp(-rEff / std::max(0.06f, n.jetLength[i] * radius));
            jetSum += n.jetWeight[i] * beam * radial;
        }
        const float beams =
            jetSum * (1.9f / std::sqrt(static_cast<float>(std::max(1, n.jetCount))));

        float lobeSum = 0.0f;
        for (int i = 0; i < n.lobeCount; ++i) {
            const float dx = lx - n.lobeX[i] * radius;
            const float dy = ly - n.lobeY[i] * radius;
            const float dl =
                std::sqrt(dx * dx + dy * dy) / std::max(0.05f, n.lobeRadius[i] * radius);
            lobeSum += n.lobeWeight[i] * std::exp(-2.1f * dl * dl);
        }

        const float rays =
            std::pow(ridged(fbm(nd * 6.5f + Vec3(warpX, warpY, 0.0f) * 3.0f, 4, 2.2f, 0.55f,
                                n.seed + 71u)),
                     1.45f);
        const float envelope = std::exp(-d * 1.5f) * smoothstep((1.35f - d) / 0.55f);
        const float redZone = smoothstep((d - 0.15f) / 0.50f);

        float density = core * 0.55f +
                        beams * (0.50f + 0.85f * rays) * (0.35f + 0.75f * fine) *
                            smoothstep((big - 0.32f) / 0.30f) +
                        lobeSum * 0.28f * (0.5f + 0.7f * fine);
        dust = smoothstep((big - 0.60f) / 0.14f) * envelope * 0.6f * n.dustStrength;
        density *= 1.0f - 0.55f * dust;

        color = hot * (core * 0.50f) +
                cool * (core * 0.40f + envelope * 0.40f + beams * 0.22f + lobeSum * 0.16f) +
                mid * (beams * redZone * 1.60f + lobeSum * 0.30f) +
                rim * (beams * 0.12f + lobeSum * 0.12f);
    } else {
        // Emission nebula: soft overlapping lobes and filamentary hot spots.
        float lobeSum = 0.0f;
        for (int i = 0; i < n.lobeCount; ++i) {
            const float dx = lx - n.lobeX[i] * radius;
            const float dy = ly - n.lobeY[i] * radius;
            const float dl =
                std::sqrt(dx * dx + dy * dy) / std::max(0.05f, n.lobeRadius[i] * radius);
            lobeSum += n.lobeWeight[i] * std::exp(-1.9f * dl * dl);
        }

        const float envelope = std::exp(-sqr(d * 1.05f)) * smoothstep((1.35f - d) / 0.55f);
        float density = lobeSum * 0.42f + envelope * 0.28f;
        density *= 0.42f + 0.90f * smoothstep((big - 0.40f) / 0.26f);
        density *= 0.40f + 0.95f * filaments;

        const float dustCoord = lx * std::cos(n.dustAngle) + ly * std::sin(n.dustAngle);
        const float band = std::exp(-sqr(dustCoord / std::max(0.06f, n.dustWidth * radius)));
        dust = std::max(smoothstep((fine - 0.55f) / 0.16f),
                        smoothstep((big - 0.62f) / 0.14f) * 0.65f) *
               band * n.dustStrength;
        density *= 1.0f - 0.70f * dust;

        const float hotSpots = smoothstep((filaments - 0.52f) / 0.30f) * density;
        const float coolPockets = smoothstep((0.60f - big) / 0.20f) * density;

        color = hot * (hotSpots * 0.95f) + mid * (density * 0.92f) +
                cool * (coolPockets * 0.80f) + rim * (density * 0.10f);
    }

    add = color;
    dustOut = saturate(dust * n.intensity);
}

// Separable sliding-window box blur (horizontal pass into tmp, then vertical). Edges
// are clamped, which is acceptable because the texture wraps only in longitude and the
// blur is used for a subtle warp effect, not as a precise filter.
void boxBlur(const std::vector<uint32_t>& src, std::vector<uint32_t>& dst, int w, int h, int radius) {
    std::vector<uint32_t> tmp(src.size());
    const int diameter = radius * 2 + 1;

    concurrency::parallel_for(0, h, [&](int y) {
        const uint32_t* row = src.data() + static_cast<size_t>(y) * static_cast<size_t>(w);
        uint32_t* out = tmp.data() + static_cast<size_t>(y) * static_cast<size_t>(w);

        int sr = 0;
        int sg = 0;
        int sb = 0;
        for (int k = -radius; k <= radius; ++k) {
            const int sx = std::min(std::max(k, 0), w - 1);
            const uint32_t c = row[sx];
            sr += static_cast<int>((c >> 16) & 0xFFu);
            sg += static_cast<int>((c >> 8) & 0xFFu);
            sb += static_cast<int>(c & 0xFFu);
        }

        for (int x = 0; x < w; ++x) {
            out[x] = packRgb(sr / diameter, sg / diameter, sb / diameter);

            const int addX = std::min(std::max(x + radius + 1, 0), w - 1);
            const int subX = std::min(std::max(x - radius, 0), w - 1);
            const uint32_t ca = row[addX];
            const uint32_t cs = row[subX];
            sr += static_cast<int>((ca >> 16) & 0xFFu) - static_cast<int>((cs >> 16) & 0xFFu);
            sg += static_cast<int>((ca >> 8) & 0xFFu) - static_cast<int>((cs >> 8) & 0xFFu);
            sb += static_cast<int>(ca & 0xFFu) - static_cast<int>(cs & 0xFFu);
        }
    });

    concurrency::parallel_for(0, w, [&](int x) {
        int sr = 0;
        int sg = 0;
        int sb = 0;
        for (int k = -radius; k <= radius; ++k) {
            const int sy = std::min(std::max(k, 0), h - 1);
            const uint32_t c = tmp[static_cast<size_t>(sy) * static_cast<size_t>(w) +
                                  static_cast<size_t>(x)];
            sr += static_cast<int>((c >> 16) & 0xFFu);
            sg += static_cast<int>((c >> 8) & 0xFFu);
            sb += static_cast<int>(c & 0xFFu);
        }

        for (int y = 0; y < h; ++y) {
            dst[static_cast<size_t>(y) * static_cast<size_t>(w) + static_cast<size_t>(x)] =
                packRgb(sr / diameter, sg / diameter, sb / diameter);

            const int addY = std::min(std::max(y + radius + 1, 0), h - 1);
            const int subY = std::min(std::max(y - radius, 0), h - 1);
            const uint32_t ca = tmp[static_cast<size_t>(addY) * static_cast<size_t>(w) +
                                   static_cast<size_t>(x)];
            const uint32_t cs = tmp[static_cast<size_t>(subY) * static_cast<size_t>(w) +
                                   static_cast<size_t>(x)];
            sr += static_cast<int>((ca >> 16) & 0xFFu) - static_cast<int>((cs >> 16) & 0xFFu);
            sg += static_cast<int>((ca >> 8) & 0xFFu) - static_cast<int>((cs >> 8) & 0xFFu);
            sb += static_cast<int>(ca & 0xFFu) - static_cast<int>(cs & 0xFFu);
        }
    });
}

} // namespace

NebulaParams makeNebula(uint32_t seed, int cycle, int avoidType) {
    // Cycle is mixed into the seed so each cycle has its own reproducible nebula.
    const cfg::Settings& settings = cfg::kSettings;
    Rng rng(seed ^ (static_cast<uint32_t>(cycle) * 0x9E3779B9u) ^ 0x51ED270Bu);

    // Build the list of enabled archetypes, then pick one. If only one type is enabled,
    // avoidType is ignored and repetition is unavoidable.
    int types[3] = {};
    int typeCount = 0;
    for (int i = 0; i < 3; ++i) {
        if ((settings.nebulaTypes & (1 << i)) != 0) {
            types[typeCount++] = i;
        }
    }
    if (typeCount == 0) {
        types[typeCount++] = 0;
    }

    int typeIndex = types[rng.rangeInt(0, typeCount - 1)];
    if (typeCount > 1 && typeIndex == avoidType) {
        int alternative = rng.rangeInt(0, typeCount - 2);
        if (types[alternative] == avoidType) {
            alternative = typeCount - 1;
        }
        typeIndex = types[alternative];
    }

    NebulaParams n;
    n.type = static_cast<NebulaType>(typeIndex);
    n.seed = rng.next();

    const Vec3 bandN = galaxyBandNormal();
    if (rng.unit() < 0.45f) {
        Vec3 best = rng.unitVector();
        float bestAbs = std::fabs(dot(best, bandN));
        for (int i = 0; i < 4; ++i) {
            const Vec3 candidate = rng.unitVector();
            const float candidateAbs = std::fabs(dot(candidate, bandN));
            if (candidateAbs < bestAbs) {
                best = candidate;
                bestAbs = candidateAbs;
            }
        }
        n.center = best;
    } else {
        n.center = rng.unitVector();
    }

    n.intensity = rng.range(0.55f, 1.15f) * settings.nebulaIntensity;
    n.noiseRotation = quatAxisAngle(rng.unitVector(), rng.range(0.0f, kTwoPi));
    n.stretchX = rng.range(0.65f, 1.45f);
    n.stretchY = rng.range(0.65f, 1.45f);
    n.rotation = rng.range(0.0f, kTwoPi);
    n.offsetX = rng.range(-0.22f, 0.22f);
    n.offsetY = rng.range(-0.22f, 0.22f);
    n.warpAmount = rng.range(0.18f, 0.50f) * settings.nebulaWarp;
    n.edgeAmount = rng.range(0.20f, 0.50f) * settings.nebulaEdge;
    n.coreOffsetX = rng.range(-0.28f, 0.28f);
    n.coreOffsetY = rng.range(-0.28f, 0.28f);
    n.coreRadius = rng.range(0.16f, 0.36f);
    n.dustAngle = rng.range(0.0f, kTwoPi);
    n.dustWidth = rng.range(0.16f, 0.55f);
    n.dustStrength = rng.range(0.50f, 1.00f) * settings.nebulaDust;

    // Hot/mid/cool/rim colors per palette. Each component gets a small random tint so
    // two cycles using the same palette still look distinct.
    static const uint32_t kEmissionPalettes[4][4] = {
        {0xFFFFD2BCu, 0xFFE05A48u, 0xFF5A78C8u, 0xFF8A6A9Au},
        {0xFFFFD6ECu, 0xFFE04890u, 0xFF6A58C8u, 0xFFC06A9Au},
        {0xFFFFE2B4u, 0xFFE08A40u, 0xFF4890C8u, 0xFF80B890u},
        {0xFFD8FFE8u, 0xFF40C090u, 0xFF4A60B0u, 0xFF90D0C0u},
    };
    static const uint32_t kShellPalettes[4][4] = {
        {0xFFFFCE9Au, 0xFFE05A38u, 0xFF3E9C86u, 0xFFA8B85Cu},
        {0xFFE8E0FFu, 0xFF5A78E0u, 0xFF40A0C8u, 0xFFA080E0u},
        {0xFFFFD8C8u, 0xFFE04858u, 0xFF6A60B0u, 0xFFC08080u},
        {0xFFD8FFF8u, 0xFF40B8B0u, 0xFF5070D0u, 0xFFA0E0D0u},
    };
    static const uint32_t kBurstPalettes[4][4] = {
        {0xFFF0FFFFu, 0xFFE6505Au, 0xFF5AA0FFu, 0xFFFFB4C8u},
        {0xFFE0FFFFu, 0xFFE050C0u, 0xFF58C8FFu, 0xFFFF90C8u},
        {0xFFFFF0D8u, 0xFFE0A040u, 0xFF5A80E0u, 0xFFFFE0A0u},
        {0xFFFFE8FFu, 0xFFE07840u, 0xFF8050E0u, 0xFFFFB080u},
    };

    const uint32_t(*palette)[4] = kEmissionPalettes;
    if (n.type == NebulaType::Shell) {
        palette = kShellPalettes;
    } else if (n.type == NebulaType::Burst) {
        palette = kBurstPalettes;
    }
    const int randomPalette = rng.rangeInt(0, 3);
    const int paletteIndex =
        settings.nebulaPalette >= 0 ? settings.nebulaPalette : randomPalette;
    const float tint = rng.range(0.10f, 0.20f);
    n.colorHot = tintedColor(rng, palette[paletteIndex][0], tint);
    n.colorMid = tintedColor(rng, palette[paletteIndex][1], tint);
    n.colorCool = tintedColor(rng, palette[paletteIndex][2], tint);
    n.colorRim = tintedColor(rng, palette[paletteIndex][3], tint);

    // Type-specific shape tables. Counts come from settings and are already clamped to
    // the capacities declared in NebulaParams.
    if (n.type == NebulaType::Shell) {
        n.radius = rng.range(0.16f, 0.34f) * settings.nebulaSize;
        n.ringRadius = rng.range(0.55f, 0.85f);
        n.ringWidth = rng.range(0.10f, 0.22f);
        n.arcCount =
            rng.rangeInt(settings.nebulaArcsMin, settings.nebulaArcsMax);
        const float arcStart = rng.range(0.0f, kTwoPi);
        for (int i = 0; i < n.arcCount; ++i) {
            n.arcCenter[i] =
                arcStart + static_cast<float>(i) * (kTwoPi / static_cast<float>(n.arcCount)) +
                rng.range(-0.55f, 0.55f);
            n.arcHalfWidth[i] = rng.range(0.40f, 1.25f);
        }
    } else if (n.type == NebulaType::Burst) {
        n.radius = rng.range(0.14f, 0.30f) * settings.nebulaSize;
        n.jetCount =
            rng.rangeInt(settings.nebulaJetsMin, settings.nebulaJetsMax);
        const float jetStart = rng.range(0.0f, kTwoPi);
        for (int i = 0; i < n.jetCount; ++i) {
            n.jetAngle[i] =
                jetStart + static_cast<float>(i) * (kTwoPi / static_cast<float>(n.jetCount)) +
                rng.range(-1.05f, 1.05f);
            n.jetWidth[i] = rng.range(0.07f, 0.34f);
            n.jetLength[i] = rng.range(0.45f, 1.35f);
            n.jetTwist[i] = rng.range(-2.20f, 2.20f);
            n.jetWeight[i] = rng.range(0.55f, 1.45f);
        }

        n.lobeCount =
            rng.rangeInt(settings.nebulaLobesMin, settings.nebulaLobesMax);
        const float lobeStart = rng.range(0.0f, kTwoPi);
        for (int i = 0; i < n.lobeCount; ++i) {
            const float angle =
                lobeStart + static_cast<float>(i) * (kTwoPi / static_cast<float>(n.lobeCount)) +
                rng.range(-0.90f, 0.90f);
            const float dist = rng.range(0.30f, 0.70f);
            n.lobeX[i] = std::cos(angle) * dist;
            n.lobeY[i] = std::sin(angle) * dist;
            n.lobeRadius[i] = rng.range(0.25f, 0.55f);
            n.lobeWeight[i] = rng.range(0.45f, 1.00f);
        }
    } else {
        n.radius = rng.range(0.18f, 0.42f) * settings.nebulaSize;
        n.lobeCount =
            rng.rangeInt(settings.nebulaLobesMin, settings.nebulaLobesMax);
        const float lobeStart = rng.range(0.0f, kTwoPi);
        for (int i = 0; i < n.lobeCount; ++i) {
            const float angle =
                lobeStart + static_cast<float>(i) * (kTwoPi / static_cast<float>(n.lobeCount)) +
                rng.range(-0.70f, 0.70f);
            const float dist = rng.range(0.15f, 0.55f);
            n.lobeX[i] = std::cos(angle) * dist;
            n.lobeY[i] = std::sin(angle) * dist;
            n.lobeRadius[i] = rng.range(0.32f, 0.75f);
            n.lobeWeight[i] = rng.range(0.60f, 1.25f);
        }
    }

    return n;
}

uint32_t Sky::sample(const std::vector<uint32_t>& tex, float u, float v) const {
    if (tex.empty() || width_ <= 0 || height_ <= 0) {
        return 0xFF000000u;
    }

    // Longitude wraps; latitude clamps at the poles to avoid sampling across the seam.
    u -= std::floor(u);
    v = clampf(v, 0.0f, 1.0f);

    const float fx = u * static_cast<float>(width_) - 0.5f;
    const float fy = v * static_cast<float>(height_) - 0.5f;

    const int x0 = static_cast<int>(std::floor(fx));
    const int y0 = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(x0);
    const float ty = fy - static_cast<float>(y0);

    int xa = x0 % width_;
    if (xa < 0) {
        xa += width_;
    }
    int xb = (x0 + 1) % width_;
    if (xb < 0) {
        xb += width_;
    }

    const int ya = std::min(std::max(y0, 0), height_ - 1);
    const int yb = std::min(std::max(y0 + 1, 0), height_ - 1);

    const uint32_t c00 = tex[static_cast<size_t>(ya) * static_cast<size_t>(width_) +
                            static_cast<size_t>(xa)];
    const uint32_t c10 = tex[static_cast<size_t>(ya) * static_cast<size_t>(width_) +
                            static_cast<size_t>(xb)];
    const uint32_t c01 = tex[static_cast<size_t>(yb) * static_cast<size_t>(width_) +
                            static_cast<size_t>(xa)];
    const uint32_t c11 = tex[static_cast<size_t>(yb) * static_cast<size_t>(width_) +
                            static_cast<size_t>(xb)];

    const float w00 = (1.0f - tx) * (1.0f - ty);
    const float w10 = tx * (1.0f - ty);
    const float w01 = (1.0f - tx) * ty;
    const float w11 = tx * ty;

    const auto mix = [&](int shift) -> int {
        const float a = static_cast<float>((c00 >> shift) & 0xFFu);
        const float b = static_cast<float>((c10 >> shift) & 0xFFu);
        const float c = static_cast<float>((c01 >> shift) & 0xFFu);
        const float d = static_cast<float>((c11 >> shift) & 0xFFu);
        return static_cast<int>(a * w00 + b * w10 + c * w01 + d * w11 + 0.5f);
    };

    return packRgb(mix(16), mix(8), mix(0));
}

void Sky::generate(uint32_t seed, const NebulaParams& nebula) {
    // This function is called on a worker thread when async sky is enabled. It must not
    // touch Scene state; everything it needs is passed in by value.
    width_ = cfg::kSkyWidth;
    height_ = cfg::kSkyHeight;
    sharp_.assign(static_cast<size_t>(width_) * static_cast<size_t>(height_), 0u);
    blurred_.assign(sharp_.size(), 0u);

    const Vec3 bandN = galaxyBandNormal();
    const Vec3 knotA = normalize(Vec3(-0.42f, 0.03f, 1.0f));
    const Vec3 knotB = normalize(Vec3(0.24f, 0.22f, 1.0f));
    const Vec3 knotC = normalize(Vec3(0.62f, -0.15f, 1.0f));
    const Vec3 offA(17.3f, -4.1f, 9.7f);
    const Vec3 offB(-8.2f, 13.4f, 3.9f);
    const Vec3 offC(4.7f, 21.8f, -12.6f);
    const Vec3 offD(-15.9f, -7.3f, 18.2f);
    const Vec3 offE(9.1f, 2.6f, -5.4f);

    concurrency::parallel_for(0, height_, [&](int y) {
        const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(height_);
        uint32_t* row = sharp_.data() + static_cast<size_t>(y) * static_cast<size_t>(width_);

        for (int x = 0; x < width_; ++x) {
            const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(width_);
            const Vec3 dir = equirectDirection(u, v);

            // Start with the Milky Way (narrow core plus wide halo) and add the
            // cycle's nebula on top.
            const float bandCoord = dot(dir, bandN);
            const float band = std::exp(-sqr(bandCoord / 0.17f));
            const float bandWide = std::exp(-sqr(bandCoord / 0.36f));

            const float large = fbm(dir + offA, 3, 1.9f, 0.55f, seed + 11u);
            const float cloud = fbm(dir + offB, 5, 4.2f, 0.55f, seed + 29u);
            const float fine = fbm(dir + offC, 4, 12.0f, 0.55f, seed + 47u);
            const float dust = fbm(dir + offD, 4, 6.0f, 0.55f, seed + 71u);
            const float tintNoise = fbm(dir + offE, 2, 2.7f, 0.5f, seed + 97u);

            const float patch = saturate((large - 0.38f) * 2.1f);
            const float clouds = saturate(cloud * 1.45f - 0.32f);

            float milkyWay = bandWide * (0.10f + 0.60f * band) * (0.22f + 0.95f * patch) *
                             (0.35f + 0.95f * clouds);
            milkyWay *= 0.55f + 0.65f * fine;

            const float dustMask = smoothstep((dust - 0.44f) / 0.22f) * (0.35f + 0.65f * band);
            milkyWay *= 1.0f - 0.88f * dustMask;

            const float knot =
                std::exp(-sqr((1.0f - dot(dir, knotA)) / 0.026f)) * 0.85f +
                std::exp(-sqr((1.0f - dot(dir, knotB)) / 0.011f)) * 0.45f +
                std::exp(-sqr((1.0f - dot(dir, knotC)) / 0.008f)) * 0.35f;

            const float haze = fbm(dir, 3, 1.1f, 0.55f, seed + 131u);

            float r = 2.5f + 2.0f * haze;
            float g = 4.0f + 3.2f * haze;
            float b = 9.0f + 5.5f * haze;

            r += milkyWay * (24.0f + 20.0f * fine) + knot * 20.0f;
            g += milkyWay * (40.0f + 28.0f * fine) + knot * 34.0f;
            b += milkyWay * (66.0f + 40.0f * fine) + knot * 54.0f;

            const float magenta = smoothstep((tintNoise - 0.58f) / 0.16f) * bandWide *
                                  (0.2f + 0.8f * milkyWay);
            r += magenta * 20.0f;
            g += magenta * 4.0f;
            b += magenta * 22.0f;

            Vec3 nebulaColor;
            float nebulaDust = 0.0f;
            nebulaSample(dir, nebula, nebulaColor, nebulaDust);

            const float dustMul = 1.0f - 0.45f * nebulaDust;
            r *= dustMul;
            g *= dustMul;
            b *= dustMul;

            r += nebulaColor.x;
            g += nebulaColor.y;
            b += nebulaColor.z;

            row[x] = packRgb(static_cast<int>(r), static_cast<int>(g), static_cast<int>(b));
        }
    });

    boxBlur(sharp_, blurred_, width_, height_, 5);
}

} // namespace sf
