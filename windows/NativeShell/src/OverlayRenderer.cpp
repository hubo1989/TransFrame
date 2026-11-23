// OverlayRenderer.cpp — portable overlay state machine + paint list + the
// FR-26 translation-overlay strategy (pure color/layout/font decisions).
// No Win32/D2D here: the 120Hz path consumes paintList() on the render
// thread; only src/SelectionOverlay.cpp touches D2D/DComp, and the composite
// pixel pass lives in src/ImageExporter.cpp.
#include "tf/OverlayRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace tf {

void OverlayState::setDesktop(Rect desktop) {
    std::scoped_lock lock(mutex_);
    desktop_ = desktop;
}

void OverlayState::setMode(OverlayMode mode) {
    std::scoped_lock lock(mutex_);
    mode_ = mode;
    if (mode == OverlayMode::Idle) {
        selection_.reset();
        dragEngaged_ = false;
        downPoint_.reset();
    }
}

OverlayMode OverlayState::mode() const {
    std::scoped_lock lock(mutex_);
    return mode_;
}

void OverlayState::setHoverWindow(std::optional<Rect> frame) {
    std::scoped_lock lock(mutex_);
    hover_ = std::move(frame);
}

void OverlayState::beginTracking(Point anchor) {
    std::scoped_lock lock(mutex_);
    anchor_ = anchor;
    current_ = anchor;
    downPoint_ = anchor;
    dragEngaged_ = false;
    selection_.reset();
    mode_ = OverlayMode::Idle;
}

void OverlayState::moveTracking(Point current) {
    std::scoped_lock lock(mutex_);
    current_ = current;
    if (downPoint_ && !dragEngaged_) {
        const std::int64_t dx = current.x - downPoint_->x;
        const std::int64_t dy = current.y - downPoint_->y;
        // Drag threshold from the macOS spec (2.2): 4px discriminates a
        // click (window capture) from a drag (manual region).
        const std::int64_t distSq = dx * dx + dy * dy;
        const std::int64_t t = visuals_.dragThresholdPx;
        if (distSq >= t * t) {
            dragEngaged_ = true;
            mode_ = OverlayMode::Dragging;
            hover_.reset();  // FR-02: drag ignores window highlight
        }
    }
    if (dragEngaged_) {
        const std::int64_t x0 = anchor_.x, y0 = anchor_.y;
        const std::int64_t x1 = current_.x, y1 = current_.y;
        Rect sel;
        sel.x = std::min(x0, x1);
        sel.y = std::min(y0, y1);
        sel.width = std::abs(x1 - x0);
        sel.height = std::abs(y1 - y0);
        selection_ = sel;
    }
}

bool OverlayState::isDragEngaged() const {
    std::scoped_lock lock(mutex_);
    return dragEngaged_;
}

void OverlayState::endTracking() {
    std::scoped_lock lock(mutex_);
    downPoint_.reset();
    if (dragEngaged_ && selection_ && !selection_->empty()) {
        mode_ = OverlayMode::Confirmed;
    } else {
        // Click without drag: selection stays empty; caller decides between
        // window capture (hover rect) and cancel (no window under cursor).
        mode_ = OverlayMode::Idle;
    }
    dragEngaged_ = false;
}

void OverlayState::cancel() {
    std::scoped_lock lock(mutex_);
    mode_ = OverlayMode::Idle;
    selection_.reset();
    hover_.reset();
    dragEngaged_ = false;
    downPoint_.reset();
}

std::optional<Rect> OverlayState::selection() const {
    std::scoped_lock lock(mutex_);
    return selection_;
}

std::optional<Rect> OverlayState::hoverWindow() const {
    std::scoped_lock lock(mutex_);
    return hover_;
}

Rect OverlayState::normalizedSelection() const {
    if (!selection_) return {};
    Rect sel = *selection_;
    if (sel.width < 0) { sel.width = -sel.width; }
    if (sel.height < 0) { sel.height = -sel.height; }
    return sel;
}

