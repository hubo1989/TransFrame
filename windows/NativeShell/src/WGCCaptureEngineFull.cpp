// WGC engine — full implementation (frame pool per monitor, one-shot capture).
// This TU is compiled into NativeShell (Windows only).
//
// Per-monitor GraphicsCaptureItem + Direct3D11CaptureFramePool (free-threaded,
// 2 buffers) + GraphicsCaptureSession are created lazily on first capture and
// kept warm. WGC only composes when desktop content CHANGES, so a fresh frame
// is not always available: each capture first drains whatever the pool holds,
// then waits briefly (~1 vsync, kFreshFrameWait) for a newer frame; when none
// arrives the desktop is static and the cached copy IS the current desktop —
// the same semantics as the DXGI engine's last-frame cache. The cache is a
// STAGING texture: a refresh is one CopyResource from the newest frame, a
// capture is Map + row-crop (PRD §8 "GPU->CPU one copy"; a static desktop
// costs no GPU work at all). The first capture pays session start-up (DWM tap
// spin-up), bounded by kFirstFrameWait and reported separately by the smoke
// tool.
//
// Platform notes:
//   * CreateFreeThreaded: callers are background threads without a
//     DispatcherQueue; FrameArrived fires on a thread-pool thread and only
//     sets a flag (never takes the engine mutex — captures would otherwise
//     stall behind a busy compositor). The pool is ring-buffered: unconsumed
//     frames are silently overwritten, so it never stalls.
//   * IsBorderRequired(false): contract 15+ (see wgcSupportsBorderless) and
//     additionally gated by an IsPropertyPresent check plus try/catch —
//     non-packaged processes can be denied at runtime. Degrade to keeping
//     the system yellow border (documented, PRD §6.13) instead of failing.
//   * WGC delivers premultiplied BGRA; desktop content is opaque, so the
//     alpha byte lands as 255 (parity with the GDI engine's output).
#include "pch.h"

#include "tf/CaptureService.hpp"
#include "CaptureD3dDevice.h"

#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.interop.h>

#include <winrt/Windows.Foundation.Metadata.h>

#include <chrono>
#include <condition_variable>
#include <exception>
#include <mutex>
#include <string>
#include <unordered_map>

