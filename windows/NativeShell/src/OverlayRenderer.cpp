// OverlayRenderer.cpp — portable overlay state machine + paint list.
// No Win32/D2D here: the 120Hz path consumes paintList() on the render
// thread; only src/SelectionOverlay.cpp touches D2D/DComp.
#include "tf/OverlayRenderer.hpp"

#include <algorithm>

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
            break;
        }
    }
    return pl;
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

} // namespace tf
