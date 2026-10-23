#include "renderer.h"

#include <algorithm>
#include <cmath>

#include "config.h"

namespace sf {

namespace {

inline int packRgb(int r, int g, int b) {
    r = r < 0 ? 0 : (r > 255 ? 255 : r);
    g = g < 0 ? 0 : (g > 255 ? 255 : g);
    b = b < 0 ? 0 : (b > 255 ? 255 : b);
    return (0xFF << 24) | (r << 16) | (g << 8) | b;
}

inline void addChannel(int& dst, int value) {
    dst += value;
    if (dst > 255) {
        dst = 255;
    }
}

} // namespace

Renderer::Renderer() {
    for (int i = 0; i < kExpLutSize; ++i) {
        expLut_[i] = std::exp(-static_cast<float>(i) * 0.125f);
    }
}

void Renderer::init() {
}

void Renderer::resize(int clientWidth, int clientHeight) {
    if (clientWidth < 1) {
        clientWidth = 1;
    }
    if (clientHeight < 1) {
        clientHeight = 1;
    }

    clientWidth_ = clientWidth;
    clientHeight_ = clientHeight;

    int w = clientWidth;
    int h = clientHeight;

    // Cap internal resolution independently of the window. present() stretches the
    // result, trading sharpness for predictable software-rendering cost at 4K+.
    const double pixels = static_cast<double>(w) * static_cast<double>(h);
    if (pixels > static_cast<double>(cfg::kMaxRenderPixels)) {
        const double scale = std::sqrt(static_cast<double>(cfg::kMaxRenderPixels) / pixels);
        w = std::max(1, static_cast<int>(std::lround(w * scale)));
        h = std::max(1, static_cast<int>(std::lround(h * scale)));
    }

    if (fb_.width != w || fb_.height != h) {
        fb_.resize(w, h);
    }
}

void Renderer::clear(uint32_t color) {
    std::fill(fb_.pixels.begin(), fb_.pixels.end(), color);
}

void Renderer::addPixel(int x, int y, uint32_t color, float alpha) {
    // Additive blend with per-channel saturation. Alpha is the contribution strength,
    // not a destination blend factor; black pixels remain the empty sky.
    if (alpha <= 0.002f) {
        return;
    }
    if (static_cast<unsigned>(x) >= static_cast<unsigned>(fb_.width) ||
        static_cast<unsigned>(y) >= static_cast<unsigned>(fb_.height)) {
        return;
    }

    int a = static_cast<int>(alpha * 255.0f + 0.5f);
    if (a <= 0) {
        return;
    }
    if (a > 255) {
        a = 255;
    }

    uint32_t& dst = fb_.pixels[static_cast<size_t>(y) * static_cast<size_t>(fb_.width) +
                               static_cast<size_t>(x)];

    const int cr = static_cast<int>((color >> 16) & 0xFFu);
    const int cg = static_cast<int>((color >> 8) & 0xFFu);
    const int cb = static_cast<int>(color & 0xFFu);

    int r = static_cast<int>((dst >> 16) & 0xFFu);
    int g = static_cast<int>((dst >> 8) & 0xFFu);
    int b = static_cast<int>(dst & 0xFFu);

    addChannel(r, (cr * a + 127) / 255);
    addChannel(g, (cg * a + 127) / 255);
    addChannel(b, (cb * a + 127) / 255);

    dst = static_cast<uint32_t>(packRgb(r, g, b));
}

void Renderer::addSplat(float x, float y, uint32_t color, float alpha) {
    if (alpha <= 0.002f) {
        return;
    }

    const float fx = x - 0.5f;
    const float fy = y - 0.5f;
    const int ix = static_cast<int>(std::floor(fx));
    const int iy = static_cast<int>(std::floor(fy));
    const float tx = fx - static_cast<float>(ix);
    const float ty = fy - static_cast<float>(iy);

    const float w00 = (1.0f - tx) * (1.0f - ty);
    const float w10 = tx * (1.0f - ty);
    const float w01 = (1.0f - tx) * ty;
    const float w11 = tx * ty;

    addPixel(ix, iy, color, alpha * w00);
    addPixel(ix + 1, iy, color, alpha * w10);
    addPixel(ix, iy + 1, color, alpha * w01);
    addPixel(ix + 1, iy + 1, color, alpha * w11);
}

void Renderer::drawGlow(float x, float y, float sigma, uint32_t color, float alpha) {
    // Gaussian splat for stars and halos. Very small glows degrade to a 2x2 splat;
    // larger ones evaluate the LUT only inside ~4 sigma and skip the far tail.
    if (alpha <= 0.002f || sigma <= 0.0f) {
        return;
    }

    if (sigma < 0.85f) {
        addSplat(x, y, color, alpha);
        return;
    }

    const int radius = static_cast<int>(std::ceil(sigma * 3.0f));
    const int cx = static_cast<int>(std::floor(x));
    const int cy = static_cast<int>(std::floor(y));

    int x0 = std::max(0, cx - radius);
    int x1 = std::min(fb_.width - 1, cx + radius);
    int y0 = std::max(0, cy - radius);
    int y1 = std::min(fb_.height - 1, cy + radius);

    if (x0 > x1 || y0 > y1) {
        return;
    }

    const float inv = 1.0f / (2.0f * sigma * sigma);
    const int cr = static_cast<int>((color >> 16) & 0xFFu);
    const int cg = static_cast<int>((color >> 8) & 0xFFu);
    const int cb = static_cast<int>(color & 0xFFu);

    for (int py = y0; py <= y1; ++py) {
        const float dy = (static_cast<float>(py) + 0.5f) - y;
        const float dy2 = dy * dy;
        uint32_t* row = fb_.scanline(py);

        for (int px = x0; px <= x1; ++px) {
            const float dx = (static_cast<float>(px) + 0.5f) - x;
            float q = (dx * dx + dy2) * inv;
            if (q > 15.9f) {
                continue;
            }

            int idx = static_cast<int>(q * 8.0f);
            if (idx >= kExpLutSize) {
                idx = kExpLutSize - 1;
            }

            float a = alpha * expLut_[idx];
            if (a <= 0.002f) {
                continue;
            }

            int ai = static_cast<int>(a * 255.0f + 0.5f);
            if (ai <= 0) {
                continue;
            }
            if (ai > 255) {
                ai = 255;
            }

            uint32_t& dst = row[px];
            int r = static_cast<int>((dst >> 16) & 0xFFu);
            int g = static_cast<int>((dst >> 8) & 0xFFu);
            int b = static_cast<int>(dst & 0xFFu);

            addChannel(r, (cr * ai + 127) / 255);
            addChannel(g, (cg * ai + 127) / 255);
            addChannel(b, (cb * ai + 127) / 255);

            dst = static_cast<uint32_t>(packRgb(r, g, b));
        }
    }
}

void Renderer::drawLine(float x0, float y0, float x1, float y1, uint32_t color, float alpha,
                        bool soft) {
    // Used for warp streaks. `soft` uses bilinear splats for wide/anti-aliased trails;
    // the hard path is cheaper for the main segment. Callers clip long lines.
    if (alpha <= 0.002f) {
        return;
    }

    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = std::sqrt(dx * dx + dy * dy);

    if (len < 1.0f) {
        addSplat(x1, y1, color, alpha);
        return;
    }

    int steps = static_cast<int>(len);
    if (steps < 1) {
        steps = 1;
    }
    if (steps > 900) {
        steps = 900;
    }

    const float inv = 1.0f / static_cast<float>(steps);

    if (soft) {
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) * inv;
            addSplat(x0 + dx * t, y0 + dy * t, color, alpha);
        }
    } else {
        for (int i = 0; i <= steps; ++i) {
            const float t = static_cast<float>(i) * inv;
            addPixel(static_cast<int>(x0 + dx * t), static_cast<int>(y0 + dy * t), color, alpha);
        }
    }
}