namespace tf {
namespace wf = ::winrt::Windows::Foundation;
namespace wgc = ::winrt::Windows::Graphics::Capture;
namespace wdx = ::winrt::Windows::Graphics::DirectX;
namespace wdx3d = ::winrt::Windows::Graphics::DirectX::Direct3D11;

namespace {

using Microsoft::WRL::ComPtr;

// Fresh-frame wait ~= one vsync: catches updates in flight without adding a
// full frame period to static-screen captures. PRD §8 budget is 50ms for 4K.
constexpr std::chrono::milliseconds kFreshFrameWait{16};
// Warm-up (first frame after StartCapture): DWM tap spin-up, outside the
// steady-state budget and reported separately by the smoke tool.
constexpr std::chrono::milliseconds kFirstFrameWait{400};
constexpr UINT kPoolBuffers = 2;

/// Give this thread an MTA if it has none. RPC_E_CHANGED_MODE (thread already
/// picked an apartment, e.g. a UI STA) is harmless — the pool is free-threaded.
void ensureWinrtApartment() {
    thread_local bool ready = false;
    if (ready) return;
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (const winrt::hresult_error&) {
        // Apartment already initialized in some mode — usable as is.
    } catch (...) {
    }
    ready = true;
}

/// Classic-COM interop activation: the projection exposes no direct ctor for
/// a monitor item, only IGraphicsCaptureItemInterop::CreateForMonitor does.
/// Throws (hresult_error) when monitor capture is disallowed — capture()
/// catches and the chain demotes to DXGI/GDI.
wgc::GraphicsCaptureItem itemForMonitor(HMONITOR monitor) {
    auto interop = winrt::get_activation_factory<wgc::GraphicsCaptureItem,
                                                 ::IGraphicsCaptureItemInterop>();
    wgc::GraphicsCaptureItem item{nullptr};
    winrt::check_hresult(interop->CreateForMonitor(
        monitor, winrt::guid_of<wgc::GraphicsCaptureItem>(), winrt::put_abi(item)));
    return item;
}

/// Unwrap a WinRT capture surface to the underlying D3D texture.
winrt::com_ptr<ID3D11Texture2D> textureFromSurface(const wdx3d::IDirect3DSurface& surface) {
    auto access =
        surface.as<::Windows::Graphics::DirectX::Direct3D11::IDirect3DDxgiInterfaceAccess>();
    winrt::com_ptr<ID3D11Texture2D> texture;
    winrt::check_hresult(
        access->GetInterface(winrt::guid_of<ID3D11Texture2D>(), texture.put_void()));
    return texture;
}

/// Borderless capture when the platform allows it; presence check + try/catch
/// because the setter can be refused at runtime (e.g. process without package
/// identity). Refusal keeps the yellow border — never fails the capture.
void tryDisableBorder(const wgc::GraphicsCaptureSession& session) {
    try {
        if (wf::Metadata::ApiInformation::IsPropertyPresent(
                L"Windows.Graphics.Capture.GraphicsCaptureSession", L"IsBorderRequired")) {
            session.IsBorderRequired(false);
        }
    } catch (...) {
        // Denied: the system border stays on (PRD §6.13 degradation).
    }
}

struct MonitorItem {
    HMONITOR monitor = nullptr;
    wgc::GraphicsCaptureItem item{nullptr};
    wgc::Direct3D11CaptureFramePool pool{nullptr};
    wgc::GraphicsCaptureSession session{nullptr};
    // WinRT IDirect3DDevice created from the shared D3D11 device; must
    // outlive the pool (frame surfaces are allocated on it).
    wdx3d::IDirect3DDevice device{nullptr};
    winrt::event_token frameArrivedToken{};
    // Current-desktop cache as a STAGING texture: refreshed by one
    // CopyResource from the newest pool frame; captures Map it and crop
    // directly (PRD §8 "GPU->CPU one copy"). Static desktop => no new
    // frames => the cache IS the current desktop, no GPU work at all.
    ComPtr<ID3D11Texture2D> staging;
    bool hasCurrent = false;
    // Pool creation size; a frame content-size mismatch marks the state
    // stale (mode change) and rebuilds it on the next capture.
    std::int64_t createdW = 0, createdH = 0;
    // FrameArrived handoff (handler runs on a thread-pool thread).
    std::mutex frameMutex;
    std::condition_variable frameCv;
    bool framePending = false;
};

class WGCEngine final : public ICaptureEngine {
public:
    CaptureEngine kind() const override { return CaptureEngine::WindowsGraphicsCapture; }

    bool isAvailable() override {
        ensureWinrtApartment();
        try {
            // Direct3D11CaptureFramePool rides UniversalApiContract 12
            // (Win10 1803+); GraphicsCaptureSession::IsSupported() covers
            // policy/SKU removals.
            if (!wf::Metadata::ApiInformation::IsApiContractPresent(
                    L"Windows.Foundation.UniversalApiContract", 12)) {
                return false;
            }
            return wgc::GraphicsCaptureSession::IsSupported();
        } catch (...) {
            return false;
        }
    }

    CaptureResult capture(const CaptureRequest& request) override {
        CaptureResult out;
        out.engine = CaptureEngine::WindowsGraphicsCapture;
        ensureWinrtApartment();
        try {
            out = captureOnce(request);
        } catch (const winrt::hresult_error& e) {
            // Transient or fatal session failures drop the cached state so
            // the next attempt rebuilds; this capture demotes down the chain.
            resetFor(request);
            out.ok = false;
            out.engine = CaptureEngine::WindowsGraphicsCapture;
            out.error = std::wstring(L"WGC: ") + e.message().c_str();
        } catch (const std::exception& e) {
            resetFor(request);
            out.ok = false;
            out.engine = CaptureEngine::WindowsGraphicsCapture;
            out.error = widen(e.what());
        } catch (...) {
            resetFor(request);
            out.ok = false;
            out.engine = CaptureEngine::WindowsGraphicsCapture;
            out.error = L"WGC capture failed.";
        }
        return out;
    }

private:
    static std::wstring widen(const char* utf8) {
        if (!utf8) return L"WGC capture failed.";
        const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, nullptr, 0);
        if (n <= 1) return L"WGC capture failed.";
        std::wstring out(static_cast<std::size_t>(n - 1), L'\0');
        ::MultiByteToWideChar(CP_UTF8, 0, utf8, -1, out.data(), n);
        return out;
    }

