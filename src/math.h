#pragma once

#include <cmath>
#include <cstdint>

namespace sf {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 6.28318530717958647692f;

inline float clampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

inline float saturate(float v) { return clampf(v, 0.0f, 1.0f); }

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

inline float smoothstep(float t) {
    t = saturate(t);
    return t * t * (3.0f - 2.0f * t);
}

inline float sqr(float v) { return v * v; }

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    Vec3() = default;
    Vec3(float vx, float vy, float vz) : x(vx), y(vy), z(vz) {}
};

inline Vec3 operator+(const Vec3& a, const Vec3& b) { return Vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline Vec3 operator-(const Vec3& a, const Vec3& b) { return Vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline Vec3 operator*(const Vec3& a, float s) { return Vec3(a.x * s, a.y * s, a.z * s); }
inline Vec3 operator*(float s, const Vec3& a) { return a * s; }

inline float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 cross(const Vec3& a, const Vec3& b) {
    return Vec3(a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x);
}

inline float length(const Vec3& v) { return std::sqrt(dot(v, v)); }

inline Vec3 normalize(const Vec3& v) {
    const float len = length(v);
    if (len < 1.0e-8f) {
        return Vec3(0.0f, 0.0f, 1.0f);
    }
    const float inv = 1.0f / len;
    return Vec3(v.x * inv, v.y * inv, v.z * inv);
}

// Right-handed quaternion (w, x, y, z). quatMul(a, b) applies b first, then a when
// rotating a vector with quatRotate().
struct Quat {
    float w = 1.0f;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

inline Quat quatAxisAngle(const Vec3& axis, float angle) {
    const Vec3 n = normalize(axis);
    const float half = angle * 0.5f;
    const float s = std::sin(half);
    Quat q;
    q.w = std::cos(half);
    q.x = n.x * s;
    q.y = n.y * s;
    q.z = n.z * s;
    return q;
}

inline Quat quatMul(const Quat& a, const Quat& b) {
    Quat r;
    r.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
    r.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
    r.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
    r.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
    return r;
}

inline Quat quatConj(const Quat& q) {
    Quat r;
    r.w = q.w;
    r.x = -q.x;
    r.y = -q.y;
    r.z = -q.z;
    return r;
}

inline Quat quatNormalize(const Quat& q) {
    const float len = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    if (len < 1.0e-8f) {
        return Quat();
    }
    const float inv = 1.0f / len;
    Quat r;
    r.w = q.w * inv;
    r.x = q.x * inv;
    r.y = q.y * inv;
    r.z = q.z * inv;
    return r;
}

inline Quat quatSlerp(const Quat& a, const Quat& b, float t) {
    float cosOmega = a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z;
    Quat bb = b;
    if (cosOmega < 0.0f) {
        cosOmega = -cosOmega;
        bb.w = -bb.w;
        bb.x = -bb.x;
        bb.y = -bb.y;
        bb.z = -bb.z;
    }

    float k0;
    float k1;
    if (cosOmega > 0.9995f) {
        k0 = 1.0f - t;
        k1 = t;
    } else {
        const float omega = std::acos(clampf(cosOmega, -1.0f, 1.0f));
        const float sinOmega = std::sin(omega);
        k0 = std::sin((1.0f - t) * omega) / sinOmega;
        k1 = std::sin(t * omega) / sinOmega;
    }

    Quat r;
    r.w = a.w * k0 + bb.w * k1;
    r.x = a.x * k0 + bb.x * k1;
    r.y = a.y * k0 + bb.y * k1;
    r.z = a.z * k0 + bb.z * k1;
    return quatNormalize(r);
}

inline Quat quatFromTo(const Vec3& from, const Vec3& to) {
    const Vec3 f = normalize(from);
    const Vec3 t = normalize(to);
    const float d = clampf(dot(f, t), -1.0f, 1.0f);

    if (d > 0.9999f) {
        return Quat();
    }
    if (d < -0.9999f) {
        Vec3 axis = cross(f, Vec3(0.0f, 1.0f, 0.0f));
        if (length(axis) < 1.0e-5f) {
            axis = cross(f, Vec3(1.0f, 0.0f, 0.0f));
        }
        return quatAxisAngle(axis, kPi);
    }

    const Vec3 axis = cross(f, t);
    return quatAxisAngle(axis, std::acos(d));
}

inline Vec3 quatRotate(const Quat& q, const Vec3& v) {
    const Vec3 u(q.x, q.y, q.z);
    const Vec3 t = cross(u, v) * 2.0f;
    return v + t * q.w + cross(u, t);
}

// Small deterministic xorshift32 generator. Not cryptographic; used only for visuals.
// Seed 0 is remapped so a zero seed does not collapse the state.
struct Rng {
    uint32_t state;

    explicit Rng(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}

    uint32_t next() {
        uint32_t s = state;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        state = s;
        return s;
    }

    float unit() {
        return static_cast<float>(next() >> 8) * (1.0f / 16777216.0f);
    }

    float range(float lo, float hi) { return lo + (hi - lo) * unit(); }

    int rangeInt(int lo, int hi) {
        if (hi <= lo) {
            return lo;
        }
        return lo + static_cast<int>(next() % static_cast<uint32_t>(hi - lo + 1));
    }

    // Uniform point on the unit sphere via z/azimuth sampling.
    Vec3 unitVector() {
        const float z = range(-1.0f, 1.0f);
        const float a = range(0.0f, kTwoPi);
        const float r = std::sqrt(clampf(1.0f - z * z, 0.0f, 1.0f));
        return Vec3(r * std::cos(a), z, r * std::sin(a));
    }
};

} // namespace sf
