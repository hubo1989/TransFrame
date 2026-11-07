// tf/Geo.hpp
// Geometry primitives shared by NativeShell services. Physical-pixel space
// (Per-Monitor V2 aware), origin = virtual-desktop top-left, Y grows down.
// Pure header: no Win32 / WinRT includes, unit-testable on any platform.
#ifndef TF_GEO_HPP
#define TF_GEO_HPP

#include <algorithm>
#include <cstdint>

namespace tf {

struct Point {
    std::int64_t x = 0;
    std::int64_t y = 0;
    bool operator==(const Point& o) const = default;
};

struct Size {
    std::int64_t width = 0;
    std::int64_t height = 0;
    bool operator==(const Size& o) const = default;
    bool empty() const { return width <= 0 || height <= 0; }
};

struct Rect {
    std::int64_t x = 0, y = 0, width = 0, height = 0;
    bool operator==(const Rect& o) const = default;

    std::int64_t left() const { return x; }
    std::int64_t top() const { return y; }
    std::int64_t right() const { return x + width; }
    std::int64_t bottom() const { return y + height; }

    bool empty() const { return width <= 0 || height <= 0; }
    bool contains(Point p) const {
        return p.x >= x && p.x < right() && p.y >= y && p.y < bottom();
    }

    Point origin() const { return {x, y}; }
    Size size() const { return {width, height}; }

    Rect intersection(const Rect& o) const {
        const std::int64_t l = std::max(left(), o.left());
        const std::int64_t t = std::max(top(), o.top());
        const std::int64_t r = std::min(right(), o.right());
        const std::int64_t b = std::min(bottom(), o.bottom());
        if (r <= l || b <= t) return {};
        return {l, t, r - l, b - t};
    }

    Rect normalized() const {
        Rect r{*this};
        if (r.width < 0) { r.x += r.width + 1; r.width = -r.width; }
        if (r.height < 0) { r.y += r.height + 1; r.height = -r.height; }
        return r;
    }

    /// Expand by d on all sides (clamped at 0).
    Rect inflated(std::int64_t d) const {
        Rect r{*this};
        r.x -= d; r.y -= d; r.width += 2 * d; r.height += 2 * d;
        if (r.width < 0) r.width = 0;
        if (r.height < 0) r.height = 0;
        return r;
    }
};

} // namespace tf

#endif // TF_GEO_HPP