PaintList OverlayState::paintList() const {
    std::scoped_lock lock(mutex_);
    PaintList pl;
    pl.desktop = desktop_;
    pl.dimRegion = desktop_;

    switch (mode_) {
        case OverlayMode::Idle:
            pl.crosshair = true;
            pl.crosshairAt = current_;
            if (hover_) {
                // Hovering a window: highlight it, keep crosshair (spec US-5:
                // desktop empty space shows crosshair only).
                pl.windowHighlight = hover_;
            }
            break;

        case OverlayMode::HoverWindow:
            pl.windowHighlight = hover_;
            pl.crosshair = true;
            pl.crosshairAt = current_;
            break;

        case OverlayMode::Dragging: {
            const Rect sel = normalizedSelection();
            pl.punchThrough = sel;
            pl.selectionStroke = sel;
            pl.crosshair = true;  // crosshair follows cursor while dragging
            pl.crosshairAt = current_;
            SizeBadge badge;
            badge.w = sel.width;
            badge.h = sel.height;
            // Badge above the selection top edge; D2D layer will clamp.
            badge.anchor = Rect{sel.x, std::max(desktop_.top(), sel.top() - 36),
                                std::max<std::int64_t>(sel.width, 80), 28};
            pl.badge = badge;
            break;
        }

        case OverlayMode::Confirmed: {
            const Rect sel = normalizedSelection();
            pl.punchThrough = sel;
            pl.selectionStroke = sel;
            SizeBadge badge;
            badge.w = sel.width;
            badge.h = sel.height;
            badge.anchor = Rect{sel.x, std::max(desktop_.top(), sel.top() - 36),
                                std::max<std::int64_t>(sel.width, 80), 28};
            pl.badge = badge;
            pl.toolbarSlots = layoutInPlaceToolbar(sel, desktop_, 8);
            pl.toolbarActions = {
                OverlayToolbarAction::Copy,     OverlayToolbarAction::Save,
                OverlayToolbarAction::Translate, OverlayToolbarAction::Pin,
                OverlayToolbarAction::ColorPick, OverlayToolbarAction::Cancel,
                std::nullopt, std::nullopt,  // reserved (OCR / upload, macOS parity)
            };
            // FR-06: panel content rides along while results are shown.
            if (panelVisible_) {
                constexpr std::int64_t kPanelW = 380;
                constexpr std::int64_t kPanelH = 320;
                pl.resultsPanel = layoutResultsPanel(sel, desktop_, kPanelW, kPanelH);
                pl.resultRows = resultRows_;
                pl.panelStatus = panelStatus_;
            }
            break;
        }
    }
    return pl;
}

void OverlayState::showResults(std::vector<OverlayTextRow> rows, std::string statusUtf8) {
    std::scoped_lock lock(mutex_);
    resultRows_ = std::move(rows);
    panelStatus_ = std::move(statusUtf8);
    panelVisible_ = true;
}

void OverlayState::showNotice(std::string noticeUtf8) {
    std::scoped_lock lock(mutex_);
    resultRows_.clear();
    panelStatus_ = std::move(noticeUtf8);
    panelVisible_ = true;
}

void OverlayState::dismissResults() {
    std::scoped_lock lock(mutex_);
    resultRows_.clear();
    panelStatus_.clear();
    panelVisible_ = false;
}

bool OverlayState::hasResults() const {
    std::scoped_lock lock(mutex_);
    return panelVisible_;
}

std::vector<Rect> layoutInPlaceToolbar(const Rect& selection, const Rect& desktop,
                                       std::size_t buttonCount) {
    // FR-05 layout skeleton: 8 slots (copy/save/translate/pin/color/cancel +
    // 2 reserved), 36px square buttons with 4px gaps, centered under the
    // selection, clamped into the desktop. Position only — no handlers.
    constexpr std::int64_t kButton = 36;
    constexpr std::int64_t kGap = 4;
    constexpr std::int64_t kMargin = 8;

    const std::int64_t count = static_cast<std::int64_t>(buttonCount);
    const std::int64_t totalWidth = count * kButton + (count - 1) * kGap;
    std::int64_t x = selection.x + (selection.width - totalWidth) / 2;
    std::int64_t y = selection.bottom() + kMargin;

    // Flip above when it would fall off the desktop bottom.
    if (y + kButton > desktop.bottom()) {
        y = selection.top() - kMargin - kButton;
    }
    // Clamp horizontally.
    x = std::max(desktop.left() + kMargin, std::min(x, desktop.right() - totalWidth - kMargin));
    y = std::max(desktop.top() + kMargin, y);

    std::vector<Rect> slots;
    slots.reserve(buttonCount);
    for (std::int64_t i = 0; i < count; ++i) {
        slots.push_back(Rect{x + i * (kButton + kGap), y, kButton, kButton});
    }
    return slots;
}

