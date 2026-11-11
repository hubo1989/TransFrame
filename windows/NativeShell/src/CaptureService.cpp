// CaptureService — engine chain + shared capture plumbing (portable core).
// Engine implementations: WGCCaptureEngine.cpp / DxgiDuplicationEngine.cpp /
// GdiCaptureEngine.cpp (Windows-only TUs).
#include "tf/CaptureService.hpp"
#ifdef _WIN32
#include "tf/PlatformFeatures.hpp"
#endif

#include <algorithm>
#include <cstring>

namespace tf {

/// Belt-and-braces: the OS build probe (PRD §6.13 policy layer) must agree
/// with the contract version — a downlevel OS must never claim borderless
/// even if a shim reports a higher contract. Only meaningful on Windows;
/// the portable leg (GCC) has no OS to ask, so the pure contract gate stands
/// alone there.
static bool borderlessOsGate() {
#ifdef _WIN32
    return tf::supportsBorderlessCapture();
#else
    return true;
#endif
}


bool wgcSupportsBorderless(std::uint32_t universalApiContractVersion) {
    // GraphicsCaptureSession.IsBorderRequired is introduced in
    // Windows.Foundation.UniversalApiContract version 15 (Windows 11 21H2,
    // SDK 10.0.22000). Below that the property simply does not exist and the
    // yellow border stays (Win10 degradation path, PRD §6.13).
    return universalApiContractVersion >= 15 && borderlessOsGate();
}

std::shared_ptr<std::uint8_t[]> copyBgraCrop(const std::uint8_t* src,
                                             std::int32_t srcStride,
                                             std::int32_t srcWidth,
                                             std::int32_t srcHeight,
                                             const Rect& crop,
                                             std::int32_t& outStride) {
    outStride = 0;
    if (!src || srcStride <= 0 || srcWidth <= 0 || srcHeight <= 0) return nullptr;
    const Rect clipped = crop.intersection(Rect{0, 0, srcWidth, srcHeight});
    if (clipped.empty()) return nullptr;

    // Tight rows: the Core buffer carries no row padding (unlike the D3D
    // staging textures / DIBs this copies from).
    const std::int64_t rowBytes = clipped.width * 4;
    outStride = static_cast<std::int32_t>(rowBytes);
    auto out = std::shared_ptr<std::uint8_t[]>(
        new std::uint8_t[static_cast<std::size_t>(rowBytes * clipped.height)]);
    const std::uint8_t* srcRow = src + static_cast<std::size_t>(clipped.y) * srcStride +
                                 static_cast<std::size_t>(clipped.x) * 4;
    std::uint8_t* dst = out.get();
    for (std::int64_t y = 0; y < clipped.height; ++y) {
        std::memcpy(dst, srcRow + y * srcStride, static_cast<std::size_t>(rowBytes));
        dst += rowBytes;
    }
    return out;
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
