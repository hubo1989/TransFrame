// SelectionOverlay.cpp — Win32 overlay window + D2D 1.1 / DComp renderer.
//
// Window: one WS_POPUP | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE |
// WS_EX_TOOLWINDOW window sized to the virtual desktop (SM_XVIRTUALSCREEN..),
// pre-created hidden at app start. Hotkey path: ShowWindow + first frame
// (<= 60ms budget: no window creation, no device creation on the hot path).
//
// Rendering: ID2D1DeviceContext with ID2D1Bitmap1 (D3D11 texture,
// D2D1_BITMAP_OPTIONS_TARGET), one DComp visual tree. Each frame:
//   1. fill desktop rect with 40% black
//   2. punch through the selection (composite destination-out or redraw
//      un-dimmed content region) + 1px accent stroke
//   3. window highlight (accent fill 25% + stroke)
//   4. size badge, crosshair, toolbar skeleton
// The D2D1Device is shared with the capture engine's D3D11 device.
//
// FR-02 (window click): a WindowDetectorService is attached by the App;
// mouse-move hover hit-tests against its snapshot (refreshed at mouse-down).
#include "pch.h"

#include "tf/SelectionOverlay.hpp"
#include "tf/OverlayRenderer.hpp"
#include "tf/WindowDetector.hpp"

#include <algorithm>
#include <d2d1_1.h>
#include <dcomp.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace tf {

using Microsoft::WRL::ComPtr;

struct SelectionOverlayController::Impl {
    HWND hwnd = nullptr;
    static constexpr wchar_t kClassName[] = L"TransFrameOverlayWnd";
    bool registered = false;
    OverlayState state;
    OverlayCompletion completion;
    bool sessionActive = false;
    WindowDetectorService* windowDetector = nullptr;  // FR-02 hover (App-owned)

