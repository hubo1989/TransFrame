// CaptureService — engine chain + shared capture plumbing (portable core).
// Engine implementations: WGCCaptureEngine.cpp / DxgiDuplicationEngine.cpp /
// GdiCaptureEngine.cpp (Windows-only TUs).
#include "tf/CaptureService.hpp"

#include <algorithm>

namespace tf {

bool wgcSupportsBorderless(std::uint32_t universalApiContractVersion) {
    // GraphicsCaptureSession.IsBorderRequired is introduced in
    // Windows.Foundation.UniversalApiContract version 15 (Windows 11 21H2,
    // SDK 10.0.22000). Below that the property simply does not exist and the
    // yellow border stays (Win10 degradation path, PRD §6.13).
    return universalApiContractVersion >= 15;
}

namespace {

// Pixel allocation lives in the engine TUs (WGC/DXGI/GDI) where the staging
// stride is known; the chain itself never allocates.

} // namespace

CaptureService::CaptureService() = default;

CaptureService::CaptureService(std::vector<std::unique_ptr<ICaptureEngine>> engines)
    : engines_(std::move(engines)) {}

CaptureService::~CaptureService() = default;

CaptureService::CaptureService(CaptureService&&) noexcept = default;
CaptureService& CaptureService::operator=(CaptureService&&) noexcept = default;

Rect CaptureService::clampRegionToDisplay(const Rect& region, const DisplayInfo& display) {
    const Rect clipped = region.intersection(display.framePhysical);
    return clipped.empty() ? Rect{} : clipped;
}

CaptureResult CaptureService::captureDisplay(const DisplayInfo& display, CaptureTrigger trigger) {
    CaptureRequest req;
    req.regionPhysical = display.framePhysical;
    req.sourceDisplay = display;
    req.trigger = trigger;
    return runChain(req);
}

CaptureResult CaptureService::captureRegion(const Rect& regionPhysical,
                                            const DisplayInfo& sourceDisplay,
                                            CaptureTrigger trigger) {
    CaptureRequest req;
    req.regionPhysical = clampRegionToDisplay(regionPhysical, sourceDisplay);
    if (req.regionPhysical.empty()) {
        CaptureResult fail;
        fail.error = L"Capture region has empty intersection with the source display.";
        return fail;
    }
    req.sourceDisplay = sourceDisplay;
    req.trigger = trigger;
    return runChain(req);
}

CaptureResult CaptureService::runChain(const CaptureRequest& request) {
    if (engines_.empty()) {
        CaptureResult fail;
        fail.error = L"No capture engine available.";
        return fail;
    }
    CaptureEngine previous = CaptureEngine::None;
    for (auto& engine : engines_) {
        if (!engine->isAvailable()) {
            if (observer_) observer_->onFallback(engine->kind(), CaptureEngine::None,
                                                 L"engine unavailable");
            previous = engine->kind();
            continue;
        }
        CaptureResult result = engine->capture(request);
        if (result.ok) {
            lastEngine_ = result.engine;
            if (observer_) observer_->onEngineSelected(result.engine);
            return result;
        }
        if (observer_ && previous != CaptureEngine::None) {
            observer_->onFallback(previous, CaptureEngine::None, result.error);
        }
        previous = engine->kind();
    }
    CaptureResult fail;
    fail.engine = CaptureEngine::None;
    fail.error = L"All capture engines failed.";
    return fail;
}

} // namespace tf
