// tf/SelectionOverlay.hpp
// Full-screen selection overlay window (FR-01/02): one WS_POPUP +
// WS_EX_NOREDIRECTIONBITMAP + WS_EX_TOPMOST window spanning the virtual desktop,
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
#include "WindowDetector.hpp"

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

/// FR-05: the overlay stays up after a drag selection (Confirmed mode) and
/// fires this handler when a toolbar button is pressed. Invoked on the main
/// thread with the overlay window ALREADY HIDDEN so the app can capture the
/// clean desktop (a visible dim layer would end up in the capture).
/// The controller does nothing afterwards except:
///   Translate/Pin/ColorPick — wait for presentResults()/presentNotice();
///   Copy/Save               — the app calls endSession() when done;
///   Cancel                  — handled internally before the handler runs.
using OverlayActionHandler =
    std::function<void(OverlayToolbarAction action, Rect regionPhysical)>;

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

    /// FR-02: attach the App's window detector for hover highlighting and
    /// click-to-capture-window. Ownership stays with the caller; must outlive
    /// the controller (or be reset with setWindowDetector(nullptr)).
    void setWindowDetector(WindowDetectorService* detector);

    /// FR-05: toolbar action callback for the Confirmed state. Set before
    /// beginSession; kept for the controller's lifetime.
    void setToolbarActionHandler(OverlayActionHandler handler);

    /// FR-04 auto-path: with a Translate action set, drag-release fires the
    /// action immediately instead of parking on the toolbar (hotkey
    /// Ctrl+Alt+T must reach results with no extra click). nullopt = normal
    /// toolbar flow (Ctrl+Alt+A).
    void setAutoConfirmAction(std::optional<OverlayToolbarAction> action);

    /// FR-06: show source/translation rows in the docked panel and bring the
    /// (action-hidden) overlay back up. Safe from any thread (posts to the
    /// window); no-op when no session is active.
    void presentResults(std::vector<OverlayTextRow> rows, std::string statusUtf8);

    /// FR-05/06: single notice line in the panel (placeholder actions);
    /// same threading rules as presentResults.
    void presentNotice(std::string noticeUtf8);

    /// Hide the overlay and complete the session as cancelled. Safe from any
    /// thread; no-op when no session is active.
    void endSession();

    /// True while the Confirmed UI (toolbar/panel) is on screen.
    bool awaitingAction() const;

    /// Inject render state (tests). Production uses internal message pump.
    OverlayState& state();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace tf

#endif // TF_SELECTION_OVERLAY_HPP
