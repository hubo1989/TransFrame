// DisplayMonitor policy implementation (portable; no Win32).
// The Win32 enumerator lives in DisplayMonitorWin32.cpp.
#include "tf/DisplayMonitor.hpp"

namespace tf {

std::vector<DisplayInfo> DisplayMonitorService::displays(const DisplayQuery& query) {
    std::scoped_lock lock(mutex_);
    if (needsRefresh(query, hasCache_)) {
        cache_ = enumerator_->enumerate();
        cachedAt = clock_->now();
        hasCache_ = true;
    }
    return cache_;
}

std::optional<DisplayInfo> DisplayMonitorService::displayAt(Point p, const DisplayQuery& query) {
    const auto list = displays(query);
    for (const auto& d : list) {
        if (d.framePhysical.contains(p)) return d;
    }
    return std::nullopt;
}

void DisplayMonitorService::invalidate() {
    std::scoped_lock lock(mutex_);
    cache_.clear();
    hasCache_ = false;
}

bool DisplayMonitorService::isMultiMonitor(const DisplayQuery& query) {
    return displays(query).size() > 1;
}

bool DisplayMonitorService::needsRefresh(const DisplayQuery& q, bool haveCache) const {
    switch (q.freshness) {
        case DisplayFreshness::Cached:
            return !haveCache;  // fill once, then serve from cache
        case DisplayFreshness::AlwaysRefresh:
            return true;
        case DisplayFreshness::RefreshIfOlderThanMs:
            if (!haveCache) return true;
            return (clock_->now() - cachedAt) > q.maxAge;
    }
    return true;  // unreachable; conservative default
}

} // namespace tf