    // D2D/DComp device stack.
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<IDXGIDevice> dxgiDevice;
    // ID2D1Factory1 (d2d1_1.h) carries CreateDevice; ID2D1Factory2 only
    // exists from d2d1_3.h and adds nothing this overlay needs.
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> d2dContext;
    ComPtr<IDCompositionDevice> dcompDevice;
    ComPtr<IDCompositionTarget> dcompTarget;
    ComPtr<IDCompositionVisual> dcompVisual;
    ComPtr<ID2D1Bitmap1> d2dTargetBitmap;  // DComp surface-backed
    ComPtr<IDXGISwapChain1> swapChain;

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            self = static_cast<Impl*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<Impl*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
        }
        if (self) return self->handleMessage(h, msg, wp, lp);
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    LRESULT handleMessage(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_MOUSEMOVE: {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                // Mouse messages carry client coordinates; the overlay spans
                // the virtual desktop, so with any display left of/above the
                // origin the client origin differs from the screen origin.
                // State and the window detector both work in screen coords.
                ::ClientToScreen(h, &pt);
                state.moveTracking({pt.x, pt.y});
                // FR-02: hover highlight while idle (drag clears it in
                // OverlayState::moveTracking via dragEngaged_).
                if (windowDetector && !state.isDragEngaged()) {
                    if (const auto win = windowDetector->windowUnderPoint({pt.x, pt.y})) {
                        state.setHoverWindow(win->frame);
                    } else {
                        state.setHoverWindow(std::nullopt);
                    }
                }
                renderFrame();
                return 0;
            }
            case WM_LBUTTONDOWN: {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ::ClientToScreen(h, &pt);  // tracking + detector use screen coords
                if (windowDetector) windowDetector->refreshSnapshot();  // cache at mouse-down
                state.beginTracking({pt.x, pt.y});
                renderFrame();
                ::SetCapture(h);
                return 0;
            }
            case WM_LBUTTONUP: {
                if (::GetCapture() == h) ::ReleaseCapture();
                state.endTracking();
                finishSession(h);
                return 0;
            }
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE) {
                    state.cancel();
                    if (completion) completion({false, {}, false, 0});
                    completion = nullptr;
                    sessionActive = false;
                    ::ShowWindow(h, SW_HIDE);
                    return 0;
                }
                break;
            case WM_KILLFOCUS:
                // Static wndProc dispatches into Impl::handleMessage (a
                // non-static member); the controller-level cancel is done by
                // the session owner (HotkeyManager/App), not here.
                state.cancel();
                if (completion) {
                    completion({false, {}, false, 0});
                    completion = nullptr;
                }
                sessionActive = false;
                ::ShowWindow(h, SW_HIDE);
                return 0;
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    void finishSession(HWND h) {
        const bool drag = state.isDragEngaged();
        const auto selection = state.selection();
        const auto hover = state.hoverWindow();
        ::ShowWindow(h, SW_HIDE);
        if (!completion) { sessionActive = false; return; }

        if (drag && selection && !selection->empty()) {
            SelectionOverlayResult r;
            r.confirmed = true;
            r.regionPhysical = *selection;
            r.wholeWindow = false;
            completion(std::move(r));
        } else if (!drag && hover) {
            // FR-02: click (no drag) -> whole window under cursor.
            SelectionOverlayResult r;
            r.confirmed = true;
            r.regionPhysical = *hover;
            r.wholeWindow = true;
            completion(std::move(r));
        } else {
            completion({false, {}, false, 0});
        }
        completion = nullptr;
        sessionActive = false;
    }

    bool createDeviceStack() {
        // D3D11 (BGRA support is mandatory for D2D interop). Hardware first;
        // WARP fallback — sessions without a usable GPU (RDP, restricted
        // drivers) fail D3D_DRIVER_TYPE_HARDWARE, which used to kill the
        // overlay silently. WARP is plenty for filled-rect frames.
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL fl{};
        HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                         flags, nullptr, 0, D3D11_SDK_VERSION,
                                         d3dDevice.GetAddressOf(), &fl, nullptr);
        if (FAILED(hr)) {
            hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                     nullptr, 0, D3D11_SDK_VERSION,
                                     d3dDevice.GetAddressOf(), &fl, nullptr);
        }
        if (FAILED(hr)) return false;
        d3dDevice.As(&dxgiDevice);

        // D2D 1.1 factory + device.
        D2D1_FACTORY_OPTIONS fo{};
        if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                       __uuidof(ID2D1Factory1),
                                       &fo, reinterpret_cast<void**>(d2dFactory.GetAddressOf())))) {
            return false;
        }
        if (FAILED(d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf()))) return false;
        if (FAILED(d2dDevice->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf()))) {
            return false;
        }

        // DirectComposition device + target bound to our HWND.
        if (FAILED(::DCompositionCreateDevice(
                dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                reinterpret_cast<void**>(dcompDevice.GetAddressOf())))) {
            return false;
        }
        if (FAILED(dcompDevice->CreateTargetForHwnd(hwnd, TRUE, dcompTarget.GetAddressOf()))) {
            return false;
        }
        if (FAILED(dcompDevice->CreateVisual(dcompVisual.GetAddressOf()))) return false;
        if (FAILED(dcompTarget->SetRoot(dcompVisual.Get()))) return false;

        // Swap chain (flip model) for the DComp content, 2 buffers.
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width = 1; scd.Height = 1;  // resized in createTargetBitmap
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        scd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;  // WS_EX_LAYERED via DComp
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        dxgiDevice->GetAdapter(adapter.GetAddressOf());
        adapter->GetParent(__uuidof(IDXGIFactory2),
                           reinterpret_cast<void**>(factory.GetAddressOf()));
        if (FAILED(factory->CreateSwapChainForComposition(
                d3dDevice.Get(), &scd, nullptr, swapChain.GetAddressOf()))) {
            return false;
        }
        if (FAILED(dcompVisual->SetContent(swapChain.Get()))) return false;
        return createTargetBitmap();
    }

    bool createTargetBitmap() {
        DXGI_SWAP_CHAIN_DESC1 desc{};
        swapChain->GetDesc1(&desc);
        const D2D1_SIZE_U size{desc.Width, desc.Height};
        const auto props = D2D1::BitmapProperties1(
            D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        ComPtr<IDXGISurface> surface;
        if (FAILED(swapChain->GetBuffer(0, __uuidof(IDXGISurface),
                                        reinterpret_cast<void**>(surface.GetAddressOf())))) {
            return false;
        }
        if (FAILED(d2dContext->CreateBitmapFromDxgiSurface(
                surface.Get(), props, d2dTargetBitmap.GetAddressOf()))) {
            return false;
        }
        d2dContext->SetTarget(d2dTargetBitmap.Get());
        return true;
    }

    void resizeToVirtualDesktop() {
        const int x = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
        const int y = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int w = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const int h = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
        ::SetWindowPos(hwnd, HWND_TOPMOST, x, y, w, h, SWP_NOACTIVATE);
        state.setDesktop(Rect{x, y, w, h});
        DXGI_SWAP_CHAIN_DESC1 desc{};
        swapChain->GetDesc1(&desc);
        if (static_cast<int>(desc.Width) != w || static_cast<int>(desc.Height) != h) {
            d2dContext->SetTarget(nullptr);
            d2dTargetBitmap.Reset();
            if (FAILED(swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) return;
            createTargetBitmap();
        }
    }

    // ---- the per-frame draw; keeps the <= 8ms/120Hz budget by pure fills ----
    void renderFrame() {
        if (!d2dContext || !d2dTargetBitmap) return;
        const PaintList pl = state.paintList();
        d2dContext->BeginDraw();
        d2dContext->Clear();  // fully transparent; DComp keeps desktop visible
        // PaintList coordinates are virtual-screen coordinates; the swap
        // chain starts at the virtual desktop's upper-left corner.
        d2dContext->SetTransform(D2D1::Matrix3x2F::Translation(
            -static_cast<float>(pl.desktop.x), -static_cast<float>(pl.desktop.y)));

        // 1) 40% dim over the whole desktop.
        ComPtr<ID2D1SolidColorBrush> dimBrush;
        d2dContext->CreateSolidColorBrush(
            D2D1::ColorF(0.f, 0.f, 0.f, state.visuals().dimAlpha), dimBrush.GetAddressOf());
        const auto dr = toD2D(pl.dimRegion);
        if (pl.punchThrough) {
            const auto raw = toD2D(*pl.punchThrough);
            const D2D1_RECT_F sel{std::clamp(raw.left, dr.left, dr.right),
                                  std::clamp(raw.top, dr.top, dr.bottom),
                                  std::clamp(raw.right, dr.left, dr.right),
                                  std::clamp(raw.bottom, dr.top, dr.bottom)};
            const D2D1_RECT_F areas[] = {
                {dr.left, dr.top, dr.right, sel.top},
                {dr.left, sel.bottom, dr.right, dr.bottom},
                {dr.left, sel.top, sel.left, sel.bottom},
                {sel.right, sel.top, dr.right, sel.bottom}
            };
            for (const auto& area : areas) {
                if (area.left < area.right && area.top < area.bottom)
                    d2dContext->FillRectangle(&area, dimBrush.Get());
            }
        } else {
            d2dContext->FillRectangle(&dr, dimBrush.Get());
        }

        const auto accent = D2D1::ColorF(state.visuals().accentR,
                                         state.visuals().accentG,
                                         state.visuals().accentB, 1.f);

        // 2) selection punch-through + 1px stroke + badge.
        if (pl.punchThrough) {
            const auto sel = toD2D(*pl.punchThrough);
            // 1px stroke.
            ComPtr<ID2D1SolidColorBrush> stroke;
            d2dContext->CreateSolidColorBrush(accent, stroke.GetAddressOf());
            D2D1_RECT_F s = sel;
            const float onePx = 1.0f;
            const D2D1_RECT_F outline{sel.left + onePx / 2, sel.top + onePx / 2,
                                      sel.right - onePx / 2, sel.bottom - onePx / 2};
            d2dContext->DrawRectangle(&outline, stroke.Get(), onePx);
        }

        // 3) hovered-window highlight: tint fill + 1px stroke (FR-02).
        if (pl.windowHighlight) {
            const auto wr = toD2D(*pl.windowHighlight);
            ComPtr<ID2D1SolidColorBrush> fill;
            d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(accent.r, accent.g, accent.b, state.visuals().windowFillAlpha),
                fill.GetAddressOf());
            d2dContext->FillRectangle(&wr, fill.Get());
            ComPtr<ID2D1SolidColorBrush> stroke;
            d2dContext->CreateSolidColorBrush(accent, stroke.GetAddressOf());
            d2dContext->DrawRectangle(&wr, stroke.Get(), 1.f);
        }

        // 4) crosshair (idle + dragging).
        if (pl.crosshair) {
            ComPtr<ID2D1SolidColorBrush> white;
            d2dContext->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White, 0.85f),
                                              white.GetAddressOf());
            const float cx = static_cast<float>(pl.crosshairAt.x);
            const float cy = static_cast<float>(pl.crosshairAt.y);
            D2D1_POINT_2F h0{static_cast<float>(pl.desktop.x), cy}, h1{static_cast<float>(pl.desktop.right()), cy};
            D2D1_POINT_2F v0{cx, static_cast<float>(pl.desktop.y)}, v1{cx, static_cast<float>(pl.desktop.bottom())};
            d2dContext->DrawLine(h0, h1, white.Get(), 1.f);
            d2dContext->DrawLine(v0, v1, white.Get(), 1.f);
        }

        // 5) size badge + toolbar skeleton: rect outlines here; text via
        //    DWrite lands with the FR-05 toolbar task (layout-only skeleton).
        if (pl.badge) {
            ComPtr<ID2D1SolidColorBrush> bg;
            d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(0.f, 0.f, 0.f, 0.75f), bg.GetAddressOf());
            const auto br = toD2D(pl.badge->anchor);
            d2dContext->FillRectangle(&br, bg.Get());
        }
        for (const auto& slot : pl.toolbarSlots) {
            const auto sr = toD2D(slot);
            ComPtr<ID2D1SolidColorBrush> slotBrush;
            d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(1.f, 1.f, 1.f, 0.08f), slotBrush.GetAddressOf());
            d2dContext->FillRectangle(&sr, slotBrush.Get());
        }

        if (FAILED(d2dContext->EndDraw())) return;
        DXGI_PRESENT_PARAMETERS pp{};
        swapChain->Present1(0, 0, &pp);
        dcompDevice->Commit();
    }

    static D2D1_RECT_F toD2D(const Rect& r) {
        return D2D1::RectF(static_cast<float>(r.x), static_cast<float>(r.y),
                           static_cast<float>(r.right()), static_cast<float>(r.bottom()));
    }
};

