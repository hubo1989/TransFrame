// DxgiDuplicationEngine.cpp — DXGI Desktop Duplication fallback (engine 2).
//
// Used when WGC is unavailable (API contract missing, group policy, capture
// consent denied). One output duplication per monitor output; capture =
// AcquireNextFrame with a short timeout, one CopyResource into the per-monitor
// staging cache, Map, crop region into the Core PixelBuffer.
//
// Cached-duplication semantics: the FIRST AcquireNextFrame after
// DuplicateOutput delivers the current desktop image, which is copied into
// the staging cache before ReleaseFrame. Later captures may hit
// DXGI_ERROR_WAIT_TIMEOUT (nothing changed since the last copy) — the cached
// copy is then exactly the current desktop and is served as-is, keeping
// static-screen captures instant without forcing desktop redraws. Access-lost
// / denied errors drop the state once and retry a fresh duplication before
// giving up; a clean failure lets the chain demote to GDI.
//
// The device must live on the adapter that owns the duplicated output (DXGI
// rule) — devices come from the shared per-adapter cache (CaptureD3dDevice.h).
// Non-BGRA8 desktop modes (HDR setups can deliver RGBA16F) are treated as
// unsupported and demote to GDI in v1.
#include "pch.h"

#include "tf/CaptureService.hpp"
#include "CaptureD3dDevice.h"

#include <chrono>
#include <mutex>
#include <unordered_map>

namespace tf {

// Defined at the bottom of this TU.
bool probeDxgiDuplicationAvailability();

namespace {

using Microsoft::WRL::ComPtr;

/// Warm-duplication wait (~2 vsyncs): a static desktop times out and serves
/// the cached copy; changing content delivers a fresh frame within a vsync.
constexpr UINT kAcquireTimeoutMs = 33;
/// First frame after DuplicateOutput (delivered immediately in practice).
constexpr UINT kFirstAcquireTimeoutMs = 250;

/// Match an HMONITOR to its DXGI output + owning adapter (factory enumeration;
/// output desc carries the HMONITOR it drives).
ComPtr<IDXGIOutput1> outputForMonitor(HMONITOR monitor, ComPtr<IDXGIAdapter1>* owner) {
    ComPtr<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return nullptr;
    }
    for (UINT a = 0;; ++a) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(a, adapter.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        for (UINT o = 0;; ++o) {
            ComPtr<IDXGIOutput> output;
            if (adapter->EnumOutputs(o, output.GetAddressOf()) == DXGI_ERROR_NOT_FOUND) {
                break;
            }
            DXGI_OUTPUT_DESC desc{};
            if (SUCCEEDED(output->GetDesc(&desc)) && desc.Monitor == monitor) {
                ComPtr<IDXGIOutput1> output1;
                if (SUCCEEDED(output.As(&output1))) {
                    if (owner) *owner = adapter;
                    return output1;
                }
            }
        }
    }
    return nullptr;
}

struct DuplicationState {
    ComPtr<IDXGIOutputDuplication> duplication;
    ComPtr<ID3D11Device> device;        // on the output's adapter
    ComPtr<ID3D11DeviceContext> context;
    // Current-desktop cache as a STAGING texture (one CopyResource per
    // acquired frame, Map+crop per capture — PRD §8 "GPU->CPU one copy").
    ComPtr<ID3D11Texture2D> staging;
    bool hasCurrent = false;
};

class DxgiEngine final : public ICaptureEngine {
public:
    CaptureEngine kind() const override { return CaptureEngine::DxgiDesktopDuplication; }

    bool isAvailable() override {
        // Cheap probe: DXGI factory + a usable D3D11 device (both cached).
        // Per-output capability (duplication rights, output format) is
        // discovered at capture time; failure there demotes to GDI.
        return probeDxgiDuplicationAvailability() && sharedD3d11Device() != nullptr;
    }

