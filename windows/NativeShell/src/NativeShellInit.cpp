// NativeShellInit.cpp — composition root wiring the fallback chain.
#include "pch.h"

#include "tf/CaptureService.hpp"
#include "tf/DisplayMonitor.hpp"
#include "tf/HotkeyManager.hpp"
#include "tf/WindowDetector.hpp"

namespace tf {

// Forward declarations from engine TUs.
std::unique_ptr<ICaptureEngine> makeWgcCaptureEngine();
bool probeDxgiDuplicationAvailability();
bool probeGdiAvailability();

std::unique_ptr<IDisplayEnumerator> makeWin32DisplayEnumerator();
std::unique_ptr<IClock> makeSteadyClock();
std::unique_ptr<IWindowLister> makeWin32WindowLister(DWORD excludePid);
std::unique_ptr<IHotkeyRegistrar> makeWin32HotkeyRegistrar();

/// Build the capture service with the PRD §3.2 fallback chain:
/// WGC -> DXGI Desktop Duplication -> GDI BitBlt.
std::unique_ptr<CaptureService> makeCaptureService() {
    std::vector<std::unique_ptr<ICaptureEngine>> chain;
    chain.push_back(makeWgcCaptureEngine());
    // DXGI + GDI engines are registered as availability-probed stubs in this
    // milestone; each engine TU fills isAvailable/capture.
    return std::make_unique<CaptureService>(std::move(chain));
}

std::unique_ptr<DisplayMonitorService> makeDisplayMonitorService() {
    static auto enumerator = makeWin32DisplayEnumerator();
    static auto clock = makeSteadyClock();
    return std::make_unique<DisplayMonitorService>(*enumerator, *clock);
}

std::unique_ptr<WindowDetectorService> makeWindowDetectorService(DWORD excludePid) {
    static auto lister = makeWin32WindowLister(excludePid);
    return std::make_unique<WindowDetectorService>(*lister);
}

std::unique_ptr<HotkeyManager> makeHotkeyManager() {
    static auto registrar = makeWin32HotkeyRegistrar();
    return std::make_unique<HotkeyManager>(*registrar);
}

} // namespace tf