void Renderer::drawSpikes(float x, float y, float length, uint32_t color, float alpha) {
    // Four-point diffraction cross with a fainter diagonal component for long spikes.
    if (alpha <= 0.002f || length < 1.0f) {
        return;
    }

    const int steps = std::max(1, static_cast<int>(length));
    for (int i = 1; i <= steps; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(steps);
        const float a = alpha * (1.0f - t) * (1.0f - t);
        const int ix = static_cast<int>(std::lround(x));
        const int iy = static_cast<int>(std::lround(y));

        addPixel(ix + i, iy, color, a);
        addPixel(ix - i, iy, color, a);
        addPixel(ix, iy + i, color, a);
        addPixel(ix, iy - i, color, a);

        if (length > 7.0f) {
            const float diag = a * 0.25f;
            addPixel(ix + i / 2, iy + i / 2, color, diag);
            addPixel(ix - i / 2, iy + i / 2, color, diag);
            addPixel(ix + i / 2, iy - i / 2, color, diag);
            addPixel(ix - i / 2, iy - i / 2, color, diag);
        }
    }
}

void Renderer::applyFade(float fade) {
    // Multiplicative RGB fade; used by callers that want to darken an existing frame.
    if (fade >= 0.999f) {
        return;
    }
    if (fade <= 0.001f) {
        clear(0xFF000000u);
        return;
    }

    for (uint32_t& px : fb_.pixels) {
        const int r = static_cast<int>(((px >> 16) & 0xFFu) * fade);
        const int g = static_cast<int>(((px >> 8) & 0xFFu) * fade);
        const int b = static_cast<int>((px & 0xFFu) * fade);
        px = static_cast<uint32_t>(packRgb(r, g, b));
    }
}

void Renderer::present(HDC dc, int dstWidth, int dstHeight) {
    if (dc == nullptr || fb_.width <= 0 || fb_.height <= 0) {
        return;
    }

    // Negative height means top-down rows, matching Framebuffer's memory order.
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = fb_.width;
    bmi.bmiHeader.biHeight = -fb_.height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    if (dstWidth == fb_.width && dstHeight == fb_.height) {
        SetDIBitsToDevice(dc, 0, 0, static_cast<DWORD>(fb_.width), static_cast<DWORD>(fb_.height),
                          0, 0, 0, static_cast<UINT>(fb_.height), fb_.pixels.data(), &bmi,
                          DIB_RGB_COLORS);
    } else {
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchDIBits(dc, 0, 0, dstWidth, dstHeight, 0, 0, fb_.width, fb_.height, fb_.pixels.data(),
                      &bmi, DIB_RGB_COLORS, SRCCOPY);
    }
}

} // namespace sf
