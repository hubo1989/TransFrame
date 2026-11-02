// tf/CaptureService.hpp
// Screen capture with fallback chain (PRD §3.2):
//   1. Windows.Graphics.Capture  - GraphicsCaptureItem per monitor,
//      IsBorderRequired=false on contract 15+ (yellow border removed on Win11)
//   2. DXGI Desktop Duplication - when WGC unavailable / API contract failure
//   3. GDI BitBlt (SRCCOPY|CAPTUREBLT) - last-resort, always available
// Output: Core Screenshot (physical pixels + DPI metadata) - identical shape
// whichever engine ran, so OCR/translation layers never care.
#ifndef TF_CAPTURE_SERVICE_HPP
#define TF_CAPTURE_SERVICE_HPP

#include "CoreTypes.hpp"
#include "DisplayMonitor.hpp"
#include "Geo.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace tf {

enum class CaptureEngine : std::uint8_t {
    WindowsGraphicsCapture,  // preferred
    DxgiDesktopDuplication,  // fallback 1
    GdiBitBlt,               // fallback 2
    None,                    // all engines failed
};

struct CaptureRequest {
    Rect regionPhysical;      // physical px in virtual-desktop coordinates
    std::optional<DisplayInfo> sourceDisplay;  // display containing region origin
    CaptureTrigger trigger = CaptureTrigger::ManualRegion;
};

struct CaptureResult {
    bool ok = false;
    Screenshot screenshot;   // valid when ok
    CaptureEngine engine = CaptureEngine::None;
    std::wstring error;      // UTF-16 error description when !ok
};

/// Observer for the one-time WGC system consent banner (informational only).
class ICaptureObserver {
public:
    virtual ~ICaptureObserver() = default;
    virtual void onEngineSelected(CaptureEngine engine) = 0;
    virtual void onFallback(CaptureEngine failed, CaptureEngine next, const std::wstring& reason) = 0;
};

class ICaptureEngine {
public:
    virtual ~ICaptureEngine() = default;
    virtual CaptureEngine kind() const = 0;
    virtual bool isAvailable() = 0;
    /// Capture region (physical px, virtual-desktop coords) into BGRA8 buffer.
    virtual CaptureResult capture(const CaptureRequest& request) = 0;
};

/// Assembled fallback chain. Owns engines in priority order.
class CaptureService {
public:
    CaptureService();

    /// Dependency-injected variant for tests.
    explicit CaptureService(std::vector<std::unique_ptr<ICaptureEngine>> engines);

    ~CaptureService();

    CaptureService(CaptureService&&) noexcept;
    CaptureService& operator=(CaptureService&&) noexcept;
    CaptureService(const CaptureService&) = delete;
    CaptureService& operator=(const CaptureService&) = delete;

    /// Full-display capture (FR-03). Multi-monitor: caller passes the display.
    CaptureResult captureDisplay(const DisplayInfo& display, CaptureTrigger trigger);

    /// Region capture (FR-01/02): manual drag rect or whole-window DWM frame.
    /// Region is clipped to the source display's frame.
    CaptureResult captureRegion(const Rect& regionPhysical,
                                const DisplayInfo& sourceDisplay,
                                CaptureTrigger trigger);

    void setObserver(ICaptureObserver* observer) { observer_ = observer; }

    /// Engine that served the last successful capture (diagnostics).
    CaptureEngine lastEngine() const { return lastEngine_; }

private:
    CaptureResult runChain(const CaptureRequest& request);
    static Rect clampRegionToDisplay(const Rect& region, const DisplayInfo& display);

    std::vector<std::unique_ptr<ICaptureEngine>> engines_;
    ICaptureObserver* observer_ = nullptr;
    CaptureEngine lastEngine_ = CaptureEngine::None;
};

/// Detect WGC borderless support: GraphicsCaptureSession.IsBorderRequired
/// requires API contract 15 (Windows 11 21H2 / SDK 10.0.22000). Exported for
/// tests via injection; production reads Windows.Foundation.UniversalApiContract.
bool wgcSupportsBorderless(std::uint32_t universalApiContractVersion);

} // namespace tf

#endif // TF_CAPTURE_SERVICE_HPP