std::optional<OverlayToolbarAction> hitTestToolbar(const PaintList& pl, Point p) {
    const std::size_t n = std::min(pl.toolbarSlots.size(), pl.toolbarActions.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (pl.toolbarActions[i] && pl.toolbarSlots[i].contains(p)) return pl.toolbarActions[i];
    }
    return std::nullopt;
}

ResultsPanelLayout layoutResultsPanel(const Rect& selection, const Rect& desktop,
                                      std::int64_t panelWidth, std::int64_t panelHeight) {
    constexpr std::int64_t kGap = 12;
    constexpr std::int64_t kBelowToolbarClear = 52;  // toolbar row + margin
    // Clamp the requested size into the desktop (min 160px so a panel never
    // collapses to a sliver on tiny regions).
    panelWidth = std::clamp(panelWidth, std::int64_t{160},
                            std::max<std::int64_t>(160, desktop.width - 2 * kGap));
    panelHeight = std::clamp(panelHeight, std::int64_t{120},
                             std::max<std::int64_t>(120, desktop.height - 2 * kGap));

    ResultsPanelLayout out;
    std::int64_t x = 0, y = 0;
    if (selection.right() + kGap + panelWidth <= desktop.right()) {
        // Right side preferred (reading order: original left, translation right).
        out.dockedRight = true;
        x = selection.right() + kGap;
        y = std::clamp(selection.top(), desktop.top() + kGap,
                       desktop.bottom() - kGap - panelHeight);
    } else if (selection.left() - kGap - panelWidth >= desktop.left()) {
        out.dockedRight = false;
        out.dockedLeft = true;
        x = selection.left() - kGap - panelWidth;
        y = std::clamp(selection.top(), desktop.top() + kGap,
                       desktop.bottom() - kGap - panelHeight);
    } else {
        // Both sides blocked: below the selection, clearing the toolbar.
        out.dockedRight = false;
        out.dockedLeft = false;
        out.dockedBelow = true;
        x = std::clamp(selection.x, desktop.left() + kGap,
                       desktop.right() - kGap - panelWidth);
        y = selection.bottom() + kBelowToolbarClear;
        if (y + panelHeight > desktop.bottom() - kGap) {
            // Last resort: above the selection, clearing the flipped toolbar.
            y = selection.top() - kBelowToolbarClear - panelHeight;
            y = std::max(y, desktop.top() + kGap);
        }
    }
    out.panel = Rect{x, y, panelWidth, panelHeight};
    out.closeButton = Rect{out.panel.right() - 30, out.panel.top() + 4, 26, 26};
    out.contentRect = Rect{out.panel.x + 12, out.panel.y + 38,
                           out.panel.width - 24, out.panel.height - 38 - 10};
    if (out.contentRect.width < 0) out.contentRect.width = 0;
    if (out.contentRect.height < 0) out.contentRect.height = 0;
    return out;
}

// ---------------------------------------------------------------------------
// FR-26 strategy
// ---------------------------------------------------------------------------

float relativeLuminance(RgbColor c) {
    // WCAG relative luminance with the sRGB transfer approximation.
    auto channel = [](float v) {
        v = std::clamp(v, 0.f, 1.f);
        return v <= 0.03928f ? v / 12.92f : std::pow((v + 0.055f) / 1.055f, 2.4f);
    };
    return 0.2126f * channel(c.r) + 0.7152f * channel(c.g) + 0.0722f * channel(c.b);
}

RgbColor adaptiveTextColor(RgbColor background) {
    return relativeLuminance(background) >= 0.5f ? RgbColor{0.09f, 0.10f, 0.12f}   // dark text
                                                 : RgbColor{0.96f, 0.97f, 0.99f};  // light text
}

