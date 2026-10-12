#pragma once
// Enta: animated End-portal style starfield (white + purple stars that drift and twinkle).
// Works with any DrawUtil (Direct2D menus and the Minecraft-renderer HUD).
#include "util/DrawContext.h"
#include <chrono>
#include <cmath>
#include <algorithm>

namespace enta {
    inline float hash01(uint32_t x) {
        x ^= x >> 16;
        x *= 0x7feb352dU;
        x ^= x >> 15;
        x *= 0x846ca68bU;
        x ^= x >> 16;
        return static_cast<float>(x & 0xFFFFFF) / 16777215.f;
    }

    inline float secondsNow() {
        static const auto t0 = std::chrono::steady_clock::now();
        return std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
    }

    // count: number of stars, scale: pixel size multiplier, seed: different pattern per box
    inline void drawStars(DrawUtil& dc, d2d::Rect const& area, int count, float scale, uint32_t seed,
                          float alphaMul = 1.f) {
        float pad = 2.5f * scale;
        float w = area.right - area.left - pad * 2.f, h = area.bottom - area.top - pad * 2.f;
        if (w <= 2.f || h <= 2.f || count <= 0) return;
        float t = secondsNow();

        for (int i = 0; i < count; i++) {
            uint32_t base = seed * 2654435761U + static_cast<uint32_t>(i) * 97U;
            float fx = hash01(base + 1), fy = hash01(base + 2), fz = hash01(base + 3), fc = hash01(base + 4);

            // slow upward drift, wrapping around
            float y = fy - t * (0.010f + 0.025f * fz);
            y -= std::floor(y);
            float x = fx + 0.012f * std::sin(t * 0.4f + static_cast<float>(i));
            x -= std::floor(x);

            float twinkle = 0.5f + 0.5f * std::sin(t * (0.8f + 2.2f * fz) + static_cast<float>(i) * 1.7f);
            float edgeFade = std::clamp((std::min)(y, 1.f - y) * 5.f, 0.f, 1.f);
            float a = (0.15f + 0.85f * twinkle) * edgeFade * alphaMul;
            if (a <= 0.02f) continue;

            bool purple = fc < 0.45f;
            d2d::Color col = purple ? d2d::Color(0.74f, 0.50f, 1.f, a) : d2d::Color(1.f, 1.f, 1.f, a);
            float s = (0.7f + 1.5f * fz * fz) * scale;
            float cx = area.left + pad + x * w, cy = area.top + pad + y * h;

            if (fz > 0.7f) { // bigger stars get a soft glow
                float g = s * 2.6f;
                dc.fillRoundedRectangle({ cx - g, cy - g, cx + g, cy + g }, col.asAlpha(a * 0.16f), g);
            }
            dc.fillRoundedRectangle({ cx - s * 0.5f, cy - s * 0.5f, cx + s * 0.5f, cy + s * 0.5f }, col, s * 0.5f);
        }
    }

    inline uint32_t seedFrom(std::string const& s) {
        uint32_t h = 2166136261U;
        for (unsigned char c : s) h = (h ^ c) * 16777619U;
        return h;
    }
}
