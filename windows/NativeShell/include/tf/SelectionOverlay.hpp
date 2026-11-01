// tf/SelectionOverlay.hpp
// Full-screen selection overlay window (FR-01/02): one WS_POPUP +
// WS_EX_LAYERED + WS_EX_TOPMOST window spanning the entire virtual desktop,
// rendered with Direct2D 1.1 + DirectComposition.
//
// This header is the Win32-side controller; it compiles only on Windows.
// The rendering state machine it drives is OverlayRenderer.hpp (portable).
//
// Performance contract (PRD §8):
//   hotkey -> overlay visible  <= 60ms  (window pre-created at app start; show = 1 DWM frame)
//   frame paint at 120Hz       <=  8ms  (D2D1.1 command list + DComp visual; no GDI)
#ifndef TF_SELECTION_OVERLAY_HPP
#define TF_SELECTION_OVERLAY_HPP

#include "OverlayRenderer.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace tf {

struct SelectionOverlayResult {
    bool confirmed = false;
    Rect regionPhysical;      // confirmed selection (physical px, virtual-desktop coords)
    bool wholeWindow = false; // FR-02: region came from a window click
    std::uint64_t windowHwnd = 0;
};

using OverlayCompletion = std::function<void(SelectionOverlayResult result)>;

/// D2D/DComp device plumbing (implemented in src/SelectionOverlay.cpp).
class OverlayRendererDevice;

class SelectionOverlayController {
public:
    SelectionOverlayController();
    ~SelectionOverlayController();

    SelectionOverlayController(const SelectionOverlayController&) = delete;
    SelectionOverlayController& operator=(const SelectionOverlayController&) = delete;

    /// Create the (hidden) overlay window + D2D/DComp device at app start so
    /// the hotkey path only has to ShowWindow. Returns false on device failure.
    bool precreate();

    /// Hotkey path: show overlay, begin tracking. completion fires exactly once
    /// per session with confirmed=true (selection made) or confirmed=false (Esc).
    bool beginSession(OverlayCompletion completion);

    /// Cancel programmatically (e.g. focus loss).
    void cancelSession();

    bool isActive() const;

    /// Inject render state (tests). Production uses internal message pump.
    OverlayState& state();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tf

#endif // TF_SELECTION_OVERLAY_HPP
