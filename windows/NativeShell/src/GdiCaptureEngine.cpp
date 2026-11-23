// GdiCaptureEngine.cpp — GDI BitBlt last-resort engine (full implementation).
//
// Always available (any session type). BitBlt with SRCCOPY|CAPTUREBLT from the
// screen DC of the target monitor. CAPTUREBLT includes layered windows.
// ~30-80ms for 4K — acceptable only as the final fallback (PRD §3.2).
#include "pch.h"

#include "tf/CaptureService.hpp"

#include <algorithm>

namespace tf {
namespace {

bool probeGdiAvailability() {
    HDC screen = ::GetDC(nullptr);
    if (!screen) return false;
    ::ReleaseDC(nullptr, screen);
    return true;
}

/// Monitor-from-rect: the display whose frame contains the region origin.
HMONITOR monitorForRegion(const Rect& region) {
    // MonitorFromPoint with MONITOR_DEFAULTTONEAREST handles multi-monitor;
    // the source display id (\\.\DISPLAY1) is only metadata.
    const POINT origin{static_cast<LONG>(region.x), static_cast<LONG>(region.y)};
    return ::MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
}

class GdiEngine final : public ICaptureEngine {
public:
    CaptureEngine kind() const override { return CaptureEngine::GdiBitBlt; }
    bool isAvailable() override { return probeGdiAvailability(); }

    CaptureResult capture(const CaptureRequest& request) override {
        CaptureResult out;
        out.engine = CaptureEngine::GdiBitBlt;

        const Rect& region = request.regionPhysical;
        if (region.width <= 0 || region.height <= 0) {
            out.error = L"Empty capture region.";
            return out;
        }

        const HMONITOR monitor = monitorForRegion(region);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!::GetMonitorInfoW(monitor, &mi)) {
            out.error = L"GetMonitorInfo failed.";
            return out;
        }
        const RECT& mon = mi.rcMonitor;  // physical px (PMv2 process)
        if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX_, &dpiY_))) {
            dpiX_ = dpiY_ = 96;
        }

        // 1) create a compatible destination DC + bitmap sized to the region
        HDC screenDc = ::GetDC(nullptr);
        if (!screenDc) {
            out.error = L"GetDC(nullptr) failed.";
            return out;
        }
        HDC memDc = ::CreateCompatibleDC(screenDc);

        const int w = static_cast<int>(region.width);
        const int h = static_cast<int>(region.height);
        HBITMAP bitmap = ::CreateCompatibleBitmap(screenDc, w, h);
        const HGDIOBJ oldBmp = ::SelectObject(memDc, bitmap);

        // 2) BitBlt the region from screen coords into the bitmap origin.
        //    Negative virtual-desktop origins (left monitor) work naturally:
        //    BitBlt takes signed x/y.
        const BOOL blit = ::BitBlt(memDc, 0, 0, w, h, screenDc,
                                   static_cast<int>(region.x), static_cast<int>(region.y),
                                   SRCCOPY | CAPTUREBLT);
        if (!blit) {
            ::SelectObject(memDc, oldBmp);
            ::DeleteObject(bitmap);
            ::DeleteDC(memDc);
            ::ReleaseDC(nullptr, screenDc);
            out.error = L"BitBlt failed (GDI).";
            return out;
        }

        // 3) Extract pixels: GetDIBits with BI_RGB top-down 32bpp.
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        const std::size_t stride = AlignUp<4>(w) * 4;
        auto data = std::make_shared_for_overwrite<std::uint8_t[]>(stride * h);
        const int got = ::GetDIBits(memDc, bitmap, 0, h, data.get(), &bmi, DIB_RGB_COLORS);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(bitmap);
        ::DeleteDC(memDc);
        ::ReleaseDC(nullptr, screenDc);
        if (got != h) {
            out.error = L"GetDIBits failed.";
            return out;
        }

        PixelBuffer pixels;
        pixels.width = w;
        pixels.height = h;
        pixels.stride = static_cast<std::int32_t>(stride);
        pixels.dpiX = dpiX_;
        pixels.dpiY = dpiY_;
        pixels.data = std::move(data);

        out.ok = true;
        out.screenshot.pixels = std::move(pixels);
        out.screenshot.trigger = request.trigger;
        out.screenshot.capturedAt = std::chrono::system_clock::now();
        if (request.sourceDisplay) {
            out.screenshot.sourceDisplay = *request.sourceDisplay;
        } else {
            out.screenshot.sourceDisplay.framePhysical = Rect{mon.left, mon.top,
                                                              mon.right - mon.left,
                                                              mon.bottom - mon.top};
        }
        return out;
    }

private:
    template <std::int64_t N>
    static std::size_t AlignUp(int v) {
        return static_cast<std::size_t>((v + N - 1) / N * N);
    }

    UINT dpiX_ = 96, dpiY_ = 96;
};

} // namespace

std::unique_ptr<ICaptureEngine> makeGdiCaptureEngine() {
    return std::make_unique<GdiEngine>();
}

} // namespace tf
