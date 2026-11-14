// OverlayRenderStrategyTests.cpp — FR-26 pure decisions (adaptive colors,
// background sampling, font fit, truncation, band geometry) plus FR-05
// toolbar hit testing and FR-06 panel docking. Everything here mirrors what
// the D2D pass in ImageExporter/SelectionOverlay consumes verbatim.
#include "catch_amalgamated.hpp"

#include "tf/OverlayRenderer.hpp"

using namespace tf;

namespace {
constexpr Rect kDesktop{0, 0, 1920, 1080};

/// 4x4 BGRA canvas filled with one color (stride = width*4).
struct BgraCanvas {
    std::int32_t w = 4, h = 4;
    std::vector<std::uint8_t> px;
    explicit BgraCanvas(std::uint8_t b, std::uint8_t g, std::uint8_t r,
                        std::int32_t width = 4, std::int32_t height = 4)
        : w(width), h(height), px(static_cast<std::size_t>(width) * height * 4, 0) {
        for (std::size_t i = 0; i + 3 < px.size(); i += 4) {
            px[i] = b;
            px[i + 1] = g;
            px[i + 2] = r;
            px[i + 3] = 255;
        }
    }
    const std::uint8_t* data() const { return px.data(); }
    std::int32_t stride() const { return w * 4; }
};
}  // namespace

// ------------------------------------------------------------------ colors ---

TEST_CASE("relative luminance orders dark and light", "[overlay][fr26]") {
    CHECK(relativeLuminance(RgbColor{0, 0, 0}) == Catch::Approx(0.f).margin(1e-4f));
    CHECK(relativeLuminance(RgbColor{1, 1, 1}) == Catch::Approx(1.f).margin(1e-3f));
    CHECK(relativeLuminance(RgbColor{1.f, 1.f, 1.f}) >
          relativeLuminance(RgbColor{0.5f, 0.5f, 0.5f}));
    // Green dominates over blue at the same channel value (BT.709 weights).
    CHECK(relativeLuminance(RgbColor{0, 1, 0}) > relativeLuminance(RgbColor{0, 0, 1}));
}

TEST_CASE("adaptive text color: dark text on light bg, light on dark",
          "[overlay][fr26]") {
    const RgbColor onWhite = adaptiveTextColor(RgbColor{1.f, 1.f, 1.f});
    CHECK(onWhite.r < 0.2f);
    CHECK(onWhite.g < 0.2f);
    CHECK(onWhite.b < 0.2f);

    const RgbColor onBlack = adaptiveTextColor(RgbColor{0.f, 0.f, 0.f});
    CHECK(onBlack.r > 0.8f);
    CHECK(onBlack.g > 0.8f);
    CHECK(onBlack.b > 0.8f);

    // Boundary sits at luminance 0.5.
    CHECK(adaptiveTextColor(RgbColor{0.9f, 0.9f, 0.9f}).r < 0.5f);
    CHECK(adaptiveTextColor(RgbColor{0.05f, 0.05f, 0.05f}).r > 0.5f);
}

TEST_CASE("overlay fill blends toward the text polarity", "[overlay][fr26]") {
    const RgbColor lightBg{1.f, 1.f, 1.f};
    const RgbColor fillLight = overlayFillColor(lightBg);
    CHECK(fillLight.r > 0.5f);  // light background keeps a light band

    const RgbColor darkBg{0.02f, 0.02f, 0.02f};
    const RgbColor fillDark = overlayFillColor(darkBg);
    CHECK(fillDark.r < 0.5f);   // dark background keeps a dark band
}

// ---------------------------------------------------------------- sampling ---

