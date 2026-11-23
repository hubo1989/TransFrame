#pragma once
// Geometry.h — Core value geometry types mirroring CoreGraphics' CGPoint/CGSize/CGRect.
// Coordinate conventions are identical to the macOS implementation: bounding boxes
// are normalized (0..1) with a top-left origin unless stated otherwise.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace tf::core {

struct Point {
    double x = 0.0;
    double y = 0.0;

    friend bool operator==(const Point& a, const Point& b) noexcept {
        return a.x == b.x && a.y == b.y;
    }
    friend bool operator!=(const Point& a, const Point& b) noexcept { return !(a == b); }
};

struct Size {
    double width = 0.0;
    double height = 0.0;

    friend bool operator==(const Size& a, const Size& b) noexcept {
        return a.width == b.width && a.height == b.height;
    }
    friend bool operator!=(const Size& a, const Size& b) noexcept { return !(a == b); }
};

/// Axis-aligned rectangle. Semantics follow CGRect: origin + size; minX/minY/maxX/maxY
/// are the sorted edges (negative width/height are tolerated, matching CoreGraphics).
struct Rect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    constexpr Rect() = default;
    constexpr Rect(double px, double py, double w, double h) noexcept
        : x(px), y(py), width(w), height(h) {}
    constexpr Rect(const Point& origin, const Size& size) noexcept
        : x(origin.x), y(origin.y), width(size.width), height(size.height) {}

    double minX() const noexcept { return width < 0 ? x + width : x; }
    double minY() const noexcept { return height < 0 ? y + height : y; }
    double maxX() const noexcept { return width < 0 ? x : x + width; }
    double maxY() const noexcept { return height < 0 ? y : y + height; }
    double midX() const noexcept { return (minX() + maxX()) / 2.0; }
    double midY() const noexcept { return (minY() + maxY()) / 2.0; }

    Point origin() const noexcept { return Point{minX(), minY()}; }
    Size size() const noexcept { return Size{maxX() - minX(), maxY() - minY()}; }

    /// CGRect.isNull: the "no rect at all" sentinel (infinite origin, zero size).
    static const Rect& nullRect() noexcept {
        static const Rect r{1e308, 1e308, 0.0, 0.0};
        return r;
    }
    static constexpr Rect zero() noexcept { return Rect{0.0, 0.0, 0.0, 0.0}; }

    bool isNull() const noexcept { return x == nullRect().x && y == nullRect().y; }

    /// CGRect.intersects (standardized rects assumed).
    bool intersects(const Rect& other) const noexcept {
        return !(maxX() < other.minX() || other.maxX() < minX() ||
                 maxY() < other.minY() || other.maxY() < minY());
    }

    /// CGRect.contains(point).
    bool contains(const Point& p) const noexcept {
        return p.x >= minX() && p.x < maxX() && p.y >= minY() && p.y < maxY();
    }

    /// CGRect.union — returns nullRect when either side is null (CoreGraphics semantics).
    Rect unionWith(const Rect& other) const noexcept {
        if (isNull()) { return other; }
        if (other.isNull()) { return *this; }
        return Rect{
            std::min(minX(), other.minX()),
            std::min(minY(), other.minY()),
            std::max(maxX(), other.maxX()) - std::min(minX(), other.minX()),
            std::max(maxY(), other.maxY()) - std::min(minY(), other.minY())
        };
    }

    /// CGRect.insetBy (may produce inverted rects; callers rely on this for hit tests).
    Rect insetBy(double dx, double dy) const noexcept {
        return Rect{x + dx, y + dy, width - 2 * dx, height - 2 * dy};
    }

    friend bool operator==(const Rect& a, const Rect& b) noexcept {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    friend bool operator!=(const Rect& a, const Rect& b) noexcept { return !(a == b); }
};

inline double distance(const Point& a, const Point& b) noexcept {
    return std::hypot(a.x - b.x, a.y - b.y);
}

}  // namespace tf::core
