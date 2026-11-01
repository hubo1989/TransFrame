// tests/WindowDetectorTests.cpp
// Geometry + filtering + hit-test policy for FR-02, including the
// DWMWA_EXTENDED_FRAME_BOUNDS vs GetWindowRect delta case (Win10/11 invisible
// resize borders) and the cloaked-window filter.
#include "catch_amalgamated.hpp"

#include "tf/WindowDetector.hpp"

using namespace tf;

namespace {

class FakeLister final : public IWindowLister {
public:
    /// Shares the caller's vector so tests can mutate the live window set
    /// (simulating windows closing between mouse-down and mouse-move).
    explicit FakeLister(std::vector<WindowInfo>& windows) : windows_(windows) {}
    std::vector<WindowInfo> listTopmostFirst() override { return windows_; }

private:
    std::vector<WindowInfo>& windows_;
};

WindowInfo win(std::uint64_t hwnd, Rect frame, bool cloaked = false, double alpha = 1.0) {
    WindowInfo w;
    w.hwnd = hwnd;
    w.frame = frame;
    w.isCloaked = cloaked;
    w.alpha = alpha;
    w.windowName = L"win-" + std::to_wstring(hwnd);
    return w;
}

} // namespace

TEST_CASE("filterVisibleWindows drops cloaked, transparent and empty frames",
          "[window][filter]") {
    std::vector<WindowInfo> raw{
        win(1, Rect{0, 0, 100, 100}),                          // keep
        win(2, Rect{0, 0, 100, 100}, /*cloaked=*/true),        // drop: UWP suspended
        win(3, Rect{0, 0, 100, 100}, false, /*alpha=*/0.0),    // drop: invisible
        win(4, Rect{50, 50, 0, 0}),                            // drop: zero size
    };
    const auto filtered = filterVisibleWindows(std::move(raw));
    REQUIRE(filtered.size() == 1);
    CHECK(filtered[0].hwnd == 1);
}

TEST_CASE("windowUnderPoint returns topmost window at point", "[window][hit]") {
    // Topmost-first order: window 2 (hwnd=2) is ON TOP of window 1.
    const std::vector<WindowInfo> ordered{
        win(2, Rect{100, 100, 200, 200}),
        win(1, Rect{0, 0, 500, 500}),
    };
    const auto hit = windowUnderPoint(ordered, Point{150, 150});
    REQUIRE(hit.has_value());
    CHECK(hit->hwnd == 2);
}

TEST_CASE("windowUnderPoint misses when point is outside all frames", "[window][hit]") {
    const std::vector<WindowInfo> ordered{win(1, Rect{0, 0, 100, 100})};
    CHECK_FALSE(windowUnderPoint(ordered, Point{500, 500}).has_value());
    // Boundary semantics: [left, right) — bottom-right edge is outside.
    CHECK_FALSE(windowUnderPoint(ordered, Point{100, 100}).has_value());
    CHECK(windowUnderPoint(ordered, Point{99, 99}).has_value());
}

TEST_CASE("DWM extended frame bounds exclude the invisible resize border",
          "[window][dwm]") {
    // GetWindowRect on Win10/11 reports e.g. (-7,-7,807,607) for a visually
    // (0,0)-(800,600) window because of the 7px invisible resize borders.
    // FR-02 acceptance requires capture frames to match the *visible* bounds.
    const Rect getWindowRect{-7, -7, 814, 614};  // includes borders
    const Rect dwmBounds{0, 0, 800, 600};        // DWMWA_EXTENDED_FRAME_BOUNDS

    // A point at (3, 3) is inside the visible window…
    CHECK(dwmBounds.contains(Point{3, 3}));
    // …but *also* inside the raw GetWindowRect — using raw rects would
    // hit-test the invisible border strip and over-crop captures.
    CHECK(getWindowRect.contains(Point{-5, -5}));
    CHECK_FALSE(dwmBounds.contains(Point{-5, -5}));
}

TEST_CASE("windowUnderPoint prefers the topmost candidate even with overlap",
          "[window][hit]") {
    const std::vector<WindowInfo> ordered{
        win(10, Rect{50, 50, 100, 100}),
        win(9,  Rect{0, 0, 400, 400}),
        win(8,  Rect{0, 0, 400, 400}),
    };
    const auto hit = windowUnderPoint(ordered, Point{75, 75});
    REQUIRE(hit.has_value());
    CHECK(hit->hwnd == 10);
}

TEST_CASE("detector snapshot cache: mouse-down refresh, move reuses snapshot",
          "[window][cache]") {
    std::vector<WindowInfo> windows{win(1, Rect{0, 0, 100, 100})};
    FakeLister lister(windows);
    WindowDetectorService detector(lister);

    // First query with no snapshot: falls back to a fresh list.
    REQUIRE(detector.windowUnderPoint(Point{50, 50}).has_value());
    CHECK(detector.hasSnapshot());

    // Mutate the backing list WITHOUT refresh: cached snapshot still answers.
    windows.clear();
    REQUIRE(detector.windowUnderPoint(Point{50, 50}).has_value());

    // Refresh (mouse-down): the cache picks up the change.
    detector.refreshSnapshot();
    CHECK_FALSE(detector.windowUnderPoint(Point{50, 50}).has_value());
}

TEST_CASE("detector invalidate clears the snapshot", "[window][cache]") {
    std::vector<WindowInfo> windows{win(1, Rect{0, 0, 100, 100})};
    FakeLister lister(windows);
    WindowDetectorService detector(lister);
    detector.refreshSnapshot();
    REQUIRE(detector.hasSnapshot());
    detector.invalidate();
    CHECK_FALSE(detector.hasSnapshot());
}