constexpr wchar_t SelectionOverlayController::Impl::kClassName[];

SelectionOverlayController::SelectionOverlayController() : impl_(std::make_unique<Impl>()) {}
SelectionOverlayController::~SelectionOverlayController() = default;

bool SelectionOverlayController::precreate() {
    auto& impl = *impl_;
    if (impl.hwnd) return true;

    const HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    if (!impl.registered) {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Impl::wndProc;
        wc.hInstance = hInst;
        wc.hCursor = ::LoadCursorW(nullptr, IDC_CROSS);
        wc.lpszClassName = Impl::kClassName;
        if (!::RegisterClassExW(&wc)) return false;
        impl.registered = true;
    }

    // Hidden at start: the hotkey path only calls ShowWindow (fast path).
    impl.hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        Impl::kClassName, L"TransFrame Selection Overlay",
        WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInst, &impl);
    if (!impl.hwnd) return false;

    if (!impl.createDeviceStack()) return false;
    impl.resizeToVirtualDesktop();
    return true;
}

bool SelectionOverlayController::beginSession(OverlayCompletion completion) {
    auto& impl = *impl_;
    if (!impl.hwnd && !precreate()) return false;
    if (impl.sessionActive) return false;

    impl.completion = std::move(completion);
    impl.sessionActive = true;
    impl.state.setMode(OverlayMode::Idle);

    impl.resizeToVirtualDesktop();  // picks up display changes since last session
    impl.renderFrame();
    ::ShowWindow(impl.hwnd, SW_SHOWNA);  // no-activate: don't steal focus
    // Overlay must receive keyboard for Esc: focus without activation is not
    // possible for a WS_EX_NOACTIVATE window; we grab it explicitly because
    // during selection the overlay owns all input (FR-01).
    ::SetFocus(impl.hwnd);
    ::SetForegroundWindow(impl.hwnd);
    return true;
}

void SelectionOverlayController::cancelSession() {
    auto& impl = *impl_;
    if (!impl.sessionActive) return;
    impl.state.cancel();
    if (impl.completion) impl.completion({false, {}, false, 0});
    impl.completion = nullptr;
    impl.sessionActive = false;
    ::ShowWindow(impl.hwnd, SW_HIDE);
}

bool SelectionOverlayController::isActive() const {
    return impl_->sessionActive;
}

void SelectionOverlayController::setWindowDetector(WindowDetectorService* detector) {
    impl_->windowDetector = detector;
    if (detector) detector->refreshSnapshot();
}

OverlayState& SelectionOverlayController::state() { return impl_->state; }

} // namespace tf
