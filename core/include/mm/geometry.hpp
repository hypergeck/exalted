#pragma once
// Screen geometry and mouse coordinate mapping, independent of Win32.
#include <algorithm>
#include <cstdint>

namespace mm {

struct Rect {
    int32_t x = 0, y = 0, w = 0, h = 0;

    constexpr int32_t right() const noexcept { return x + w; }
    constexpr int32_t bottom() const noexcept { return y + h; }
    constexpr bool contains(int32_t px, int32_t py) const noexcept {
        return px >= x && py >= y && px < right() && py < bottom();
    }
    constexpr bool operator==(const Rect& o) const noexcept { return x == o.x && y == o.y && w == o.w && h == o.h; }
};

// A point expressed as a fraction of a window's client area, so it can be
// replayed into a client of a different size.
struct NormPoint {
    double u = 0.0, v = 0.0;
};

inline NormPoint normalize(const Rect& client, int32_t sx, int32_t sy) noexcept {
    const double w = client.w > 0 ? client.w : 1;
    const double h = client.h > 0 ? client.h : 1;
    return {(sx - client.x) / w, (sy - client.y) / h};
}

struct Point {
    int32_t x = 0, y = 0;
};

inline Point denormalize(const Rect& client, NormPoint p) noexcept {
    const double u = std::clamp(p.u, 0.0, 1.0);
    const double v = std::clamp(p.v, 0.0, 1.0);
    // Round to the nearest pixel and keep the result inside the client area.
    int32_t px = client.x + static_cast<int32_t>(u * client.w + 0.5);
    int32_t py = client.y + static_cast<int32_t>(v * client.h + 0.5);
    if (client.w > 0) px = std::min(px, client.right() - 1);
    if (client.h > 0) py = std::min(py, client.bottom() - 1);
    return {px, py};
}

// SendInput absolute coordinates: 0..65535 across the virtual screen,
// where 65535 maps to the last pixel column/row (MOUSEEVENTF_VIRTUALDESK).
struct AbsPoint {
    uint16_t ax = 0, ay = 0;
};

inline AbsPoint to_absolute(const Rect& virtual_screen, int32_t sx, int32_t sy) noexcept {
    const auto scale = [](int32_t v, int32_t origin, int32_t extent) -> uint16_t {
        if (extent <= 1) return 0;
        const double t = static_cast<double>(v - origin) / static_cast<double>(extent - 1);
        const double c = std::clamp(t, 0.0, 1.0) * 65535.0;
        return static_cast<uint16_t>(c + 0.5);
    };
    return {scale(sx, virtual_screen.x, virtual_screen.w), scale(sy, virtual_screen.y, virtual_screen.h)};
}

}  // namespace mm
