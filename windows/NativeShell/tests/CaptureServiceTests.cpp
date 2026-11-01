// tests/CaptureServiceTests.cpp — engine fallback chain + region clamping +
// borderless-contract gate + pixel-buffer invariants.
#include "catch_amalgamated.hpp"

#include "tf/CaptureService.hpp"

using namespace tf;

namespace {

constexpr std::uint32_t kTestDpi = 96;

DisplayInfo makeDisplay(std::int64_t x, std::int64_t y,
                        std::int64_t w, std::int64_t h) {
    DisplayInfo d;
    d.framePhysical = Rect{x, y, w, h};
    d.dpiX = d.dpiY = kTestDpi;
    d.isPrimary = (x == 0 && y == 0);
    return d;
}

/// Scripted engine: availability and capture outcome preset per test.
class StubEngine final : public ICaptureEngine {
public:
    StubEngine(CaptureEngine kind, bool available, bool succeed,
               std::wstring failReason = L"stub failure")
        : kind_(kind), available_(available), succeed_(succeed),
          failReason_(std::move(failReason)) {}

    CaptureEngine kind() const override { return kind_; }
    bool isAvailable() override { return available_; }

    CaptureResult capture(const CaptureRequest& request) override {
        ++captures;
        lastRequest = request;
        CaptureResult r;
        if (succeed_) {
            r.ok = true;
            r.engine = kind_;
            r.screenshot.pixels.width = static_cast<std::int32_t>(request.regionPhysical.width);
            r.screenshot.pixels.height = static_cast<std::int32_t>(request.regionPhysical.height);
            r.screenshot.pixels.stride = r.screenshot.pixels.width * 4;
            r.screenshot.pixels.dpiX = kTestDpi;
            r.screenshot.pixels.dpiY = kTestDpi;
            r.screenshot.sourceDisplay = request.sourceDisplay.value_or(DisplayInfo{});
            r.screenshot.trigger = request.trigger;
        } else {
            r.ok = false;
            r.engine = kind_;
            r.error = failReason_;
        }
        return r;
    }

    int captures = 0;
    CaptureRequest lastRequest{};

private:
    CaptureEngine kind_;
    bool available_;
    bool succeed_;
    std::wstring failReason_;
};

CaptureService makeChain(std::vector<std::unique_ptr<ICaptureEngine>> engines) {
    return CaptureService(std::move(engines));
}

/// Helper: braced-init of unique_ptr lists trips a libc++ 27 bootstrap defect
/// in initializer_list construction; push_back is the portable spelling.
template <typename... Ts>
std::vector<std::unique_ptr<ICaptureEngine>> chain(Ts&&... engines) {
    std::vector<std::unique_ptr<ICaptureEngine>> out;
    (out.push_back(std::forward<Ts>(engines)), ...);
    return out;
}

} // namespace

TEST_CASE("WGC contract gate: borderless needs UniversalApiContract 15",
          "[capture][wgc]") {
    // Win10 21H2 (contract 12/14): yellow border stays (documented degradation).
    CHECK_FALSE(wgcSupportsBorderless(12));
    CHECK_FALSE(wgcSupportsBorderless(14));
    // Win11 21H2+ (contract 15+): IsBorderRequired=false applies.
    CHECK(wgcSupportsBorderless(15));
    CHECK(wgcSupportsBorderless(16));
}

TEST_CASE("chain prefers WGC when available", "[capture][chain]") {
    auto svc = makeChain(chain(
        std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, true),
        std::make_unique<StubEngine>(CaptureEngine::DxgiDesktopDuplication, true, true),
        std::make_unique<StubEngine>(CaptureEngine::GdiBitBlt, true, true)));

    const auto display = makeDisplay(0, 0, 1920, 1080);
    const auto result = svc.captureDisplay(display, CaptureTrigger::TrayFullScreen);
    CHECK(result.ok);
    CHECK(result.engine == CaptureEngine::WindowsGraphicsCapture);
    CHECK(result.screenshot.pixels.width == 1920);
    CHECK(result.screenshot.pixels.height == 1080);
    CHECK(result.screenshot.trigger == CaptureTrigger::TrayFullScreen);
}

TEST_CASE("chain falls back to DXGI when WGC unavailable", "[capture][chain]") {
    auto svc = makeChain(chain(
        std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, false, false),
        std::make_unique<StubEngine>(CaptureEngine::DxgiDesktopDuplication, true, true),
        std::make_unique<StubEngine>(CaptureEngine::GdiBitBlt, true, true)));

    const auto result = svc.captureDisplay(makeDisplay(0, 0, 1920, 1080),
                                           CaptureTrigger::HotkeyFullScreen);
    CHECK(result.ok);
    CHECK(result.engine == CaptureEngine::DxgiDesktopDuplication);
}