    /// Tear a state down before map erase: revoke the FrameArrived handler
    /// first so the thread-pool callback cannot touch a dead MonitorItem.
    /// (States are heap-anchored via unique_ptr, so the handler's captured
    /// pointer stays valid until here.)
    static void destroyState(MonitorItem& st) {
        if (st.pool) {
            try {
                st.pool.FrameArrived(st.frameArrivedToken);  // best-effort revoke
            } catch (...) {
            }
        }
        st.session = nullptr;
        st.pool = nullptr;
        st.item = nullptr;
        st.device = nullptr;
        st.staging.Reset();
        st.hasCurrent = false;
    }

    void dropState(HMONITOR monitor) {
        if (auto it = states_.find(monitor); it != states_.end()) {
            destroyState(*it->second);
            states_.erase(it);
        }
    }

    void resetFor(const CaptureRequest& request) {
        const POINT origin{static_cast<LONG>(request.regionPhysical.x),
                           static_cast<LONG>(request.regionPhysical.y)};
        const HMONITOR monitor = ::MonitorFromPoint(origin, MONITOR_DEFAULTTONEAREST);
        std::lock_guard<std::mutex> lock(mutex_);
        dropState(monitor);
    }

    // A capture runs under the engine mutex: one-shot captures are sequential
    // in practice (CaptureService::runChain) and this keeps the per-monitor
    // state race-free.
    CaptureResult captureOnce(const CaptureRequest& request) {
        std::lock_guard<std::mutex> lock(mutex_);
        CaptureResult out;
        out.engine = CaptureEngine::WindowsGraphicsCapture;

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
            out.error = L"WGC: GetMonitorInfo failed.";
            return out;
        }
        UINT dpiX = 96, dpiY = 96;
        if (FAILED(::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY))) {
            dpiX = dpiY = 96;
        }
        const RECT& mon = mi.rcMonitor;  // physical px, virtual-desktop coords

        const ComPtr<ID3D11Device> d3d = sharedD3d11Device();
        ComPtr<ID3D11DeviceContext> context;
        if (!d3d) {
            out.error = L"WGC: D3D11 device unavailable.";
            return out;
        }
        d3d->GetImmediateContext(&context);  // void return, always succeeds

        MonitorItem* state = stateFor(monitor, d3d.Get());
        if (!state) {
            dropState(monitor);
            out.error = L"WGC: capture item/session creation failed.";
            return out;
        }

        const RefreshResult refreshed =
            refreshCurrent(*state, d3d.Get(), context.Get());
        if (!refreshed.ok) {
            dropState(monitor);
            out.error = refreshed.error;
            return out;
        }

        // Monitor-local crop clamped to the cached texture.
        D3D11_TEXTURE2D_DESC stagingDesc{};
        state->staging->GetDesc(&stagingDesc);
        const Rect crop = Rect{region.x - mon.left, region.y - mon.top,
                               region.width, region.height}
                              .intersection(Rect{0, 0, static_cast<std::int64_t>(stagingDesc.Width),
                                                 static_cast<std::int64_t>(stagingDesc.Height)});
        if (crop.empty()) {
            out.error = L"WGC: capture region outside the target monitor.";
            return out;
        }

        PixelBuffer pixels = cropFromStaging(context.Get(), state->staging.Get(), crop);
        if (!pixels.valid()) {
            out.error = L"WGC: GPU->CPU readback failed.";
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

        if (refreshed.stale) dropState(monitor);  // mode change: rebuild next time
        return out;
    }

    struct RefreshResult {
        bool ok = false;
        bool stale = false;  // frame content size != pool creation size
        const wchar_t* error = nullptr;
    };

