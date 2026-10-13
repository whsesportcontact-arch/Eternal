#pragma once
// Tiny software renderer for a Minecraft skin: draws the player model (classic or slim arms, plus the
// outer layer) at any rotation into a pixel buffer. Portable C++17, no Windows or GPU needed.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace skin {

// pixels are 0xAARRGGBB
struct Image {
    int w = 0, h = 0;
    std::vector<uint32_t> px;

    [[nodiscard]] bool valid() const { return w > 0 && h > 0 && px.size() == static_cast<size_t>(w) * h; }
    [[nodiscard]] uint32_t at(int x, int y) const {
        if (x < 0 || y < 0 || x >= w || y >= h) return 0;
        return px[static_cast<size_t>(y) * w + x];
    }
};

// Standard skin sizes only: 64x64, the old 64x32, and HD 128x128.
inline bool isSupported(Image const& img) {
    return img.valid() && ((img.w == 64 && (img.h == 64 || img.h == 32)) || (img.w == 128 && img.h == 128));
}

inline bool isLegacy(Image const& img) { return img.h * 2 == img.w; }

// Slim ("Alex") skins leave part of the classic arm area empty.
inline bool isSlim(Image const& img) {
    if (isLegacy(img)) return false;
    const int s = img.w / 64;
    for (int y = 20 * s; y < 32 * s; y++)
        for (int x = 54 * s; x < 56 * s; x++)
            if ((img.at(x, y) >> 24) != 0) return false;
    return true;
}

namespace detail {
    struct V3 {
        float x, y, z;
    };
    inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
    inline V3 operator*(V3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }

    struct Target {
        int w, h;
        std::vector<uint32_t>& color;
        std::vector<float>& depth;
    };

    inline float edge(float ax, float ay, float bx, float by, float px, float py) {
        return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
    }

    // flat-colored triangle with a depth test (bigger z = closer)
    inline void triangle(Target& t, V3 a, V3 b, V3 c, uint32_t color) {
        float area = edge(a.x, a.y, b.x, b.y, c.x, c.y);
        if (std::fabs(area) < 1e-6f) return;
        const int minX = std::max(0, static_cast<int>(std::floor(std::min({ a.x, b.x, c.x }))));
        const int maxX = std::min(t.w - 1, static_cast<int>(std::ceil(std::max({ a.x, b.x, c.x }))));
        const int minY = std::max(0, static_cast<int>(std::floor(std::min({ a.y, b.y, c.y }))));
        const int maxY = std::min(t.h - 1, static_cast<int>(std::ceil(std::max({ a.y, b.y, c.y }))));
        const float inv = 1.f / area;

        for (int y = minY; y <= maxY; y++) {
            const float py = y + 0.5f;
            for (int x = minX; x <= maxX; x++) {
                const float px = x + 0.5f;
                float w0 = edge(b.x, b.y, c.x, c.y, px, py) * inv;
                float w1 = edge(c.x, c.y, a.x, a.y, px, py) * inv;
                float w2 = edge(a.x, a.y, b.x, b.y, px, py) * inv;
                // small tolerance closes hairline gaps between neighbouring texels
                if (w0 < -1e-4f || w1 < -1e-4f || w2 < -1e-4f) continue;
                const float z = w0 * a.z + w1 * b.z + w2 * c.z;
                const size_t i = static_cast<size_t>(y) * t.w + x;
                if (z <= t.depth[i]) continue;
                t.depth[i] = z;
                t.color[i] = color;
            }
        }
    }

    struct Camera {
        float cy, sy, cp, sp; // yaw / pitch cos+sin
        float scale, ox, oy;

        [[nodiscard]] V3 rotate(V3 p) const {
            // yaw around Y, then pitch around X
            float x = p.x * cy + p.z * sy;
            float z = -p.x * sy + p.z * cy;
            float y = p.y * cp - z * sp;
            z = p.y * sp + z * cp;
            return { x, y, z };
        }
        [[nodiscard]] V3 project(V3 p) const {
            V3 r = rotate(p);
            return { ox + r.x * scale, oy - r.y * scale, r.z };
        }
    };

    // One face of a box: texture rectangle (tu,tv,tw,th in 64-px units), a corner and two edge vectors
    // spanning the texture's columns (U) and rows (V).
    struct Face {
        int tu, tv, tw, th;
        V3 origin, U, V, normal;
        bool flipCols;
    };