TEST_CASE("sampleRegionAverage reads the requested rect only", "[overlay][fr26]") {
    // Left half white, right half black.
    BgraCanvas canvas(255, 255, 255, 8, 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 4; x < 8; ++x) {
            std::uint8_t* p = canvas.px.data() + (y * 8 + x) * 4;
            p[0] = p[1] = p[2] = 0;
        }
    }
    const RgbColor left = sampleRegionAverage(canvas.data(), canvas.stride(), 8, 4,
                                              Rect{0, 0, 4, 4});
    CHECK(left.r == Catch::Approx(1.f).margin(1e-3f));
    const RgbColor right = sampleRegionAverage(canvas.data(), canvas.stride(), 8, 4,
                                               Rect{4, 0, 4, 4});
    CHECK(right.r == Catch::Approx(0.f).margin(1e-3f));

    // Pure red region samples red regardless of B/G.
    BgraCanvas red(0, 0, 255);
    const RgbColor sampled = sampleRegionAverage(red.data(), red.stride(), 4, 4,
                                                 Rect{0, 0, 4, 4});
    CHECK(sampled.r == Catch::Approx(1.f).margin(1e-3f));
    CHECK(sampled.g == Catch::Approx(0.f).margin(1e-3f));
}

TEST_CASE("sampleRegionAverage clamps out-of-image rects and null buffers",
          "[overlay][fr26]") {
    BgraCanvas black(0, 0, 0, 4, 4);
    const RgbColor inside = sampleRegionAverage(
        black.data(), black.stride(), 4, 4, Rect{-10, -10, 20, 20});
    CHECK(inside.r == Catch::Approx(0.f).margin(1e-3f));  // clamped to image

    const RgbColor noBuffer = sampleRegionAverage(nullptr, 0, 4, 4, Rect{0, 0, 4, 4});
    CHECK(noBuffer.r == Catch::Approx(1.f));  // documented white fallback
}

// ------------------------------------------------------------- width/fit -----

TEST_CASE("estimateTextWidthEm counts CJK wider than latin", "[overlay][fr26]") {
    CHECK(estimateTextWidthEm("hello") == Catch::Approx(5 * 0.55));
    CHECK(estimateTextWidthEm("你好") == Catch::Approx(2 * 1.0));
    CHECK(estimateTextWidthEm("") == Catch::Approx(0.0));
    // Mixed content is additive.
    CHECK(estimateTextWidthEm("你a") == Catch::Approx(1.55));
    // Fullwidth forms count as wide.
    CHECK(estimateTextWidthEm("Ａ") == Catch::Approx(1.0));
}

TEST_CASE("planOverlayItem replaces inside the bbox with adaptive colors",
          "[overlay][fr26]") {
    BgraCanvas white(255, 255, 255, 200, 100);
    OverlaySegment seg;
    seg.pixelBox = Rect{10, 10, 80, 24};
    seg.text = "译文";

    const OverlayDrawItem item = planOverlayItem(
        seg, OverlayTextMode::ReplaceOriginal, 200, 100, OverlayStyle{},
        white.data(), white.stride());
    CHECK(item.fillRect == seg.pixelBox);  // replace keeps the bbox
    CHECK(item.fillAlpha > 0.f);
    CHECK(item.textColor.r < 0.5f);        // dark text over light bg
    CHECK(item.fillColor.r > 0.5f);
    CHECK_FALSE(item.ellipsize);           // two CJK chars fit 80px easily
    // Font tracks the bbox height, clamped to the style bounds.
    CHECK(item.fontPx >= OverlayStyle{}.minFontPx);
    CHECK(item.fontPx <= OverlayStyle{}.maxFontPx);

    BgraCanvas dark(10, 10, 10, 200, 100);
    const OverlayDrawItem darkItem = planOverlayItem(
        seg, OverlayTextMode::ReplaceOriginal, 200, 100, OverlayStyle{},
        dark.data(), dark.stride());
    CHECK(darkItem.textColor.r > 0.5f);    // light text over dark bg
    CHECK(darkItem.fillColor.r < 0.5f);
}