RgbColor overlayFillColor(RgbColor background) {
    // Blend the sampled background toward its own text polarity so the band
    // reads as a frosted version of the replaced region.
    const bool light = relativeLuminance(background) >= 0.5f;
    const RgbColor target = light ? RgbColor{1.f, 1.f, 1.f} : RgbColor{0.05f, 0.05f, 0.07f};
    constexpr float kBlend = 0.55f;
    return RgbColor{background.r * (1.f - kBlend) + target.r * kBlend,
                    background.g * (1.f - kBlend) + target.g * kBlend,
                    background.b * (1.f - kBlend) + target.b * kBlend};
}

RgbColor sampleRegionAverage(const std::uint8_t* bgra, std::int32_t stride,
                             std::int32_t imageWidth, std::int32_t imageHeight,
                             const Rect& pixelRect) {
    if (!bgra || stride <= 0 || imageWidth <= 0 || imageHeight <= 0) return RgbColor{1.f, 1.f, 1.f};
    const Rect area = pixelRect.intersection(
        Rect{0, 0, static_cast<std::int64_t>(imageWidth), static_cast<std::int64_t>(imageHeight)});
    if (area.empty()) return RgbColor{1.f, 1.f, 1.f};

    // Deterministic 4x4..8x8-ish grid: bounded sample count regardless of the
    // region size (the bbox can span a full-screen line).
    constexpr int kMaxSamplesPerAxis = 8;
    const std::int64_t stepsX = std::clamp(area.width, std::int64_t{1},
                                           std::int64_t{kMaxSamplesPerAxis});
    const std::int64_t stepsY = std::clamp(area.height, std::int64_t{1},
                                           std::int64_t{kMaxSamplesPerAxis});
    double sumB = 0, sumG = 0, sumR = 0;
    std::int64_t samples = 0;
    for (std::int64_t iy = 0; iy < stepsY; ++iy) {
        const std::int64_t y = area.top() + (2 * iy + 1) * area.height / (2 * stepsY);
        const auto row = bgra + static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
        for (std::int64_t ix = 0; ix < stepsX; ++ix) {
            const std::int64_t x = area.left() + (2 * ix + 1) * area.width / (2 * stepsX);
            const std::uint8_t* px = row + static_cast<std::size_t>(x) * 4;
            sumB += px[0];
            sumG += px[1];
            sumR += px[2];
            ++samples;
        }
    }
    if (samples == 0) return RgbColor{1.f, 1.f, 1.f};
    const double inv = 1.0 / (samples * 255.0);
    return RgbColor{static_cast<float>(sumR * inv), static_cast<float>(sumG * inv),
                    static_cast<float>(sumB * inv)};
}

namespace {

/// One UTF-8 code point at i; i advances past it (invalid byte -> U+FFFD, 1).
char32_t decodeUtf8(const std::string& s, std::size_t& i) {
    const auto byte = [&](std::size_t k) -> unsigned char {
        return k < s.size() ? static_cast<unsigned char>(s[k])
                            : static_cast<unsigned char>(0);
    };
    const unsigned char c = byte(i);
    if (c < 0x80) { ++i; return c; }
    if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x1F) << 6) | (byte(i + 1) & 0x3F);
        i += 2;
        return cp;
    }
    if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x0F) << 12) |
                            (static_cast<char32_t>(byte(i + 1) & 0x3F) << 6) |
                            (byte(i + 2) & 0x3F);
        i += 3;
        return cp;
    }
    if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
        const char32_t cp = (static_cast<char32_t>(c & 0x07) << 18) |
                            (static_cast<char32_t>(byte(i + 1) & 0x3F) << 12) |
                            (static_cast<char32_t>(byte(i + 2) & 0x3F) << 6) |
                            (byte(i + 3) & 0x3F);
        i += 4;
        return cp;
    }
    ++i;
    return 0xFFFD;
}

/// East-Asian-width "wide or fullwidth" classification (subset that matters
/// for translation overlays: CJK, Hangul, Kana, fullwidth forms).
bool isWideCodePoint(char32_t c) {
    return (c >= 0x1100 && c <= 0x115F) ||   // Hangul Jamo
           (c >= 0x2E80 && c <= 0xA4CF) ||   // CJK radicals .. Yi
           (c >= 0xA960 && c <= 0xA97F) ||
           (c >= 0xAC00 && c <= 0xD7FF) ||   // Hangul syllables
           (c >= 0xF900 && c <= 0xFAFF) ||   // CJK compat ideographs
           (c >= 0xFE10 && c <= 0xFE19) ||
           (c >= 0xFE30 && c <= 0xFE6F) ||
           (c >= 0xFF00 && c <= 0xFF60) ||   // fullwidth forms
           (c >= 0xFFE0 && c <= 0xFFE6) ||
           (c >= 0x1F300 && c <= 0x1FAFF) ||  // emoji
           (c >= 0x20000 && c <= 0x3FFFD);   // CJK ext B+
}

}  // namespace