    inline uint32_t shade(uint32_t c, float k) {
        auto ch = [&](int shift) {
            int v = static_cast<int>(((c >> shift) & 0xFF) * k);
            return static_cast<uint32_t>(std::clamp(v, 0, 255)) << shift;
        };
        return 0xFF000000u | ch(16) | ch(8) | ch(0);
    }

    inline void drawFace(Target& t, Camera const& cam, Image const& img, Face const& f, bool overlay) {
        const V3 n = cam.rotate(f.normal);
        if (n.z <= 0.f) return; // facing away

        // simple light from the front-top-right
        const float lx = 0.35f, ly = 0.6f, lz = 0.72f;
        const float diffuse = std::max(0.f, n.x * lx + n.y * ly + n.z * lz);
        const float k = 0.55f + 0.5f * diffuse;

        const int s = img.w / 64;
        const int cols = f.tw * s, rows = f.th * s;
        if (cols <= 0 || rows <= 0) return;
        const V3 du = f.U * (1.f / cols), dv = f.V * (1.f / rows);

        for (int j = 0; j < rows; j++) {
            for (int i = 0; i < cols; i++) {
                const int texCol = f.flipCols ? cols - 1 - i : i;
                uint32_t c = img.at(f.tu * s + texCol, f.tv * s + j);
                const uint32_t alpha = c >> 24;
                if (overlay && alpha < 128) continue; // outer layer: only solid pixels
                const V3 p0 = f.origin + du * static_cast<float>(i) + dv * static_cast<float>(j);
                const V3 a = cam.project(p0);
                const V3 b = cam.project(p0 + du);
                const V3 cc = cam.project(p0 + du + dv);
                const V3 d = cam.project(p0 + dv);
                const uint32_t col = shade(c, k);
                triangle(t, a, b, cc, col);
                triangle(t, a, cc, d, col);
            }
        }
    }

    // A box with Minecraft's texture layout starting at (u, v), sized w x h x d (texture pixels).
    inline void drawBox(Target& t, Camera const& cam, Image const& img, V3 mn, V3 mx, int u, int v, int w, int h,
                        int d, float inflate, bool overlay, bool mirror) {
        mn = mn + V3 { -inflate, -inflate, -inflate };
        mx = mx + V3 { inflate, inflate, inflate };
        const float X = mx.x - mn.x, Y = mx.y - mn.y, Z = mx.z - mn.z;

        // texture regions
        const int rightU = u, frontU = u + d, leftU = u + d + w, backU = u + 2 * d + w;
        // when mirrored (old skins' left limbs), the side textures swap places and everything flips
        const int sideAtMinX = mirror ? leftU : rightU;
        const int sideAtMaxX = mirror ? rightU : leftU;

        const Face faces[] = {
            // front (z max), columns x min -> max
            { frontU, v + d, w, h, { mn.x, mx.y, mx.z }, { X, 0, 0 }, { 0, -Y, 0 }, { 0, 0, 1 }, mirror },
            // back (z min), columns x max -> min
            { backU, v + d, w, h, { mx.x, mx.y, mn.z }, { -X, 0, 0 }, { 0, -Y, 0 }, { 0, 0, -1 }, mirror },
            // side at x min, columns z min -> max
            { sideAtMinX, v + d, d, h, { mn.x, mx.y, mn.z }, { 0, 0, Z }, { 0, -Y, 0 }, { -1, 0, 0 }, mirror },
            // side at x max, columns z max -> min
            { sideAtMaxX, v + d, d, h, { mx.x, mx.y, mx.z }, { 0, 0, -Z }, { 0, -Y, 0 }, { 1, 0, 0 }, mirror },
            // top (y max), rows z min -> max
            { u + d, v, w, d, { mn.x, mx.y, mn.z }, { X, 0, 0 }, { 0, 0, Z }, { 0, 1, 0 }, mirror },
            // bottom (y min)
            { u + d + w, v, w, d, { mn.x, mn.y, mn.z }, { X, 0, 0 }, { 0, 0, Z }, { 0, -1, 0 }, mirror },
        };
        for (auto const& f : faces) drawFace(t, cam, img, f, overlay);
    }
}

