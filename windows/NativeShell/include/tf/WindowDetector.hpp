// tf/WindowDetector.hpp
// Window detection for FR-02: EnumWindows + DwmGetWindowAttribute
// (DWMWA_EXTENDED_FRAME_BOUNDS / DWMWA_CLOAKED) with a mouse-down snapshot
// cache, mirroring the macOS WindowDetector.swift semantics:
//   - visibleWindows(): topmost-first Z order, filtered
//   - windowUnderPoint(): <= 1ms hot path from the cached snapshot
//   - refreshSnapshot() at mouse-down (windows do not move while user operates overlay)
//
// Win32 specifics are isolated behind IWindowLister so geometry, filtering and
// hit-testing are unit-testable headless.
#ifndef TF_WINDOW_DETECTOR_HPP
#define TF_WINDOW_DETECTOR_HPP

#include "CoreTypes.hpp"
#include "Geo.hpp"

#include <optional>
#include <string>
#include <vector>

namespace tf {

struct WindowInfo {
    std::uint64_t hwnd = 0;          // HWND as stable integer
    std::uint32_t pid = 0;
    Rect frame;                      // DWMWA_EXTENDED_FRAME_BOUNDS, physical px, virtual-desktop coords
    std::wstring ownerName;          // process / owner title
    std::wstring windowName;         // window title (best effort)
    std::int32_t zOrder = 0;         // 0 = topmost visible
    bool isCloaked = false;          // DWMWA_CLOAKED != 0 (UWP suspended etc.)
    bool isOnScreen = true;
    double alpha = 1.0;              // GetLayeredWindowAttributes alpha / 255 (0 if layered absent)

    bool operator==(const WindowInfo& o) const {
        return hwnd == o.hwnd && frame == o.frame && isCloaked == o.isCloaked;
    }
};

/// Platform hook: production implementation in src/WindowDetector.cpp uses
/// EnumWindows + DwmGetWindowAttribute; tests feed synthetic window sets.
class IWindowLister {
public:
    virtual ~IWindowLister() = default;
    /// Topmost-first list of candidate windows (unfiltered raw frames).
    virtual std::vector<WindowInfo> listTopmostFirst() = 0;
};

/// Pure filtering + hit-test policy. Given a raw topmost-first list:
///   - drop cloaked windows (DWMWA_CLOAKED)
///   - drop fully transparent (alpha == 0)
///   - drop zero-size / off-screen-empty frames
///   - keep everything else (owner-owned child windows included: browser
///     view children must be selectable per FR-02 acceptance)
/// Returns topmost-first filtered list.
std::vector<WindowInfo> filterVisibleWindows(std::vector<WindowInfo> windows);

/// Hit-test: first window in topmost-first order whose frame contains the
/// point. Caller passes output of filterVisibleWindows.
std::optional<WindowInfo> windowUnderPoint(const std::vector<WindowInfo>& ordered, Point p);

class WindowDetectorService {
public:
    explicit WindowDetectorService(IWindowLister& lister) : lister_(&lister) {}

    /// Enumerate + filter now (also refreshes the snapshot cache).
    std::vector<WindowInfo> visibleWindows();

    /// Hot path (mouse-move / hover): hit-test against the cached snapshot.
    /// Falls back to a fresh list when no snapshot exists yet.
    std::optional<WindowInfo> windowUnderPoint(Point p);

    /// Refresh snapshot (call at mouse-down; see macOS spec caching note).
    void refreshSnapshot();

    /// Drop snapshot (call at overlay teardown).
    void invalidate();

    bool hasSnapshot() const { return snapshot_.has_value(); }

private:
    IWindowLister* lister_;
    std::optional<std::vector<WindowInfo>> snapshot_;
};

} // namespace tf

#endif // TF_WINDOW_DETECTOR_HPP
