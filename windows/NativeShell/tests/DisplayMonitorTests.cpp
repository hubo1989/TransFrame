// tests/DisplayMonitorTests.cpp — the 2s cache-refresh policy
// (mirrors macOS DisplayFreshnessPolicy.refreshIfOlderThan(2)).
#include "catch_amalgamated.hpp"

#include "tf/DisplayMonitor.hpp"

#include <deque>

using namespace tf;
using namespace std::chrono_literals;

namespace {

class FakeClock final : public IClock {
public:
    std::chrono::steady_clock::time_point now() override { return current_; }
    void advance(std::chrono::milliseconds ms) { current_ += ms; }

private:
    std::chrono::steady_clock::time_point current_{};
};

class CountingEnumerator final : public IDisplayEnumerator {
public:
    explicit CountingEnumerator(std::vector<DisplayInfo> list)
        : list_(std::move(list)) {}

    std::vector<DisplayInfo> enumerate() override {
        ++calls;
        return list_;
    }

    int calls = 0;

private:
    std::vector<DisplayInfo> list_;
};

DisplayInfo display(std::int64_t x, std::int64_t w, bool primary = false) {
    DisplayInfo d;
    d.framePhysical = Rect{x, 0, w, 1080};
    d.isPrimary = primary;
    return d;
}

} // namespace

TEST_CASE("refreshIfOlderThan(2s): repeated queries inside the window hit cache",
          "[display][cache]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{display(0, 1920, true)}};
    DisplayMonitorService svc(enumerator, clock);

    DisplayQuery q;  // default: RefreshIfOlderThanMs, maxAge 2000ms
    (void)svc.displays(q);
    (void)svc.displays(q);
    (void)svc.displays(q);
    CHECK(enumerator.calls == 1);
}

TEST_CASE("refreshIfOlderThan(2s): expiry re-enumerates once", "[display][cache]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{display(0, 1920, true)}};
    DisplayMonitorService svc(enumerator, clock);

    DisplayQuery q;
    (void)svc.displays(q);
    clock.advance(1999ms);
    (void)svc.displays(q);      // still fresh (1999 < 2000)
    CHECK(enumerator.calls == 1);
    clock.advance(2ms);         // 2001ms total
    (void)svc.displays(q);      // stale -> refresh
    CHECK(enumerator.calls == 2);
    (void)svc.displays(q);      // fresh again
    CHECK(enumerator.calls == 2);
}

TEST_CASE("Cached never re-enumerates after the first fill", "[display][cache]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{display(0, 1920, true)}};
    DisplayMonitorService svc(enumerator, clock);

    DisplayQuery q;
    q.freshness = DisplayFreshness::Cached;
    (void)svc.displays(q);
    clock.advance(10s);
    (void)svc.displays(q);
    CHECK(enumerator.calls == 1);
}

TEST_CASE("AlwaysRefresh enumerates every query", "[display][cache]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{display(0, 1920, true)}};
    DisplayMonitorService svc(enumerator, clock);

    DisplayQuery q;
    q.freshness = DisplayFreshness::AlwaysRefresh;
    (void)svc.displays(q);
    (void)svc.displays(q);
    CHECK(enumerator.calls == 2);
}

TEST_CASE("invalidate forces the next query to enumerate", "[display][cache]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{display(0, 1920, true)}};
    DisplayMonitorService svc(enumerator, clock);

    DisplayQuery q;
    (void)svc.displays(q);
    svc.invalidate();           // e.g. WM_DISPLAYCHANGE
    (void)svc.displays(q);
    CHECK(enumerator.calls == 2);
}

TEST_CASE("displayAt resolves the monitor containing the point", "[display]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{
        display(0, 1920, true),
        display(1920, 3840),
    }};
    DisplayMonitorService svc(enumerator, clock);

    CHECK(svc.displayAt(Point{100, 100})->framePhysical.x == 0);
    CHECK(svc.displayAt(Point{2000, 100})->framePhysical.x == 1920);
    CHECK_FALSE(svc.displayAt(Point{9000, 100}).has_value());
    CHECK(svc.isMultiMonitor());
}

TEST_CASE("empty enumeration: displays() returns empty, no crash", "[display]") {
    FakeClock clock;
    CountingEnumerator enumerator{std::vector<DisplayInfo>{}};
    DisplayMonitorService svc(enumerator, clock);
    CHECK(svc.displays().empty());
    CHECK_FALSE(svc.isMultiMonitor());
}
