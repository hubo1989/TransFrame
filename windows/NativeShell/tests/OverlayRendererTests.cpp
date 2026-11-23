// tests/OverlayRendererTests.cpp — overlay state machine + paint list
// (MT-safe pure logic; the D2D paint is a mechanical projection of this).
#include "catch_amalgamated.hpp"

#include "tf/OverlayRenderer.hpp"

#include <thread>

using namespace tf;

namespace {
constexpr Rect kDesktop{0, 0, 3840, 2160};
} // namespace

TEST_CASE("idle overlay shows crosshair only, no dim punch", "[overlay][idle]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.setMode(OverlayMode::Idle);
    st.beginTracking({100, 100});
    st.moveTracking({100, 100});

    const auto pl = st.paintList();
    CHECK(pl.crosshair);
    CHECK(pl.crosshairAt == Point{100, 100});
    CHECK_FALSE(pl.punchThrough.has_value());
    CHECK_FALSE(pl.selectionStroke.has_value());
    CHECK(pl.toolbarSlots.empty());
}

TEST_CASE("hover highlights the window under cursor until drag starts",
          "[overlay][hover]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    const Rect windowFrame{200, 200, 800, 600};
    st.setHoverWindow(windowFrame);

    auto pl = st.paintList();
    REQUIRE(pl.windowHighlight.has_value());
    CHECK(*pl.windowHighlight == windowFrame);
    CHECK(pl.crosshair);  // crosshair stays visible while hovering (spec US-5)

    // Drag engages past the 4px threshold -> highlight disappears (FR-02).
    st.beginTracking({250, 250});
    st.moveTracking({260, 260});  // 10px, 14.1 total > 4
    CHECK(st.isDragEngaged());
    pl = st.paintList();
    CHECK_FALSE(pl.windowHighlight.has_value());
}

TEST_CASE("drag below threshold stays a click (hand-shake tolerance)",
          "[overlay][threshold]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({500, 500});
    st.moveTracking({502, 502});  // ~2.8px < 4px threshold
    CHECK_FALSE(st.isDragEngaged());
    st.endTracking();
    CHECK(st.mode() == OverlayMode::Idle);  // treated as a click, no selection
}

TEST_CASE("drag paints punch-through, stroke and size badge", "[overlay][drag]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({100, 200});
    st.moveTracking({500, 400});  // 400x200 selection

    const auto pl = st.paintList();
    REQUIRE(pl.punchThrough.has_value());
    CHECK(*pl.punchThrough == Rect{100, 200, 400, 200});
    REQUIRE(pl.selectionStroke.has_value());
    CHECK(pl.selectionStroke == pl.punchThrough);
    REQUIRE(pl.badge.has_value());
    CHECK(pl.badge->w == 400);
    CHECK(pl.badge->h == 200);
    CHECK(pl.crosshair);
}

TEST_CASE("drag in any direction normalizes the selection", "[overlay][drag]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({500, 400});
    st.moveTracking({100, 200});  // up-left drag
    const auto pl = st.paintList();
    REQUIRE(pl.punchThrough.has_value());
    CHECK(*pl.punchThrough == Rect{100, 200, 400, 200});
}

TEST_CASE("mouse-up freezes selection into Confirmed with toolbar slots",
          "[overlay][confirm]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({100, 100});
    st.moveTracking({900, 500});
    st.endTracking();

    CHECK(st.mode() == OverlayMode::Confirmed);
    const auto pl = st.paintList();
    CHECK(pl.punchThrough.has_value());
    CHECK(pl.toolbarSlots.size() == 8);  // FR-05 layout skeleton
}

TEST_CASE("Esc cancels: back to idle, selection cleared", "[overlay][cancel]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({100, 100});
    st.moveTracking({500, 500});
    st.cancel();
    CHECK(st.mode() == OverlayMode::Idle);
    CHECK_FALSE(st.selection().has_value());
    CHECK_FALSE(st.paintList().punchThrough.has_value());
}

TEST_CASE("toolbar layout: below selection, clamped to desktop", "[overlay][toolbar]") {
    const Rect selection{100, 100, 200, 100};
    const auto slots = layoutInPlaceToolbar(selection, kDesktop, 8);
    REQUIRE(slots.size() == 8);
    // All inside the desktop.
    for (const auto& s : slots) {
        CHECK(s.top() >= kDesktop.top());
        CHECK(s.bottom() <= kDesktop.bottom());
        CHECK(s.left() >= kDesktop.left());
        CHECK(s.right() <= kDesktop.right());
    }
    // Row sits right below the selection, centered.
    const auto& first = slots.front();
    CHECK(first.top() == selection.bottom() + 8);
    const auto& last = slots.back();
    const auto center = selection.x + selection.width / 2;
    CHECK(first.left() <= center);
    CHECK(last.right() >= center);
}

TEST_CASE("toolbar layout flips above when below would overflow the desktop",
          "[overlay][toolbar]") {
    // Selection hugging the bottom edge of the desktop.
    const Rect selection{100, 2000, 300, 150};  // bottom = 2150, desktop h = 2160
    const auto slots = layoutInPlaceToolbar(selection, kDesktop, 8);
    REQUIRE_FALSE(slots.empty());
    // Row must be above the selection top and inside the desktop.
    CHECK(slots.front().bottom() <= selection.top());
    CHECK(slots.front().top() >= kDesktop.top());
}

TEST_CASE("state machine is usable from two threads without tearing",
          "[overlay][mt]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    volatile bool run = true;

    std::thread mutator([&] {
        for (int i = 0; i < 5000 && run; ++i) {
            st.beginTracking({100 + (i % 50), 100 + (i % 50)});
            st.moveTracking({500 + (i % 100), 500});
            st.endTracking();
        }
    });
    std::thread reader([&] {
        for (int i = 0; i < 5000 && run; ++i) {
            const auto pl = st.paintList();  // must never crash or deadlock
            (void)pl;
            const auto m = st.mode();
            (void)m;
        }
    });
    mutator.join();
    run = false;
    reader.join();
    SUCCEED("concurrent mutation + paintList is race-free");
}

TEST_CASE("visual defaults match the FR-01 spec", "[overlay][visuals]") {
    OverlayState st;
    CHECK(st.visuals().dimAlpha == 0.40f);
    CHECK(st.visuals().dragThresholdPx == 4);
    CHECK(st.visuals().windowFillAlpha == 0.25f);
}
