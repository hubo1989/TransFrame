// windows/Tests/manual/smoke_capture.cpp — capture engine manual smoke
// (PRD stage 2, FR-01/03 engine layer). Real-run verification of the capture
// chain on the primary display: engine marker, timing against the §8 budget
// (4K full screen <= 50ms steady-state), region crop, and PNG export through
// the same WIC path the app uses (ImageExporter).
//
// Build (from windows/, after building TransFrame.sln Release x64) — or just
// run Tests\manual\build_smoke_capture.bat, which wraps this command. /MD
// matches the sln libs (MultiThreadedDLL); cl's default /MT fails to link
// with __imp_* CRT unresolved symbols:
//
//   cl /nologo /std:c++20 /EHsc /utf-8 /W4 ^
//      /I NativeShell\include /I Core /I Core\include /I Core\vendor ^
//      Tests\manual\smoke_capture.cpp /Fe:build\smoke_capture.exe ^
//      /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib ^
//            build\x64\Release\TransFrame.Core\TransFrame.Core.lib ^
//            d3d11.lib dxgi.lib dwmapi.lib shcore.lib d2d1.lib dwrite.lib ^
//            dcomp.lib windowscodecs.lib windowsapp.lib winhttp.lib ^
//            advapi32.lib user32.lib gdi32.lib ole32.lib
//   build\smoke_capture.exe
//
// Checks (each prints PASS/FAIL; exit code != 0 on any FAIL):
//   C1 display enumeration returns a primary display
//   C2 full-display capture ok via the WGC->DXGI->GDI chain
//   C3 engine marker is WindowsGraphicsCapture (capable system expected;
//      an actual fallback prints the reason — investigate, do not ignore)
//   C4 steady-state timing vs PRD §8 (<= 50ms at 4K; warm-up reported apart)
//   C5 pixel buffer invariants (dims == display frame, tight stride for
//      WGC/DXGI, DPI metadata present)
//   C6 region capture (center crop) ok with cropped dims
//   C7 PNG export via ImageExporter (WIC) produced a non-empty file
//
// NOTE: the first capture may trigger the one-time system capture notice
// (Windows 11 behavior for a new unpackaged app) and pays session warm-up;
// steady-state runs are the ones measured against the budget.
//
// NOTE: this exe ships no PMv2 manifest, so Windows DPI-virtualization
// heuristics decide per launch whether GetMonitorInfo reports physical or
// scaled coordinates (both were observed on one machine). Either way the
// capture is self-consistent; the shipped app embeds the PMv2 manifest and
// always works in physical pixels.

#ifdef _WIN32

#include "tf/CaptureService.hpp"
#include "tf/ImageExporter.hpp"
#include "tf/NativeShellInit.hpp"

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

using namespace tf;

namespace {

int g_failures = 0;

void check(bool ok, const char* step, const std::string& detail = "") {
    if (ok) {
        std::printf("[PASS] %s %s\n", step, detail.c_str());
    } else {
        ++g_failures;
        std::printf("[FAIL] %s %s\n", step, detail.c_str());
    }
}

const char* engineName(CaptureEngine engine) {
    switch (engine) {
        case CaptureEngine::WindowsGraphicsCapture: return "WindowsGraphicsCapture";
        case CaptureEngine::DxgiDesktopDuplication: return "DxgiDesktopDuplication";
        case CaptureEngine::GdiBitBlt: return "GdiBitBlt";
        case CaptureEngine::None: return "None";
    }
    return "?";
}

std::string narrow(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(),
                                        static_cast<int>(wide.size()), nullptr, 0,
                                        nullptr, nullptr);
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                          out.data(), n, nullptr, nullptr);
    return out;
}

double elapsedMs(std::chrono::steady_clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
        .count();
}

std::string errorDetail(const CaptureResult& r) {
    return r.ok ? std::string() : (" error=" + narrow(r.error));
}

}  // namespace