TEST_CASE("planOverlayItem appends a band below the bbox", "[overlay][fr26]") {
    BgraCanvas white(255, 255, 255, 200, 100);
    OverlaySegment seg;
    seg.pixelBox = Rect{10, 10, 80, 20};
    seg.text = "text";

    const OverlayStyle style;
    const OverlayDrawItem item = planOverlayItem(
        seg, OverlayTextMode::AppendBelow, 200, 100, style, white.data(),
        white.stride());
    CHECK(item.fillRect.y >= seg.pixelBox.bottom() + style.appendGapPx);
    CHECK(item.fillRect.x == seg.pixelBox.x);
    CHECK(item.fillRect.width == seg.pixelBox.width);
    CHECK(item.fillRect.bottom() <= 100);  // clamped to the image

    // Band hugging the bottom edge gets clamped away completely.
    OverlaySegment edge;
    edge.pixelBox = Rect{10, 92, 80, 6};
    edge.text = "x";
    const OverlayDrawItem gone = planOverlayItem(
        edge, OverlayTextMode::AppendBelow, 200, 100, style, white.data(),
        white.stride());
    CHECK(gone.fillAlpha == 0.f);  // renderer skips it
}

TEST_CASE("planOverlayItem shrinks font then flags truncation", "[overlay][fr26]") {
    BgraCanvas white(255, 255, 255, 200, 100);
    OverlaySegment seg;
    seg.pixelBox = Rect{0, 40, 40, 30};  // 40px wide, tall enough for big font
    seg.text = "这是一段很长的译文文本根本放不下";

    const OverlayStyle style;
    const OverlayDrawItem item = planOverlayItem(
        seg, OverlayTextMode::ReplaceOriginal, 200, 100, style, white.data(),
        white.stride());
    // Estimated width (17em+) cannot fit 34px at any size >= minFontPx.
    CHECK(item.ellipsize);
    CHECK(item.fontPx == Catch::Approx(style.minFontPx));

    // Short text in the same box: no truncation, unclamped height font.
    OverlaySegment shortSeg;
    shortSeg.pixelBox = Rect{0, 40, 40, 30};
    shortSeg.text = "短";
    const OverlayDrawItem shortItem = planOverlayItem(
        shortSeg, OverlayTextMode::ReplaceOriginal, 200, 100, style, white.data(),
        white.stride());
    CHECK_FALSE(shortItem.ellipsize);
    CHECK(shortItem.fontPx == Catch::Approx(30 * style.fontHeightFactor));
}

TEST_CASE("planOverlayItem skips degenerate boxes safely", "[overlay][fr26]") {
    OverlaySegment seg;
    seg.pixelBox = Rect{-50, -50, 10, 10};  // fully outside
    seg.text = "x";
    const OverlayDrawItem item =
        planOverlayItem(seg, OverlayTextMode::ReplaceOriginal, 100, 100);
    CHECK(item.fillAlpha == 0.f);

    seg.pixelBox = Rect{0, 0, 0, 0};
    const OverlayDrawItem zero =
        planOverlayItem(seg, OverlayTextMode::ReplaceOriginal, 100, 100);
    CHECK(zero.fillAlpha == 0.f);
}

// ---------------------------------------------------------------- toolbar ----

TEST_CASE("confirmed paint list carries actions and hits test correctly",
          "[overlay][fr05]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({100, 100});
    st.moveTracking({900, 500});
    st.endTracking();

    const PaintList pl = st.paintList();
    REQUIRE(pl.toolbarSlots.size() == 8);
    REQUIRE(pl.toolbarActions.size() == 8);
    CHECK(*pl.toolbarActions[0] == OverlayToolbarAction::Copy);
    CHECK(*pl.toolbarActions[2] == OverlayToolbarAction::Translate);
    CHECK(*pl.toolbarActions[5] == OverlayToolbarAction::Cancel);
    CHECK_FALSE(pl.toolbarActions[6].has_value());  // reserved
    CHECK_FALSE(pl.toolbarActions[7].has_value());

    // Hit test: middle of the translate slot (index 2).
    const Rect& translateSlot = pl.toolbarSlots[2];
    const Point p{translateSlot.x + translateSlot.width / 2,
                  translateSlot.y + translateSlot.height / 2};
    const auto action = hitTestToolbar(pl, p);
    REQUIRE(action.has_value());
    CHECK(*action == OverlayToolbarAction::Translate);

    // Reserved slots and outside click: no action.
    const Rect& reserved = pl.toolbarSlots[6];
    CHECK_FALSE(hitTestToolbar(
        pl, {reserved.x + reserved.width / 2, reserved.y + reserved.height / 2})
                    .has_value());
    CHECK_FALSE(hitTestToolbar(pl, {5, 5}).has_value());
}