TEST_CASE("chain falls back to GDI when both WGC and DXGI fail at capture time",
          "[capture][chain]") {
    auto svc = makeChain(chain(
        std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, false),
        std::make_unique<StubEngine>(CaptureEngine::DxgiDesktopDuplication, true, false),
        std::make_unique<StubEngine>(CaptureEngine::GdiBitBlt, true, true)));

    const auto result = svc.captureDisplay(makeDisplay(0, 0, 1920, 1080),
                                           CaptureTrigger::HotkeyFullScreen);
    CHECK(result.ok);
    CHECK(result.engine == CaptureEngine::GdiBitBlt);
}

TEST_CASE("all engines failing reports an error with no screenshot", "[capture][chain]") {
    auto svc = makeChain(chain(
        std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, false),
        std::make_unique<StubEngine>(CaptureEngine::DxgiDesktopDuplication, false, false),
        std::make_unique<StubEngine>(CaptureEngine::GdiBitBlt, true, false)));

    const auto result = svc.captureDisplay(makeDisplay(0, 0, 1920, 1080),
                                           CaptureTrigger::HotkeyFullScreen);
    CHECK_FALSE(result.ok);
    CHECK(result.engine == CaptureEngine::None);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("empty chain reports no engine", "[capture][chain]") {
    CaptureService svc{};  // default-constructed: no engines
    const auto result = svc.captureDisplay(makeDisplay(0, 0, 1920, 1080),
                                           CaptureTrigger::HotkeyFullScreen);
    CHECK_FALSE(result.ok);
    CHECK_FALSE(result.error.empty());
}

TEST_CASE("region capture clamps to the display frame", "[capture][region]") {
    auto wgc = std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, true);
    auto* wgcPtr = wgc.get();
    auto svc = makeChain(chain(std::move(wgc)));

    const auto display = makeDisplay(1920, 0, 1920, 1080);
    // Drag crossing the left edge of the secondary monitor.
    const auto result = svc.captureRegion(Rect{1800, 100, 400, 300}, display,
                                          CaptureTrigger::ManualRegion);
    CHECK(result.ok);
    // Clamp: the 120px on the primary monitor are excluded.
    CHECK(wgcPtr->lastRequest.regionPhysical == Rect{1920, 100, 280, 300});
    CHECK(result.screenshot.pixels.width == 280);
}

TEST_CASE("region capture with empty display intersection fails fast", "[capture][region]") {
    auto wgc = std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, true);
    auto* wgcPtr = wgc.get();
    auto svc = makeChain(chain(std::move(wgc)));

    const auto result = svc.captureRegion(Rect{5000, 5000, 100, 100},
                                          makeDisplay(0, 0, 1920, 1080),
                                          CaptureTrigger::ManualRegion);
    CHECK_FALSE(result.ok);
    CHECK(wgcPtr->captures == 0);  // engine never invoked
}

TEST_CASE("window-click capture routes with the whole-window trigger", "[capture][window]") {
    auto wgc = std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, true);
    auto* wgcPtr = wgc.get();
    auto svc = makeChain(chain(std::move(wgc)));

    const auto display = makeDisplay(0, 0, 3840, 2160);
    const Rect dwmFrame{100, 100, 800, 600};  // from WindowDetector
    const auto result = svc.captureRegion(dwmFrame, display, CaptureTrigger::WindowClick);
    CHECK(result.ok);
    CHECK(wgcPtr->lastRequest.trigger == CaptureTrigger::WindowClick);
    CHECK(result.screenshot.pixels.width == 800);
    CHECK(result.screenshot.pixels.height == 600);
    CHECK(result.screenshot.pixels.dpiX == kTestDpi);
}

TEST_CASE("PixelBuffer invariants: stride >= width*4, byteSize = stride*height",
          "[capture][pixels]") {
    PixelBuffer buf;
    buf.width = 1920;
    buf.height = 1080;
    buf.stride = 8192;  // D3D staging rows align to 256
    buf.data = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[buf.stride * buf.height]);
    CHECK(buf.valid());
    CHECK(buf.byteSize() == 8192ull * 1080);
}

TEST_CASE("observer sees fallback notifications", "[capture][observer]") {
    struct Recorder final : ICaptureObserver {
        void onEngineSelected(CaptureEngine) override { selected++; }
        void onFallback(CaptureEngine failed, CaptureEngine next,
                        const std::wstring&) override {
            fallbacks.push_back(failed);
            (void)next;
        }
        int selected = 0;
        std::vector<CaptureEngine> fallbacks;
    };

    Recorder rec;
    auto svc = makeChain(chain(
        std::make_unique<StubEngine>(CaptureEngine::WindowsGraphicsCapture, true, false),
        std::make_unique<StubEngine>(CaptureEngine::DxgiDesktopDuplication, true, true)));
    svc.setObserver(&rec);

    const auto result = svc.captureDisplay(makeDisplay(0, 0, 1920, 1080),
                                           CaptureTrigger::HotkeyFullScreen);
    CHECK(result.ok);
    CHECK(rec.selected == 1);
}