int main() {
    std::printf("== TransFrame Windows capture smoke (stage 2 engine layer) ==\n");
    ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    auto monitorService = makeDisplayMonitorService();
    auto capture = makeCaptureService();

    // C1: primary display.
    const auto displays = monitorService->displays();
    const auto primaryIt = std::find_if(displays.begin(), displays.end(),
                                        [](const DisplayInfo& d) { return d.isPrimary; });
    check(primaryIt != displays.end(), "C1 primary display enumerated",
          "display count=" + std::to_string(displays.size()));
    if (primaryIt == displays.end()) {
        std::printf("== smoke complete: %d failure(s) ==\n", g_failures + 1);
        return 1;
    }
    const DisplayInfo primary = *primaryIt;
    std::printf("      primary %s: %lldx%lld @%u dpi, origin(%lld,%lld)\n",
                narrow(primary.id).c_str(), primary.framePhysical.width,
                primary.framePhysical.height, primary.dpiX, primary.framePhysical.x,
                primary.framePhysical.y);

    // C2/C4: full-display captures — run 0 warms the pool/session, runs
    // 1..5 measure the steady state the PRD budget applies to.
    constexpr int kSteadyRuns = 5;
    const bool is4k = primary.framePhysical.width >= 3840 &&
                      primary.framePhysical.height >= 2160;
    const double budgetMs = is4k ? 50.0 : 50.0 * (3840.0 * 2160.0) /
                                               static_cast<double>(
                                                   primary.framePhysical.width *
                                                   primary.framePhysical.height);
    CaptureResult reference{};
    std::vector<double> steadyMs;
    bool allOk = true;
    for (int i = 0; i <= kSteadyRuns; ++i) {
        const auto t0 = std::chrono::steady_clock::now();
        CaptureResult r = capture->captureDisplay(primary, CaptureTrigger::HotkeyFullScreen);
        const double ms = elapsedMs(t0);
        if (i == 0) {
            reference = std::move(r);
            std::printf("      warm-up capture: %.2f ms (%s)%s\n", ms,
                        engineName(reference.engine), errorDetail(reference).c_str());
        } else {
            allOk = allOk && r.ok;
            if (!r.ok) {
                std::printf("      steady run %d failed:%s\n", i, errorDetail(r).c_str());
            }
            steadyMs.push_back(ms);
        }
    }
    check(reference.ok, "C2 full-display capture ok", errorDetail(reference));
    check(allOk, "C2 steady-state captures ok", "");

    // C3: engine marker.
    check(reference.engine == CaptureEngine::WindowsGraphicsCapture, "C3 engine is WGC",
          std::string("actual=") + engineName(reference.engine));

    // C4: steady-state timing (median of the 5 runs).
    std::sort(steadyMs.begin(), steadyMs.end());
    const double medianMs = steadyMs[steadyMs.size() / 2];
    {
        std::string runs;
        for (const double ms : steadyMs) runs += " " + std::to_string(ms).substr(0, 6);
        std::printf("      steady runs (ms):%s\n", runs.c_str());
        std::printf("      resolution %lldx%lld, %s -> median %.2f ms (budget %.1f ms)\n",
                    primary.framePhysical.width, primary.framePhysical.height,
                    is4k ? "4K" : "sub-4K (budget scaled)", medianMs, budgetMs);
    }
    check(medianMs <= budgetMs, "C4 steady-state within PRD budget",
          "median=" + std::to_string(medianMs) + "ms budget=" +
              std::to_string(budgetMs) + "ms");

    // C5: pixel buffer invariants.
    const PixelBuffer& px = reference.screenshot.pixels;
    check(px.valid() && px.width == static_cast<std::int32_t>(primary.framePhysical.width) &&
              px.height == static_cast<std::int32_t>(primary.framePhysical.height),
          "C5 pixel dims match display frame",
          "got " + std::to_string(px.width) + "x" + std::to_string(px.height));
    if (reference.engine != CaptureEngine::GdiBitBlt) {
        check(px.stride == px.width * 4, "C5 tight stride (WGC/DXGI path)",
              "stride=" + std::to_string(px.stride));
    } else {
        std::printf("      (GDI engine: aligned stride %d, tight-stride check skipped)\n",
                    px.stride);
    }
    check(px.dpiX == primary.dpiX && px.dpiY == primary.dpiY, "C5 dpi metadata carried",
          std::to_string(px.dpiX));
    {
        const std::int64_t cx = px.width / 2, cy = px.height / 2;
        const std::uint8_t* center =
            px.data.get() + static_cast<std::size_t>(cy) * px.stride +
            static_cast<std::size_t>(cx) * 4;
        std::printf("      center pixel BGRA: %u %u %u %u\n", center[0], center[1],
                    center[2], center[3]);
    }

    // C6: region capture (center crop, clamped by the service).
    {
        const Rect region{primary.framePhysical.x + primary.framePhysical.width / 2 - 256,
                          primary.framePhysical.y + primary.framePhysical.height / 2 - 192,
                          512, 384};
        const auto t0 = std::chrono::steady_clock::now();
        const CaptureResult regionResult =
            capture->captureRegion(region, primary, CaptureTrigger::ManualRegion);
        const double ms = elapsedMs(t0);
        const Rect expected = region.intersection(primary.framePhysical);
        check(regionResult.ok &&
                  regionResult.screenshot.pixels.width ==
                      static_cast<std::int32_t>(expected.width) &&
                  regionResult.screenshot.pixels.height ==
                      static_cast<std::int32_t>(expected.height),
              "C6 region capture cropped dims",
              std::string(engineName(regionResult.engine)) + " " +
                  std::to_string(ms).substr(0, 6) + "ms" + errorDetail(regionResult));
    }

    // C7: PNG export via the WIC path the app uses.
    {
        const std::wstring path = L"smoke_capture.png";
        std::wstring error;
        const bool saved = encodeToFile(reference.screenshot.pixels, path,
                                        ExportFormat::Png, 100, error);
        WIN32_FILE_ATTRIBUTE_DATA attrs{};
        const bool exists =
            saved && ::GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attrs) &&
            attrs.nFileSizeLow > 0;
        check(exists, "C7 PNG export via ImageExporter",
              exists ? "smoke_capture.png, " +
                           std::to_string(static_cast<unsigned long long>(
                               attrs.nFileSizeLow) / 1024) +
                           " KiB"
                     : " error=" + narrow(error));
    }

    std::printf("== smoke complete: %d failure(s) ==\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else  // !_WIN32

#include <cstdio>

int main() {
    std::printf(
        "smoke_capture: Windows-only manual smoke (build with MSVC on Windows; "
        "this host is not Windows — NOT RUN).\n");
    return 0;
}

#endif  // _WIN32
