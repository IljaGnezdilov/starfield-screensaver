#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdint>
#include <vector>

#include "math.h"

namespace sf {

// Packed 0xAARRGGBB pixels. alpha is always treated as opaque; blending is done
// manually by the primitives. scanline() gives fast row access for render passes.
struct Framebuffer {
    int width = 0;
    int height = 0;
    std::vector<uint32_t> pixels;

    void resize(int w, int h) {
        width = w;
        height = h;
        pixels.assign(static_cast<size_t>(w) * static_cast<size_t>(h), 0u);
    }

    inline uint32_t* scanline(int y) {
        return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
    }

    inline const uint32_t* scanline(int y) const {
        return pixels.data() + static_cast<size_t>(y) * static_cast<size_t>(width);
    }
};

// Software rasterizer with additive blending. All coordinates are in framebuffer
// pixels; present() is the only place that knows about the destination window size.
class Renderer {
public:
    Renderer();

    void init();
    void resize(int clientWidth, int clientHeight);
    void present(HDC dc, int dstWidth, int dstHeight);

    Framebuffer& frame() { return fb_; }
    const Framebuffer& frame() const { return fb_; }

    int width() const { return fb_.width; }
    int height() const { return fb_.height; }

    void clear(uint32_t color);
    void addPixel(int x, int y, uint32_t color, float alpha);
    void addSplat(float x, float y, uint32_t color, float alpha);
    void drawGlow(float x, float y, float sigma, uint32_t color, float alpha);
    void drawLine(float x0, float y0, float x1, float y1, uint32_t color, float alpha, bool soft);
    void drawSpikes(float x, float y, float length, uint32_t color, float alpha);
    void applyFade(float fade);

private:
    // exp(-x) sampled at 1/8 steps for glow falloff; avoids exp() in the inner loop.
    static constexpr int kExpLutSize = 128;

    Framebuffer fb_;
    int clientWidth_ = 0;
    int clientHeight_ = 0;
    float expLut_[kExpLutSize] = {};
};

} // namespace sf