double estimateTextWidthEm(const std::string& utf8) {
    double em = 0.0;
    std::size_t i = 0;
    while (i < utf8.size()) {
        const char32_t cp = decodeUtf8(utf8, i);
        if (cp == 0) continue;               // embedded NUL / trailing junk
        if (cp >= 0x0300 && cp <= 0x036F) continue;  // combining marks: 0 width
        em += isWideCodePoint(cp) ? 1.0 : 0.55;
    }
    return em;
}

OverlayDrawItem planOverlayItem(const OverlaySegment& segment, OverlayTextMode mode,
                                std::int32_t imageWidth, std::int32_t imageHeight,
                                const OverlayStyle& style,
                                const std::uint8_t* bgra, std::int32_t stride) {
    OverlayDrawItem item;
    item.fillAlpha = style.fillAlpha;

    const Rect imageRect{0, 0, static_cast<std::int64_t>(imageWidth),
                         static_cast<std::int64_t>(imageHeight)};
    Rect box = segment.pixelBox.intersection(imageRect);
    if (box.empty()) {
        // Degenerate (outside / zero-size) — report an empty, invisible item.
        item.fillRect = Rect{0, 0, 0, 0};
        item.textColor = RgbColor{0, 0, 0};
        item.fillColor = RgbColor{0, 0, 0};
        item.fillAlpha = 0;
        item.fontPx = style.minFontPx;
        return item;
    }

    // Geometry + base font size per mode.
    float fontPx;
    if (mode == OverlayTextMode::ReplaceOriginal) {
        item.fillRect = box;
        fontPx = static_cast<float>(box.height) * style.fontHeightFactor;
    } else {
        const std::int64_t bandHeight = std::max<std::int64_t>(
            static_cast<std::int64_t>(static_cast<float>(box.height) *
                                      style.appendBandFactor),
            static_cast<std::int64_t>(style.minFontPx * 1.6f));
        Rect band{box.x, box.bottom() + style.appendGapPx, box.width, bandHeight};
        band = band.intersection(imageRect);  // clamp at the image bottom
        if (band.empty()) {
            item.fillRect = Rect{0, 0, 0, 0};
            item.textColor = RgbColor{0, 0, 0};
            item.fillColor = RgbColor{0, 0, 0};
            item.fillAlpha = 0;
            item.fontPx = style.minFontPx;
            return item;
        }
        item.fillRect = band;
        fontPx = static_cast<float>(box.height) * style.appendHeightFactor;
    }
    fontPx = std::clamp(fontPx, style.minFontPx, style.maxFontPx);

    // Adaptive colors from the sampled segment background.
    const RgbColor bg = sampleRegionAverage(bgra, stride, imageWidth, imageHeight, box);
    item.fillColor = overlayFillColor(bg);
    item.textColor = adaptiveTextColor(bg);

    // Fit: shrink toward the floor, then flag truncation.
    constexpr float kInnerPadX = 3.f;
    const float innerWidth = static_cast<float>(item.fillRect.width) - 2.f * kInnerPadX;
    const double emWidth = estimateTextWidthEm(segment.text);
    if (innerWidth > 0 && emWidth > 0) {
        if (static_cast<double>(fontPx) * emWidth > static_cast<double>(innerWidth)) {
            const float shrink = static_cast<float>(
                static_cast<double>(innerWidth) / emWidth);
            fontPx = std::clamp(std::min(fontPx, shrink), style.minFontPx, style.maxFontPx);
            item.ellipsize = static_cast<double>(fontPx) * emWidth >
                             static_cast<double>(innerWidth);
        }
    } else if (innerWidth <= 0) {
        item.ellipsize = !segment.text.empty();
    }
    item.fontPx = fontPx;
    return item;
}

} // namespace tf
