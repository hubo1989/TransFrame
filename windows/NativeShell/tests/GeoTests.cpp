// tests/GeoTests.cpp — shared geometry primitives.
#include "catch_amalgamated.hpp"

#include "tf/Geo.hpp"

using namespace tf;

TEST_CASE("Rect::contains uses [left, right) half-open semantics", "[geo]") {
    const Rect r{10, 10, 100, 100};
    CHECK(r.contains(Point{10, 10}));
    CHECK(r.contains(Point{109, 109}));
    CHECK_FALSE(r.contains(Point{110, 10}));  // right edge exclusive
    CHECK_FALSE(r.contains(Point{10, 110}));  // bottom edge exclusive
    CHECK_FALSE(r.contains(Point{9, 10}));
}

TEST_CASE("Rect::intersection handles overlap, touch and disjoint", "[geo]") {
    const Rect a{0, 0, 100, 100};
    CHECK(a.intersection(Rect{50, 50, 100, 100}) == Rect{50, 50, 50, 50});
    CHECK(a.intersection(Rect{100, 0, 50, 50}).empty());  // touching edge
    CHECK(a.intersection(Rect{200, 200, 50, 50}).empty());
    CHECK(a.intersection(a) == a);
}

TEST_CASE("Rect::inflated expands symmetrically and clamps at zero", "[geo]") {
    const Rect r{10, 10, 100, 100};
    const auto bigger = r.inflated(5);
    CHECK(bigger == Rect{5, 5, 110, 110});
    const auto gone = r.inflated(-1000);
    CHECK(gone.width == 0);
    CHECK(gone.height == 0);
}
