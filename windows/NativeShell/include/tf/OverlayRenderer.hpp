// tf/OverlayRenderer.hpp
// Selection-overlay rendering model (FR-01/02, FR-05 layout skeleton).
// Pure state + paint-list computation; the D2D1.1/DComp device plumbing is in
// src/SelectionOverlay.cpp. Keeping the state machine here makes the
// 120Hz repaint path unit-testable (<= 8ms budget) and MT-safe by construction:
//  - UI thread mutates state under std::mutex
//  - render thread snapshots and builds a paint list without Win32 calls
//
// Rendering contract (visual spec, macOS SelectionOverlayWindow parity):
//   whole virtual desktop darkened 40% black;
//   selection rect "punched through" (undimmed) + 1px accent stroke;
//   size badge near selection edge; crosshair lines when idle;
//   hovered-window highlight (accent fill 25% + 1px stroke) until drag starts.
#ifndef TF_OVERLAY_RENDERER_HPP
#define TF_OVERLAY_RENDERER_HPP

#include "CoreTypes.hpp"
#include "Geo.hpp"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tf {

enum class OverlayMode : std::uint8_t {
    Idle,        // crosshair only, no selection yet
    HoverWindow, // window under cursor highlighted (FR-02)
    Dragging,    // manual rect selection in progress (FR-01)
    Confirmed,   // selection done; in-place toolbar shown (FR-05)
};

struct OverlayVisuals {
    float dimAlpha = 0.40f;                 // 40% black over whole desktop (FR-01)
    float accentR = 0.00f, accentG = 0.47f, accentB = 0.94f;  // system accent blue
    float windowFillAlpha = 0.25f;          // hovered-window tint
    std::int64_t dragThresholdPx = 4;       // click vs drag discriminator (spec 2.2)
};

struct SizeBadge {
    std::int64_t w = 0, h = 0;
    Rect anchor;                 // badge rect in physical px (D2D draws text inside)
};

// ---- FR-05: in-place toolbar actions (slot 0..5; slots 6..7 reserved) ----
enum class OverlayToolbarAction : std::uint8_t {
    Copy = 0,
    Save,
    Translate,
    Pin,        // placeholder until the pin manager lands (other worktree)
    ColorPick,  // placeholder until FR-32
    Cancel,
};

// ---- FR-06: in-place results panel (source/translation pairs) -----------
struct OverlayTextRow {
    std::string source;      // UTF-8
    std::string translated;  // UTF-8
};

struct ResultsPanelLayout {
    Rect panel;          // docked panel rect (physical px, screen space)
    Rect closeButton;    // 26x26 close target at the panel's top-right
    Rect contentRect;    // text area inside padding + header
    bool dockedRight = true;
    bool dockedLeft = false;
    bool dockedBelow = false;
};

struct PaintList {
    Rect desktop;                // virtual desktop bounds (physical px)
    Rect dimRegion = {};         // = desktop; kept explicit for the punch-out pass
    std::optional<Rect> punchThrough;      // undimmed selection
    std::optional<Rect> selectionStroke;   // 1px accent outline
    std::optional<Rect> windowHighlight;   // hovered window (fill+stroke)
    bool crosshair = false;
    Point crosshairAt{};
    std::optional<SizeBadge> badge;
    // FR-05 toolbar: geometry slots + parallel action ids (nullopt = reserved).
    std::vector<Rect> toolbarSlots;
    std::vector<std::optional<OverlayToolbarAction>> toolbarActions;
    // FR-06 results panel; nullopt while no results/notice are shown.
    std::optional<ResultsPanelLayout> resultsPanel;
    std::vector<OverlayTextRow> resultRows;
    std::string panelStatus;      // header line (UTF-8)
};

/// Thread-safe overlay state machine.
class OverlayState {
public:
    explicit OverlayState(OverlayVisuals visuals = {}) : visuals_(visuals) {}

    void setDesktop(Rect desktop);
    void setMode(OverlayMode mode);
    OverlayMode mode() const;

    void setHoverWindow(std::optional<Rect> frame);
    void beginTracking(Point anchor);
    void moveTracking(Point current);    // hover + crosshair position; drag when active
    /// Returns true when movement crossed the drag threshold (drag engaged).
    bool isDragEngaged() const;
    void endTracking();                  // mouse-up: freeze selection
    void cancel();                       // Esc: back to idle, selection cleared

    std::optional<Rect> selection() const;
    std::optional<Rect> hoverWindow() const;
    Point anchor() const { return anchor_; }
    Point current() const { return current_; }

    // ---- FR-06: in-place results panel state (thread-safe like the rest) --
    /// Show (or replace) the results panel content. Switches the Confirmed
    /// paint list to include the docked panel. No-op in other modes is fine:
    /// the data is simply not painted until Confirmed.
    void showResults(std::vector<OverlayTextRow> rows, std::string statusUtf8);
    /// Panel with a single notice line (placeholder actions, errors).
    void showNotice(std::string noticeUtf8);
    /// Drop the panel; toolbar-only Confirmed view returns.
    void dismissResults();
    bool hasResults() const;