// ------------------------------------------------------------------ panel ----

TEST_CASE("results panel docks right when space allows", "[overlay][fr06]") {
    const Rect selection{100, 100, 400, 200};
    const auto layout = layoutResultsPanel(selection, kDesktop, 380, 320);
    CHECK(layout.dockedRight);
    CHECK(layout.panel.x == selection.right() + 12);
    CHECK(layout.panel.y == selection.top());
    CHECK(layout.panel.width == 380);
    // Close button sits inside the panel's top-right corner.
    CHECK(layout.closeButton.right() <= layout.panel.right());
    CHECK(layout.closeButton.top() >= layout.panel.top());
    // Content rect stays inside the panel.
    CHECK(layout.contentRect.x >= layout.panel.x);
    CHECK(layout.contentRect.bottom() <= layout.panel.bottom());
}

TEST_CASE("results panel flips left near the right desktop edge", "[overlay][fr06]") {
    const Rect selection{1500, 100, 400, 200};  // right side blocks docking
    const auto layout = layoutResultsPanel(selection, kDesktop, 380, 320);
    CHECK(layout.dockedLeft);
    CHECK_FALSE(layout.dockedRight);
    CHECK(layout.panel.right() <= selection.left());
    CHECK(layout.panel.left() >= kDesktop.left());
}

TEST_CASE("results panel drops below when both sides are blocked",
          "[overlay][fr06]") {
    // Selection spans nearly the whole desktop width.
    const Rect selection{0, 200, 1900, 300};
    const auto layout = layoutResultsPanel(selection, kDesktop, 380, 320);
    CHECK(layout.dockedBelow);
    // Below placement clears the in-place toolbar row.
    CHECK(layout.panel.y >= selection.bottom() + 52);
    CHECK(layout.panel.bottom() <= kDesktop.bottom());
    CHECK(layout.panel.left() >= kDesktop.left());
    CHECK(layout.panel.right() <= kDesktop.right());
}

TEST_CASE("results panel clamps into small desktops", "[overlay][fr06]") {
    const Rect tiny{0, 0, 400, 300};
    const auto layout = layoutResultsPanel(tiny, tiny, 380, 320);
    CHECK(layout.panel.width <= 400 - 24);
    CHECK(layout.panel.height <= 300 - 24);
    CHECK(layout.panel.right() <= tiny.right());
    CHECK(layout.panel.bottom() <= tiny.bottom());
}

TEST_CASE("overlay state exposes results panel content", "[overlay][fr06]") {
    OverlayState st;
    st.setDesktop(kDesktop);
    st.beginTracking({100, 100});
    st.moveTracking({900, 500});
    st.endTracking();

    CHECK_FALSE(st.paintList().resultsPanel.has_value());
    st.showResults({OverlayTextRow{"hello", "你好"}}, "OCR 20ms");
    auto pl = st.paintList();
    REQUIRE(pl.resultsPanel.has_value());
    REQUIRE(pl.resultRows.size() == 1);
    CHECK(pl.resultRows[0].source == "hello");
    CHECK(pl.resultRows[0].translated == "你好");
    CHECK(pl.panelStatus == "OCR 20ms");
    // Toolbar stays available beside the panel.
    CHECK(pl.toolbarSlots.size() == 8);

    st.showNotice("占位");
    pl = st.paintList();
    REQUIRE(pl.resultsPanel.has_value());
    CHECK(pl.resultRows.empty());
    CHECK(pl.panelStatus == "占位");

    st.dismissResults();
    CHECK_FALSE(st.paintList().resultsPanel.has_value());
}
