// tf/DisplayMonitor.hpp
// Display enumeration: EnumDisplayMonitors + GetDpiForMonitor (Per-Monitor V2).
// Mirrors macOS DisplayFreshnessPolicy (CapturePipeline.swift): a 2s cache so
// hot paths (hotkey -> overlay) never pay the ~1-3ms enumeration cost, while
// repeated invocations still pick up display changes within the PRD budget.
//
// Windows API surface is isolated behind an injectable provider so the cache
// policy itself is unit-testable on any platform (see tests/).
#ifndef TF_DISPLAY_MONITOR_HPP
#define TF_DISPLAY_MONITOR_HPP

#include "CoreTypes.hpp"

#include <chrono>
#include <mutex>
#include <vector>

namespace tf {

enum class DisplayFreshness : std::uint8_t {
    Cached,                  // return cached list, never enumerate
    AlwaysRefresh,           // enumerate now
    RefreshIfOlderThanMs,    // enumerate when cache older than threshold
};

struct DisplayQuery {
    DisplayFreshness freshness = DisplayFreshness::RefreshIfOlderThanMs;
    std::chrono::milliseconds maxAge{2000};  // aligns macOS refreshIfOlderThan(2)
};

/// Platform hook. Production implementation lives in
/// src/DisplayMonitor.cpp (Win32); tests substitute a fake clock + fake list.
class IDisplayEnumerator {
public:
    virtual ~IDisplayEnumerator() = default;
    /// Returns physical-pixel frames in virtual-desktop coordinates.
    virtual std::vector<DisplayInfo> enumerate() = 0;
};

/// Abstract clock for testable cache expiry.
class IClock {
public:
    virtual ~IClock() = default;
    virtual std::chrono::steady_clock::time_point now() = 0;
};

class DisplayMonitorService {
public:
    explicit DisplayMonitorService(IDisplayEnumerator& enumerator, IClock& clock)
        : enumerator_(&enumerator), clock_(&clock) {}

    /// Primary entry for capture pipelines. Thread-safe.
    std::vector<DisplayInfo> displays(const DisplayQuery& query = {});

    /// Display whose frame contains the point (physical virtual-desktop px).
    /// Empty result when the point sits outside any monitor.
    std::optional<DisplayInfo> displayAt(Point p, const DisplayQuery& query = {});

    /// Drop cache; next query enumerates (call on WM_DISPLAYCHANGE).
    void invalidate();

    /// True when at least 2 displays are connected (FR-03 picker rule).
    bool isMultiMonitor(const DisplayQuery& query = {});

    static std::chrono::milliseconds defaultMaxAge() { return std::chrono::milliseconds(2000); }

private:
    bool needsRefresh(const DisplayQuery& q, bool haveCache) const;

    IDisplayEnumerator* enumerator_;
    IClock* clock_;
    mutable std::mutex mutex_;
    std::vector<DisplayInfo> cache_;
    std::chrono::steady_clock::time_point cachedAt{};
    bool hasCache_ = false;
};

} // namespace tf

#endif // TF_DISPLAY_MONITOR_HPP