    /// Compute the paint list (called by the render thread each frame).
    PaintList paintList() const;

    const OverlayVisuals& visuals() const { return visuals_; }

private:
    Rect normalizedSelection() const;

    mutable std::mutex mutex_;
    OverlayVisuals visuals_;
    OverlayMode mode_ = OverlayMode::Idle;
    Rect desktop_{};
    std::optional<Rect> hover_;
    std::optional<Rect> selection_;
    Point anchor_{};
    Point current_{};
    bool dragEngaged_ = false;
    std::optional<Point> downPoint_;
    // FR-06 panel content (empty rows = notice-only when status set).
    std::vector<OverlayTextRow> resultRows_;
    std::string panelStatus_;
    bool panelVisible_ = false;
};

/// FR-05 layout skeleton: toolbar button rects below/above selection edge,
/// clamped to desktop. Pure geometry, ready for later task to attach commands.
std::vector<Rect> layoutInPlaceToolbar(const Rect& selection, const Rect& desktop,
                                       std::size_t buttonCount);

/// FR-05 hit test: which toolbar button (if any) contains the point.
std::optional<OverlayToolbarAction> hitTestToolbar(const PaintList& pl, Point p);

/// FR-06: dock the results panel beside the selection (right preferred, then
/// left, then below clearing the toolbar), clamped into the desktop.
/// Pure geometry.
ResultsPanelLayout layoutResultsPanel(const Rect& selection, const Rect& desktop,
                                      std::int64_t panelWidth, std::int64_t panelHeight);

// ---------------------------------------------------------------------------
// FR-26: translation overlay strategy — pure decisions (colors, geometry,
// font fit, truncation); the D2D pixel pass consumes OverlayDrawItem.
// ---------------------------------------------------------------------------

enum class OverlayTextMode : std::uint8_t {
    ReplaceOriginal,  // in-place: fill the segment bbox, draw translation inside
    AppendBelow,      // append a translation band below each segment line
};

struct RgbColor {
    float r = 0.f, g = 0.f, b = 0.f;
    friend bool operator==(const RgbColor&, const RgbColor&) = default;
};

/// WCAG-style relative luminance (0 = dark .. 1 = light).
float relativeLuminance(RgbColor c);

/// FR-26 adaptive text color: dark text on light backgrounds, light text on
/// dark ones.
RgbColor adaptiveTextColor(RgbColor background);

/// Semi-transparent base fill color derived from the sampled background
/// (keeps the overlay looking native over the region it replaces).
RgbColor overlayFillColor(RgbColor background);

/// Mean of a grid sample over a pixel rect (BGRA8, top-down rows, stride
/// bytes). Samples outside the image are skipped. Pure function of the bytes;
/// null buffer returns white.
RgbColor sampleRegionAverage(const std::uint8_t* bgra, std::int32_t stride,
                             std::int32_t imageWidth, std::int32_t imageHeight,
                             const Rect& pixelRect);

/// Rough display width in em units: CJK/fullwidth glyphs ~1.0em, combining
/// marks 0, everything else ~0.55em (matches DWrite metrics closely enough
/// for fit/truncation decisions).
double estimateTextWidthEm(const std::string& utf8);

struct OverlayStyle {
    float fillAlpha = 0.62f;          // translucency of the per-segment base
    float fontHeightFactor = 0.78f;   // replace-mode font vs bbox height
    float appendHeightFactor = 0.72f; // append-mode font vs bbox height
    float minFontPx = 10.0f;
    float maxFontPx = 34.0f;
    std::int64_t appendGapPx = 2;         // gap below bbox before the band
    float appendBandFactor = 1.2f;        // band height vs bbox height
};

struct OverlaySegment {
    Rect pixelBox;      // segment bbox in image pixels
    std::string text;   // translated text (UTF-8)
};

/// One fully resolved draw instruction for the D2D pass.
struct OverlayDrawItem {
    Rect fillRect;          // image px, clamped inside the image
    RgbColor fillColor;
    float fillAlpha = 0.62f;
    RgbColor textColor;
    float fontPx = 14.f;
    bool ellipsize = false;  // estimated overflow at min size → trim + ellipsis
};

/// Pure per-segment decision for one overlay item: background sampling (when
/// pixels are given), adaptive colors, fill geometry (mode-dependent, clamped
/// to the image), font fit and the truncation flag.
OverlayDrawItem planOverlayItem(const OverlaySegment& segment, OverlayTextMode mode,
                                std::int32_t imageWidth, std::int32_t imageHeight,
                                const OverlayStyle& style = {},
                                const std::uint8_t* bgra = nullptr,
                                std::int32_t stride = 0);

} // namespace tf

#endif // TF_OVERLAY_RENDERER_HPP