    /// Bring `st.staging` up to date. Drains whatever the pool holds, keeps
    /// only the newest frame and copies it into the staging cache while we
    /// hold it (the sanctioned WGC pattern — one GPU->CPU-path copy per
    /// refresh, never per drained frame). Cold cache: must land SOMETHING
    /// within the warm-up budget. Warm cache: wait up to one vsync for an
    /// in-flight update; on timeout the desktop is static and the cached
    /// copy is served as-is.
    RefreshResult refreshCurrent(MonitorItem& st, ID3D11Device* device,
                                 ID3D11DeviceContext* context) {
        RefreshResult result;
        const auto deadline =
            std::chrono::steady_clock::now() +
            (st.hasCurrent ? kFreshFrameWait : kFirstFrameWait);

        for (;;) {
            // 1) Drain the ring, keeping the newest frame (older frame
            //    objects drop and their buffers return to the pool).
            wgc::Direct3D11CaptureFrame newest{nullptr};
            while (const wgc::Direct3D11CaptureFrame next = st.pool.TryGetNextFrame()) {
                newest = next;
            }
            if (newest) {
                const winrt::com_ptr<ID3D11Texture2D> texture =
                    textureFromSurface(newest.Surface());
                if (!copyToStagingCache(device, context, texture.get(), st.staging)) {
                    result.error = L"WGC: staging cache allocation failed.";
                    return result;
                }
                st.hasCurrent = true;

                const auto content = newest.ContentSize();
                result.stale =
                    content.Width != st.createdW || content.Height != st.createdH;
            }

            // 2) Fresh frame in hand, or warm cache with nothing newer by
            //    now: serve what we have.
            if (newest || (st.hasCurrent && std::chrono::steady_clock::now() >= deadline)) {
                if (st.hasCurrent) {
                    result.ok = true;
                    return result;
                }
            }

            // 3) Cold cache past the budget = failure; otherwise wait for
            //    FrameArrived (or the deadline) and drain again.
            if (std::chrono::steady_clock::now() >= deadline) break;
            std::unique_lock<std::mutex> lock(st.frameMutex);
            st.frameCv.wait_until(lock, deadline, [&] { return st.framePending; });
            st.framePending = false;  // handler re-flags on the next arrival
        }
        result.error = L"WGC: no frame arrived within the time budget.";
        return result;
    }

    /// Find or build the per-monitor item/pool/session. Returns null on
    /// failure (item creation refused, pool creation failed).
    MonitorItem* stateFor(HMONITOR monitor, ID3D11Device* d3d) {
        auto it = states_.find(monitor);
        if (it != states_.end()) return it->second.get();

        const wgc::GraphicsCaptureItem item = itemForMonitor(monitor);
        const auto size = item.Size();

        // Heap-anchored: MonitorItem is not movable (mutex + condvar), and
        // the FrameArrived handler captures this stable address.
        auto built = std::make_unique<MonitorItem>();
        MonitorItem& st = *built;

        ComPtr<IDXGIDevice> dxgiDevice;
        winrt::com_ptr<::IInspectable> inspectable;
        if (FAILED(d3d->QueryInterface(IID_PPV_ARGS(&dxgiDevice))) ||
            FAILED(::CreateDirect3D11DeviceFromDXGIDevice(dxgiDevice.Get(),
                                                          inspectable.put()))) {
            return nullptr;
        }
        st.device = inspectable.as<wdx3d::IDirect3DDevice>();
        st.pool = wgc::Direct3D11CaptureFramePool::CreateFreeThreaded(
            st.device, wdx::DirectXPixelFormat::B8G8R8A8UIntNormalized, kPoolBuffers,
            size);
        st.session = st.pool.CreateCaptureSession(item);
        tryDisableBorder(st.session);
        st.monitor = monitor;
        st.item = item;
        st.createdW = size.Width;
        st.createdH = size.Height;

        // Register before StartCapture so the very first frame cannot slip
        // through unnoticed; a missed event still recovers because the drain
        // in refreshCurrent sees buffered frames regardless.
        st.frameArrivedToken = st.pool.FrameArrived(
            [&st](const wgc::Direct3D11CaptureFramePool&, const wf::IInspectable&) {
                std::lock_guard<std::mutex> lock(st.frameMutex);
                st.framePending = true;
                st.frameCv.notify_all();
            });
        st.session.StartCapture();

        MonitorItem* anchored = built.get();
        states_.emplace(monitor, std::move(built));
        return anchored;
    }

    std::mutex mutex_;
    std::unordered_map<HMONITOR, std::unique_ptr<MonitorItem>> states_;
};

} // namespace

std::unique_ptr<ICaptureEngine> makeWgcCaptureEngine() {
    return std::make_unique<WGCEngine>();
}

} // namespace tf
