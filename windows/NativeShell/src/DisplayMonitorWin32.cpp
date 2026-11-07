// DisplayMonitorWin32.cpp — EnumDisplayMonitors + GetDpiForMonitor provider.
// Physical-pixel frames come from GetMonitorInfo(rcMonitor) because the
// process is Per-Monitor V2 aware (app.manifest): rcMonitor is already in the
// physical coordinate space of the virtual desktop.
#include "pch.h"

#include "tf/DisplayMonitor.hpp"

#include <algorithm>

namespace tf {
namespace {

class Win32Enumerator final : public IDisplayEnumerator {
public:
    std::vector<DisplayInfo> enumerate() override {
        std::vector<DisplayInfo> out;
        ::EnumDisplayMonitors(
            nullptr, nullptr,
            [](HMONITOR monitor, HDC, LPRECT, LPARAM lParam) -> BOOL {
                auto* self = reinterpret_cast<std::vector<DisplayInfo>*>(lParam);

                MONITORINFOEXW mi{};
                mi.cbSize = sizeof(mi);
                if (!::GetMonitorInfoW(monitor, &mi)) return TRUE;  // skip broken entries

                UINT dpiX = 96, dpiY = 96;
                // PROCESS_PER_MONITOR_DPI_AWARE => returns per-monitor DPI.
                if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
                    dpiX = dpiY = 96;  // assume 100% when the API is unavailable (Win10 1607-)
                }

                const RECT& r = mi.rcMonitor;  // physical px, virtual-desktop coords
                DisplayInfo info;
                info.id = mi.szDevice;
                info.name = mi.szDevice;  // friendly names need DISPLAYCONFIG; fine for v1
                info.framePhysical = Rect{r.left, r.top,
                                          r.right - r.left, r.bottom - r.top};
                info.dpiX = dpiX;
                info.dpiY = dpiY;
                info.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
                self->push_back(std::move(info));
                return TRUE;
            },
            reinterpret_cast<LPARAM>(&out));

        // Deterministic order: primary first, then by x (matches macOS
        // DisplayInfo ordering expectations and keeps capture targeting stable).
        std::sort(out.begin(), out.end(), [](const DisplayInfo& a, const DisplayInfo& b) {
            if (a.isPrimary != b.isPrimary) return a.isPrimary;
            return a.framePhysical.x < b.framePhysical.x;
        });
        return out;
    }
};

class SteadyClock final : public IClock {
public:
    std::chrono::steady_clock::time_point now() override {
        return std::chrono::steady_clock::now();
    }
};

} // namespace

std::unique_ptr<IDisplayEnumerator> makeWin32DisplayEnumerator() {
    return std::make_unique<Win32Enumerator>();
}
std::unique_ptr<IClock> makeSteadyClock() {
    return std::make_unique<SteadyClock>();
}

} // namespace tf
