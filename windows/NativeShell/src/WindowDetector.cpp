// WindowDetector policy implementation (portable; no Win32).
// Win32 lister lives in WindowDetectorWin32.cpp.
#include "tf/WindowDetector.hpp"

namespace tf {

std::vector<WindowInfo> filterVisibleWindows(std::vector<WindowInfo> windows) {
    std::vector<WindowInfo> out;
    out.reserve(windows.size());
    for (auto& w : windows) {
        if (w.isCloaked) continue;                 // DWMWA_CLOAKED != 0 (UWP suspended, cloaked)
        if (w.alpha <= 0.0) continue;              // fully transparent layered window
        if (w.frame.empty()) continue;             // zero-size frame
        out.push_back(std::move(w));
    }
    return out;
}

std::optional<WindowInfo> windowUnderPoint(const std::vector<WindowInfo>& ordered, Point p) {
    for (const auto& w : ordered) {
        if (w.frame.contains(p)) return w;
    }
    return std::nullopt;
}

std::vector<WindowInfo> WindowDetectorService::visibleWindows() {
    snapshot_ = filterVisibleWindows(lister_->listTopmostFirst());
    return *snapshot_;
}

std::optional<WindowInfo> WindowDetectorService::windowUnderPoint(Point p) {
    if (!snapshot_) refreshSnapshot();
    return tf::windowUnderPoint(*snapshot_, p);
}

void WindowDetectorService::refreshSnapshot() {
    snapshot_ = filterVisibleWindows(lister_->listTopmostFirst());
}

void WindowDetectorService::invalidate() {
    snapshot_.reset();
}

} // namespace tf
