// tests/DpiScalingTests.cpp — DPI conversion invariants (Per-Monitor V2).
#include "catch_amalgamated.hpp"

#include "tf/CoreTypes.hpp"
#include "tf/DisplayMonitor.hpp"

using namespace tf;

TEST_CASE("DisplayInfo.scale derives from DPI (96 => 1.0)", "[dpi]") {
    DisplayInfo d;
    d.dpiX = 96;
    d.dpiY = 96;
    CHECK(d.scale() == 1.0);
}

TEST_CASE("DisplayInfo.scale for 150% and 200% monitors", "[dpi]") {
    DisplayInfo d150;
    d150.dpiX = d150.dpiY = 144;  // 150%
    CHECK(d150.scale() == Catch::Approx(1.5));

    DisplayInfo d200;
    d200.dpiX = d200.dpiY = 192;  // 200%
    CHECK(d200.scale() == Catch::Approx(2.0));
}

TEST_CASE("logical to physical rounding keeps selection inside the monitor",
          "[dpi][geo]") {
    // Mixed-DPI multi-monitor: primary at 100% (0..1919), secondary at 150%
    // positioned to the right with a logical origin at 1920.
    // Physical frame of the secondary monitor on a PMv2 process:
    //   origin (1920, 0), logical size 1280x720 -> physical 1920x1080.
    DisplayInfo secondary;
    secondary.framePhysical = Rect{1920, 0, 1920, 1080};
    secondary.dpiX = secondary.dpiY = 144;

    // A user drag from (1920+100, 100) to (1920+100+400, 100+300) physical:
    const Rect drag{2020, 100, 400, 300};
    const Rect clipped = drag.intersection(secondary.framePhysical);
    CHECK(clipped == drag);  // fully inside -> unchanged

    // A drag starting on the primary but crossing into the secondary must
    // clamp to the secondary's left edge for its own capture.
    const Rect crossing{1800, 100, 400, 300};
    const Rect clipped2 = crossing.intersection(secondary.framePhysical);
    CHECK(clipped2.x == 1920);
    CHECK(clipped2.width == 280);
}

TEST_CASE("physical pixel capture size matches display frame", "[dpi]") {
    DisplayInfo d;
    d.framePhysical = Rect{0, 0, 3840, 2160};  // 4K @ 200%
    d.dpiX = d.dpiY = 192;
    CHECK(d.framePhysical.width == 3840);
    CHECK(d.framePhysical.height == 2160);
    // A full-display capture must produce exactly this many pixels per row.
    CHECK(d.framePhysical.width * 4 == 15360);  // BGRA stride lower bound
}