    CaptureResult capture(const CaptureRequest& request) override {
        CaptureResult out;
        out.engine = CaptureEngine::DxgiDesktopDuplication;
        try {
            out = captureOnce(request);
        } catch (const std::exception& e) {
            resetAll();
            out.ok = false;
            out.engine = CaptureEngine::DxgiDesktopDuplication;
            out.error = std::wstring(L"DXGI: ") + widen(e.what());
        } catch (...) {
            resetAll();
            out.ok = false;
            out.engine = CaptureEngine::DxgiDesktopDuplication;
            out.error = L"DXGI capture failed.";
        }
        return out;
    }

private:
    static std::wstring widen(const char* utf8) {
        if (!utf8) return L"DXGI capture failed.";
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (n <= 1) return L"DXGI capture failed.";
        std::wstring out(static_cast<std::size_t>(n - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
        return out;
    }

    // One duplication per output; captures are serialized under the engine
    // mutex (AcquireNextFrame/ReleaseFrame pairs must not interleave).
    CaptureResult captureOnce(const CaptureRequest& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        CaptureResult out;
        out.engine = CaptureEngine::DxgiDesktopDuplication;

        const Rect& region = request.regionPhysical;
        if (region.width <= 0 || region.height <= 0) {
            out.error = L"Empty capture region.";
            return out;
        }

        const POINT origin{static_cast<LONG>(region.x), static_cast<LONG>(region.y)};
        const HMONITOR monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!::GetMonitorInfoW(monitor, &mi)) {
            out.error = L"DXGI: GetMonitorInfo failed.";
            return out;
        }
        UINT dpiX = 96, dpiY = 96;
        if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
            dpiX = dpiY = 96;
        }
        const RECT& mon = mi.rcMonitor;

        DuplicationState* st = stateFor(monitor);
        if (!st || FAILED(acquireDesktop(*st))) {
            // Access lost (mode change / fullscreen exclusive / another
            // duplicator took the output): one fresh-duplication retry.
            states_.erase(monitor);
            st = stateFor(monitor);
            if (!st || FAILED(acquireDesktop(*st))) {
                states_.erase(monitor);
                out.error = L"DXGI: AcquireNextFrame failed.";
                return out;
            }
        }

        // Output-local crop, clamped to the cached desktop size.
        D3D11_TEXTURE2D_DESC stagingDesc{};
        st->staging->GetDesc(&stagingDesc);
        const Rect crop = Rect{region.x - mon.left, region.y - mon.top,
                               region.width, region.height}
                              .intersection(Rect{0, 0, static_cast<std::int64_t>(stagingDesc.Width),
                                                 static_cast<std::int64_t>(stagingDesc.Height)});
        if (crop.empty()) {
            out.error = L"DXGI: capture region outside the target monitor.";
            return out;
        }

        PixelBuffer pixels = cropFromStaging(st->context.Get(), st->staging.Get(), crop);
        if (!pixels.valid()) {
            out.error = L"DXGI: GPU->CPU readback failed.";
            return out;
        }
        pixels.dpiX = dpiX;
        pixels.dpiY = dpiY;

        out.ok = true;
        out.screenshot.pixels = std::move(pixels);
        out.screenshot.trigger = request.trigger;
        out.screenshot.capturedAt = std::chrono::system_clock::now();
        if (request.sourceDisplay) {
            out.screenshot.sourceDisplay = *request.sourceDisplay;
        } else {
            DisplayInfo fallback;
            fallback.id = mi.szDevice;
            fallback.name = mi.szDevice;
            fallback.framePhysical = Rect{mon.left, mon.top, mon.right - mon.left,
                                          mon.bottom - mon.top};
            fallback.dpiX = dpiX;
            fallback.dpiY = dpiY;
            fallback.isPrimary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
            out.screenshot.sourceDisplay = std::move(fallback);
        }
        return out;
    }

    /// Bring `st.staging` up to date. S_OK when the cache holds the current
    /// desktop (freshly acquired or unchanged-since-last-copy); failure codes
    /// mean the duplication needs a rebuild.
    HRESULT acquireDesktop(DuplicationState& st) {
        DXGI_OUTDUPL_FRAME_INFO info{};
        ComPtr<IDXGIResource> resource;
        const UINT timeout = st.hasCurrent ? kAcquireTimeoutMs : kFirstAcquireTimeoutMs;
        HRESULT hr = st.duplication->AcquireNextFrame(timeout, &info, resource.GetAddressOf());
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No change since our copy — the copy IS the current desktop.
            return st.hasCurrent ? S_OK : hr;
        }
        if (FAILED(hr)) return hr;

        // Copy the full image into the staging cache before ReleaseFrame
        // invalidates it (one GPU->CPU-path copy; Map happens per capture).
        ComPtr<ID3D11Texture2D> desktop;
        hr = resource.As(&desktop);
        if (SUCCEEDED(hr)) {
            if (!copyToStagingCache(st.device.Get(), st.context.Get(), desktop.Get(),
                                    st.staging)) {
                hr = E_FAIL;
            } else {
                st.hasCurrent = true;
            }
        }
        const HRESULT release = st.duplication->ReleaseFrame();
        if (FAILED(hr)) return hr;
        if (!st.hasCurrent) return E_FAIL;  // nothing copied and no cache
        return release;
    }

    /// Find or build the per-monitor duplication. Null on failure.
    DuplicationState* stateFor(HMONITOR monitor) {
        auto it = states_.find(monitor);
        if (it != states_.end() && it->second.duplication) return &it->second;
        states_.erase(monitor);

        ComPtr<IDXGIAdapter1> adapter;
        const ComPtr<IDXGIOutput1> output = outputForMonitor(monitor, &adapter);
        if (!output) return nullptr;

        DuplicationState st;
        st.device = d3d11DeviceForAdapter(adapter.Get());
        if (!st.device) return nullptr;
        st.device->GetImmediateContext(st.context.GetAddressOf());

        ComPtr<IDXGIOutputDuplication> duplication;
        // E_ACCESSDENIED (another process owns the duplication / not
        // permitted) and DXGI_ERROR_UNSUPPORTED land here -> GDI.
        if (FAILED(output->DuplicateOutput(st.device.Get(), duplication.GetAddressOf()))) {
            return nullptr;
        }
        DXGI_OUTDUPL_DESC dupDesc{};
        duplication->GetDesc(&dupDesc);
        if (dupDesc.ModeDesc.Format != DXGI_FORMAT_B8G8R8A8_UNORM) {
            return nullptr;  // HDR RGBA16F desktops demote to GDI (v1)
        }

        st.duplication = std::move(duplication);
        return &states_.emplace(monitor, std::move(st)).first->second;
    }

    void resetAll() {
        std::lock_guard<std::mutex> lock(mutex_);
        states_.clear();
    }

    std::mutex mutex_;
    std::unordered_map<HMONITOR, DuplicationState> states_;
};

} // namespace

bool probeDxgiDuplicationAvailability() {
    // Desktop Duplication requires D3D11 + a duplicatable output; the cheap
    // probe is DXGI factory creation. Full output enumeration happens in the
    // engine itself; failure there demotes to GDI.
    Microsoft::WRL::ComPtr<IDXGIFactory1> factory;
    if (FAILED(::CreateDXGIFactory1(__uuidof(IDXGIFactory1),
                                    reinterpret_cast<void**>(factory.GetAddressOf())))) {
        return false;
    }
    return true;
}

std::unique_ptr<ICaptureEngine> makeDxgiDuplicationEngine() {
    return std::make_unique<DxgiEngine>();
}

} // namespace tf