// Draws the player into `out` (outW x outH, 0xAARRGGBB, transparent background).
// yaw/pitch in degrees; yaw 0 faces the viewer.
inline void render(Image const& img, float yawDeg, float pitchDeg, int outW, int outH, std::vector<uint32_t>& out) {
    using namespace detail;
    out.assign(static_cast<size_t>(outW) * outH, 0);
    if (!isSupported(img) || outW <= 0 || outH <= 0) return;

    std::vector<float> depth(out.size(), -1e9f);
    Target t { outW, outH, out, depth };

    const float yaw = yawDeg * 3.14159265f / 180.f, pitch = pitchDeg * 3.14159265f / 180.f;
    Camera cam;
    cam.cy = std::cos(yaw);
    cam.sy = std::sin(yaw);
    cam.cp = std::cos(pitch);
    cam.sp = std::sin(pitch);
    // model is 32 units tall and about 16 wide; leave a margin
    cam.scale = std::min(outH / 36.f, outW / 20.f);
    cam.ox = outW * 0.5f;
    cam.oy = outH * 0.5f + 16.f * cam.scale; // model y 16 at the center

    const bool legacy = isLegacy(img);
    const bool slim = isSlim(img);
    const int armW = slim ? 3 : 4;

    struct BoxDef {
        V3 mn, mx;
        int u, v, w, h, d;
        int ou, ov; // outer layer texture (-1 = none)
        float inflate;
        bool mirror;
    };
    std::vector<BoxDef> boxes = {
        // head + hat
        { { -4, 24, -4 }, { 4, 32, 4 }, 0, 0, 8, 8, 8, 32, 0, 0.5f, false },
        // body + jacket
        { { -4, 12, -2 }, { 4, 24, 2 }, 16, 16, 8, 12, 4, legacy ? -1 : 16, 32, 0.25f, false },
        // right arm (viewer's left) + sleeve
        { { -4.f - armW, 12, -2 }, { -4, 24, 2 }, 40, 16, armW, 12, 4, legacy ? -1 : 40, 32, 0.25f, false },
        // right leg + pants
        { { -4, 0, -2 }, { 0, 12, 2 }, 0, 16, 4, 12, 4, legacy ? -1 : 0, 32, 0.25f, false },
    };
    if (legacy) {
        // old 64x32 skins reuse the right limbs' texture, mirrored
        boxes.push_back({ { 4, 12, -2 }, { 4.f + armW, 24, 2 }, 40, 16, armW, 12, 4, -1, 0, 0.f, true });
        boxes.push_back({ { 0, 0, -2 }, { 4, 12, 2 }, 0, 16, 4, 12, 4, -1, 0, 0.f, true });
    } else {
        boxes.push_back({ { 4, 12, -2 }, { 4.f + armW, 24, 2 }, 32, 48, armW, 12, 4, 48, 48, 0.25f, false });
        boxes.push_back({ { 0, 0, -2 }, { 4, 12, 2 }, 16, 48, 4, 12, 4, 0, 48, 0.25f, false });
    }

    for (auto const& b : boxes)
        drawBox(t, cam, img, b.mn, b.mx, b.u, b.v, b.w, b.h, b.d, 0.f, false, b.mirror);
    for (auto const& b : boxes)
        if (b.ou >= 0) drawBox(t, cam, img, b.mn, b.mx, b.ou, b.ov, b.w, b.h, b.d, b.inflate, true, b.mirror);
}

// render() at 2x and average down, for smooth edges
inline void renderSmooth(Image const& img, float yawDeg, float pitchDeg, int outW, int outH,
                         std::vector<uint32_t>& out) {
    std::vector<uint32_t> big;
    render(img, yawDeg, pitchDeg, outW * 2, outH * 2, big);
    out.assign(static_cast<size_t>(outW) * outH, 0);
    for (int y = 0; y < outH; y++) {
        for (int x = 0; x < outW; x++) {
            uint32_t a = 0, r = 0, g = 0, b = 0;
            for (int dy = 0; dy < 2; dy++) {
                for (int dx = 0; dx < 2; dx++) {
                    uint32_t c = big[static_cast<size_t>(y * 2 + dy) * (outW * 2) + (x * 2 + dx)];
                    uint32_t ca = c >> 24;
                    a += ca;
                    // premultiply so empty pixels don't darken the edges
                    r += ((c >> 16) & 0xFF) * ca / 255;
                    g += ((c >> 8) & 0xFF) * ca / 255;
                    b += (c & 0xFF) * ca / 255;
                }
            }
            a /= 4;
            r /= 4;
            g /= 4;
            b /= 4;
            out[static_cast<size_t>(y) * outW + x] = (a << 24) | (r << 16) | (g << 8) | b; // premultiplied
        }
    }
}

} // namespace skin
