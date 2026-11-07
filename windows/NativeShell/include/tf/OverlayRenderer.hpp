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

#include <mutex>
#include <optional>
#include <vector>

namespace tf {

enum class OverlayMode : std::uint8_t {
    Idle,        // crosshair only, no selection yet
    HoverWindow, // window under cursor highlighted (FR-02)
    Dragging,    // manual rect selection in progress (FR-01)
    Confirmed,   // selection done; in-place toolbar shown (layout skeleton only)
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

struct PaintList {
    Rect desktop;                // virtual desktop bounds (physical px)
    Rect dimRegion = {};         // = desktop; kept explicit for the punch-out pass
    std::optional<Rect> punchThrough;      // undimmed selection
    std::optional<Rect> selectionStroke;   // 1px accent outline
    std::optional<Rect> windowHighlight;   // hovered window (fill+stroke)
    bool crosshair = false;
    Point crosshairAt{};
    std::optional<SizeBadge> badge;
    // FR-05: layout skeleton only (rects for toolbar buttons; no handlers).
    std::vector<Rect> toolbarSlots;
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
};

/// FR-05 layout skeleton: toolbar button rects below/above selection edge,
/// clamped to desktop. Pure geometry, ready for later task to attach commands.
std::vector<Rect> layoutInPlaceToolbar(const Rect& selection, const Rect& desktop,
                                       std::size_t buttonCount);

} // namespace tf

#endif // TF_OVERLAY_RENDERER_HPP
