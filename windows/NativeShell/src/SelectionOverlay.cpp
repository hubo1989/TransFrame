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
//   4. size badge, crosshair
//   5. FR-05 in-place toolbar (DWrite labels, hover + hit testing) and
//      FR-06 results panel (source/translation rows, adaptive docking)
// The D2D1Device is shared with the capture engine's D3D11 device.
//
// FR-02 (window click): a WindowDetectorService is attached by the App;
// mouse-move hover hit-tests against its snapshot (refreshed at mouse-down).
//
// FR-05 session model: drag-release parks in Confirmed (toolbar visible);
// toolbar clicks hide the window first (clean desktop for the App's
// capture) and hand the action to the App. presentResults()/presentNotice()
// bring the overlay back with the results panel; endSession() finishes it.
#include "pch.h"

#include "tf/SelectionOverlay.hpp"
#include "tf/OverlayRenderer.hpp"
#include "tf/WindowDetector.hpp"

#include <algorithm>
#include <d2d1_1.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dwrite.h>
#include <wrl/client.h>

#pragma comment(lib, "dwrite.lib")

namespace tf {

using Microsoft::WRL::ComPtr;

// Diagnostic: precreate failures used to vanish silently inside the MSIX
// container while the same Win32 calls succeed in a plain process. Append
// each step's outcome to %TEMP%\TransFrameOverlayInit.log.
namespace {
void initLog(const char* step, long hr) {
    char path[MAX_PATH];
    const DWORD n = ::GetTempPathA(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH) return;
    lstrcatA(path, "TransFrameOverlayInit.log");
    HANDLE f = ::CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[128];
    const int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s hr=0x%08lX\n",
                                step, (unsigned long)hr);
    if (len > 0) {
        DWORD written = 0;
        ::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
    }
    ::CloseHandle(f);
}
}  // namespace

namespace {

/// FR-05/06 cross-thread requests (worker -> main thread hop).
constexpr UINT kPresentResultsMsg = WM_APP + 0x210;
constexpr UINT kPresentNoticeMsg = WM_APP + 0x211;
constexpr UINT kEndSessionMsg = WM_APP + 0x212;

struct ResultsBox {
    std::vector<OverlayTextRow> rows;
    std::string status;
};

struct NoticeBox {
    std::string notice;
};

/// Toolbar labels for the six FR-05 actions (slots 6..7 reserved, undrawn).
const wchar_t* toolbarActionLabel(OverlayToolbarAction action) {
    switch (action) {
        case OverlayToolbarAction::Copy: return L"复制";
        case OverlayToolbarAction::Save: return L"保存";
        case OverlayToolbarAction::Translate: return L"翻译";
        case OverlayToolbarAction::Pin: return L"置顶";
        case OverlayToolbarAction::ColorPick: return L"取色";
        case OverlayToolbarAction::Cancel: return L"取消";
    }
    return L"?";
}

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (len <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), len);
    wide.resize(static_cast<std::size_t>(len) - 1);
    return wide;
}

}  // namespace

struct SelectionOverlayController::Impl {
    HWND hwnd = nullptr;
    static constexpr wchar_t kClassName[] = L"TransFrameOverlayWnd";
    bool registered = false;
    OverlayState state;
    OverlayCompletion completion;
    bool sessionActive = false;
    WindowDetectorService* windowDetector = nullptr;  // FR-02 hover (App-owned)

    // FR-05: toolbar wiring + interaction state (main thread only).
    OverlayActionHandler actionHandler;
    std::optional<OverlayToolbarAction> autoAction;  // FR-04 auto path
    bool actionInFlight = false;  // window hidden for an action; ignore KILLFOCUS
    int hoverSlot = -1;
    bool hoverClose = false;

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

    // FR-05/06 text resources (created lazily on the render thread).
    ComPtr<IDWriteFactory> dwrite;
    ComPtr<IDWriteTextFormat> buttonFormat;
    ComPtr<IDWriteTextFormat> panelStatusFormat;
    ComPtr<IDWriteTextFormat> panelSourceFormat;
    ComPtr<IDWriteTextFormat> panelTranslatedFormat;
    ComPtr<IDWriteInlineObject> trimmingSign;

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
            case kPresentResultsMsg: {
                // Worker thread finished the pipeline: panel up, window back.
                auto* box = reinterpret_cast<ResultsBox*>(lp);
                std::unique_ptr<ResultsBox> owned(box);
                if (!sessionActive) break;  // session ended meanwhile: drop
                state.showResults(std::move(box->rows), std::move(box->status));
                reshowAfterAction();
                return 0;
            }
            case kPresentNoticeMsg: {
                auto* box = reinterpret_cast<NoticeBox*>(lp);
                std::unique_ptr<NoticeBox> owned(box);
                if (!sessionActive) break;
                state.showNotice(std::move(box->notice));
                reshowAfterAction();
                return 0;
            }
            case kEndSessionMsg: {
                if (sessionActive) endSessionInternal();
                return 0;
            }
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
                // FR-05: toolbar/close hover feedback while Confirmed.
                if (state.mode() == OverlayMode::Confirmed) {
                    updateHover(pt);
                }
                renderFrame();
                return 0;
            }
            case WM_LBUTTONDOWN: {
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ::ClientToScreen(h, &pt);  // tracking + detector use screen coords
                if (state.mode() == OverlayMode::Confirmed) {
                    handleConfirmedClick(pt);
                    return 0;
                }
                if (windowDetector) windowDetector->refreshSnapshot();  // cache at mouse-down
                state.beginTracking({pt.x, pt.y});
                renderFrame();
                ::SetCapture(h);
                return 0;
            }
            case WM_LBUTTONUP: {
                if (::GetCapture() == h) ::ReleaseCapture();
                // Confirmed mouse-ups belong to toolbar clicks already handled
                // in WM_LBUTTONDOWN; a fresh drag ends below this guard.
                if (state.mode() == OverlayMode::Confirmed) return 0;
                state.endTracking();
                if (state.mode() == OverlayMode::Confirmed) {
                    // FR-05: park on the in-place toolbar (window stays up so
                    // the user keeps the region context).
                    hoverSlot = -1;
                    if (autoAction) {
                        performAction(*autoAction);  // FR-04: no extra click
                    } else {
                        renderFrame();
                    }
                    return 0;
                }
                finishSession(h);
                return 0;
            }
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE) {
                    if (sessionActive) endSessionInternal();
                    return 0;
                }
                break;
            case WM_KILLFOCUS:
                // The window hides itself while an action captures the
                // desktop — that focus loss must not end the session.
                if (actionInFlight) return 0;
                if (sessionActive) endSessionInternal();
                return 0;
            case WM_SETCURSOR: {
                // Confirmed UI gets an arrow/hand; selection modes keep cross.
                if (LOWORD(lp) == HTCLIENT &&
                    (state.mode() == OverlayMode::Confirmed)) {
                    ::SetCursor(::LoadCursorW(nullptr, (hoverSlot >= 0 || hoverClose)
                                                        ? IDC_HAND : IDC_ARROW));
                    return TRUE;
                }
                break;
            }
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    /// FR-05/06 click routing while Confirmed: panel first (it can overlap
    /// the toolbar), then toolbar buttons, then dismiss.
    void handleConfirmedClick(POINT pt) {
        const PaintList pl = state.paintList();
        const Point p{pt.x, pt.y};
        if (pl.resultsPanel) {
            const ResultsPanelLayout& panel = *pl.resultsPanel;
            if (panel.closeButton.contains(p)) {
                state.dismissResults();
                renderFrame();
                return;
            }
            if (panel.panel.contains(p)) return;  // inert panel surface
        }
        if (const auto action = hitTestToolbar(pl, p)) {
            performAction(*action);
            return;
        }
        endSessionInternal();  // click-away dismiss (macOS parity)
    }

    void updateHover(POINT pt) {
        const PaintList pl = state.paintList();
        const Point p{pt.x, pt.y};
        hoverSlot = -1;
        hoverClose = false;
        if (pl.resultsPanel && pl.resultsPanel->closeButton.contains(p)) {
            hoverClose = true;
        }
        const std::size_t n = std::min(pl.toolbarSlots.size(), pl.toolbarActions.size());
        for (std::size_t i = 0; i < n; ++i) {
            if (pl.toolbarActions[i] && pl.toolbarSlots[i].contains(p)) {
                hoverSlot = static_cast<int>(i);
                break;
            }
        }
    }

    /// Hide for a clean desktop capture, then hand the action to the App.
    void performAction(OverlayToolbarAction action) {
        if (action == OverlayToolbarAction::Cancel) {
            endSessionInternal();
            return;
        }
        const auto selection = state.selection();
        actionInFlight = true;
        hoverSlot = -1;
        ::ShowWindow(hwnd, SW_HIDE);
        if (actionHandler && selection) {
            actionHandler(action, *selection);
            // Translate/Pin/ColorPick: presentResults/presentNotice re-shows.
            // Copy/Save: the App calls endSession() when done.
        } else {
            // No handler (or no selection — should not happen): fall through
            // to a plain cancel so the overlay never wedges.
            actionInFlight = false;
            endSessionInternal();
        }
    }

    /// Bring the window back after an action hid it (main thread).
    void reshowAfterAction() {
        actionInFlight = false;
        hoverSlot = -1;
        hoverClose = false;
        ::ShowWindow(hwnd, SW_SHOWNA);
        ::SetFocus(hwnd);  // Esc must reach us while the panel is up
        renderFrame();
    }

    /// Unified dismissal: drop panel + selection, hide, fire completion once.
    /// State is cleared BEFORE ShowWindow: hiding a focused window delivers
    /// WM_KILLFOCUS synchronously, and its handler re-enters here — the
    /// early flags make that second visit a no-op instead of a double
    /// completion.
    void endSessionInternal() {
        state.dismissResults();
        state.cancel();
        actionInFlight = false;
        hoverSlot = -1;
        hoverClose = false;
        sessionActive = false;
        const OverlayCompletion callback = std::move(completion);
        completion = nullptr;
        ::ShowWindow(hwnd, SW_HIDE);
        if (callback) callback({false, {}, false, 0});
    }

    void finishSession(HWND h) {
        // Only reached for click (no-drag) mouse-ups: drag releases return
        // early from WM_LBUTTONUP in Confirmed mode. Note endTracking() has
        // already reset dragEngaged_, so the mode check — not isDragEngaged —
        // is the drag discriminator.
        const auto hover = state.hoverWindow();
        sessionActive = false;
        const OverlayCompletion callback = std::move(completion);
        completion = nullptr;
        ::ShowWindow(h, SW_HIDE);
        if (!callback) return;

        if (!hover) {
            callback({false, {}, false, 0});
        } else {
            // FR-02: click (no drag) -> whole window under cursor.
            SelectionOverlayResult r;
            r.confirmed = true;
            r.regionPhysical = *hover;
            r.wholeWindow = true;
            callback(std::move(r));
        }
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
        initLog("D3D11-HARDWARE", static_cast<long>(hr));
        if (FAILED(hr)) {
            hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                     nullptr, 0, D3D11_SDK_VERSION,
                                     d3dDevice.GetAddressOf(), &fl, nullptr);
            initLog("D3D11-WARP", static_cast<long>(hr));
        }
        if (FAILED(hr)) return false;
        d3dDevice.As(&dxgiDevice);

        // D2D 1.1 factory + device.
        D2D1_FACTORY_OPTIONS fo{};
        hr = ::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                 __uuidof(ID2D1Factory1),
                                 &fo, reinterpret_cast<void**>(d2dFactory.GetAddressOf()));
        initLog("D2D1CreateFactory", static_cast<long>(hr));
        if (FAILED(hr)) return false;
        hr = d2dFactory->CreateDevice(dxgiDevice.Get(), d2dDevice.GetAddressOf());
        initLog("D2D-CreateDevice", static_cast<long>(hr));
        if (FAILED(hr)) return false;
        hr = d2dDevice->CreateDeviceContext(
                D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContext.GetAddressOf());
        initLog("D2D-CreateDeviceContext", static_cast<long>(hr));
        if (FAILED(hr)) return false;

        // DirectComposition device + target bound to our HWND.
        hr = ::DCompositionCreateDevice(
                dxgiDevice.Get(), __uuidof(IDCompositionDevice),
                reinterpret_cast<void**>(dcompDevice.GetAddressOf()));
        initLog("DCompositionCreateDevice", static_cast<long>(hr));
        if (FAILED(hr)) return false;
        hr = dcompDevice->CreateTargetForHwnd(hwnd, TRUE, dcompTarget.GetAddressOf());
        initLog("CreateTargetForHwnd", static_cast<long>(hr));
        if (FAILED(hr)) return false;
        hr = dcompDevice->CreateVisual(dcompVisual.GetAddressOf());
        initLog("CreateVisual", static_cast<long>(hr));
        if (FAILED(hr)) return false;
        hr = dcompTarget->SetRoot(dcompVisual.Get());
        initLog("SetRoot", static_cast<long>(hr));
        if (FAILED(hr)) return false;

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
            initLog("CreateSwapChainForComposition", -1);
            return false;
        }
        hr = dcompVisual->SetContent(swapChain.Get());
        initLog("SetContent", static_cast<long>(hr));
        if (FAILED(hr)) return false;
        const bool ok = createTargetBitmap();
        initLog("createTargetBitmap", ok ? 0 : -1);
        return ok;
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

        // 5) size badge: rect fill here; text via DWrite below.
        if (pl.badge) {
            ComPtr<ID2D1SolidColorBrush> bg;
            d2dContext->CreateSolidColorBrush(
                D2D1::ColorF(0.f, 0.f, 0.f, 0.75f), bg.GetAddressOf());
            const auto br = toD2D(pl.badge->anchor);
            d2dContext->FillRectangle(&br, bg.Get());
        }

        // 6) FR-05 toolbar buttons + FR-06 results panel (DWrite text).
        if (!pl.toolbarSlots.empty()) {
            drawToolbar(pl);
        }
        if (pl.resultsPanel) {
            drawResultsPanel(pl);
        }

        if (FAILED(d2dContext->EndDraw())) return;
        DXGI_PRESENT_PARAMETERS pp{};
        swapChain->Present1(0, 0, &pp);
        dcompDevice->Commit();
    }

    // ---- FR-05/06 text resources (lazy, cached) --------------------------
    bool ensureTextResources() {
        if (buttonFormat && panelStatusFormat && panelSourceFormat &&
            panelTranslatedFormat && trimmingSign) {
            return true;
        }
        if (!dwrite) {
            if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED,
                                             __uuidof(IDWriteFactory),
                                             reinterpret_cast<IUnknown**>(
                                                 dwrite.GetAddressOf())))) {
                return false;
            }
        }
        auto makeFormat = [&](FLOAT size, DWRITE_FONT_WEIGHT weight,
                              IDWriteTextFormat** out) {
            return dwrite->CreateTextFormat(
                L"Segoe UI", nullptr, weight, DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL, size, L"", out);
        };
        if (!buttonFormat &&
            FAILED(makeFormat(12.f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              buttonFormat.GetAddressOf()))) {
            return false;
        }
        if (buttonFormat) {
            buttonFormat->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
            buttonFormat->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        }
        if (!panelStatusFormat &&
            FAILED(makeFormat(12.f, DWRITE_FONT_WEIGHT_NORMAL,
                              panelStatusFormat.GetAddressOf()))) {
            return false;
        }
        if (!panelSourceFormat &&
            FAILED(makeFormat(12.5f, DWRITE_FONT_WEIGHT_NORMAL,
                              panelSourceFormat.GetAddressOf()))) {
            return false;
        }
        if (!panelTranslatedFormat &&
            FAILED(makeFormat(15.f, DWRITE_FONT_WEIGHT_SEMI_BOLD,
                              panelTranslatedFormat.GetAddressOf()))) {
            return false;
        }
        if (!trimmingSign &&
            FAILED(dwrite->CreateEllipsisTrimmingSign(panelSourceFormat.Get(),
                                                      trimmingSign.GetAddressOf()))) {
            return false;
        }
        return true;
    }

    /// One panel text line: layout with width-limited trimming, drawn at
    /// (x, y), occupying at most maxWidth x lineHeight. Returns the consumed
    /// height (lineHeight on success, 0 when text resources are unusable).
    FLOAT drawPanelLine(const std::string& utf8, IDWriteTextFormat* format,
                        FLOAT x, FLOAT y, FLOAT maxWidth, FLOAT lineHeight,
                        const D2D1_COLOR_F& color) {
        if (utf8.empty() || !format || !d2dContext) return lineHeight;
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(d2dContext->CreateSolidColorBrush(color, brush.GetAddressOf()))) {
            return 0.f;
        }
        ComPtr<IDWriteTextLayout> layout;
        const std::wstring text = utf8ToWide(utf8);
        if (FAILED(dwrite->CreateTextLayout(text.c_str(),
                                            static_cast<UINT32>(text.size()), format,
                                            maxWidth, lineHeight,
                                            layout.GetAddressOf()))) {
            return 0.f;
        }
        const DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 1};
        layout->SetTrimming(&trimming, trimmingSign.Get());
        // CLIP: a long row must never paint over the row below it.
        d2dContext->DrawTextLayout(D2D1_POINT_2F{x, y}, layout.Get(), brush.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);
        return lineHeight;
    }

    void drawToolbar(const PaintList& pl) {
        if (!ensureTextResources() || !d2dContext) return;
        const std::size_t n = std::min(pl.toolbarSlots.size(), pl.toolbarActions.size());
        for (std::size_t i = 0; i < n; ++i) {
            const Rect& slot = pl.toolbarSlots[i];
            const auto action = pl.toolbarActions[i];
            const auto sr = toD2D(slot);
            if (!action) {
                // Reserved slot: keep the faint skeleton fill (FR-05 layout
                // skeleton callers relied on; future OCR/upload buttons).
                ComPtr<ID2D1SolidColorBrush> slotBrush;
                d2dContext->CreateSolidColorBrush(
                    D2D1::ColorF(1.f, 1.f, 1.f, 0.08f), slotBrush.GetAddressOf());
                d2dContext->FillRectangle(&sr, slotBrush.Get());
                continue;
            }
            const bool hovered = hoverSlot == static_cast<int>(i);
            const bool isCancel = *action == OverlayToolbarAction::Cancel;
            const wchar_t* label = toolbarActionLabel(*action);
            ComPtr<ID2D1SolidColorBrush> fill;
            d2dContext->CreateSolidColorBrush(
                hovered ? D2D1::ColorF(0.16f, 0.27f, 0.45f, 0.96f)
                        : D2D1::ColorF(0.11f, 0.12f, 0.14f, 0.92f),
                fill.GetAddressOf());
            ComPtr<ID2D1SolidColorBrush> stroke;
            d2dContext->CreateSolidColorBrush(
                hovered ? D2D1::ColorF(state.visuals().accentR, state.visuals().accentG,
                                       state.visuals().accentB, 1.f)
                        : D2D1::ColorF(1.f, 1.f, 1.f, 0.22f),
                stroke.GetAddressOf());
            const D2D1_ROUNDED_RECT card{sr, 6.f, 6.f};
            d2dContext->FillRoundedRectangle(&card, fill.Get());
            d2dContext->DrawRoundedRectangle(&card, stroke.Get(), 1.f);
            ComPtr<ID2D1SolidColorBrush> text;
            d2dContext->CreateSolidColorBrush(
                isCancel ? D2D1::ColorF(0.75f, 0.78f, 0.84f, 1.f)
                         : D2D1::ColorF(0.96f, 0.97f, 0.99f, 1.f),
                text.GetAddressOf());
            d2dContext->DrawTextW(label, static_cast<UINT32>(wcslen(label)),
                                  buttonFormat.Get(), sr, text.Get());
        }
    }

    void drawResultsPanel(const PaintList& pl) {
        if (!ensureTextResources() || !d2dContext || !pl.resultsPanel) return;
        const ResultsPanelLayout& layout = *pl.resultsPanel;
        const auto pr = toD2D(layout.panel);

        // Card.
        ComPtr<ID2D1SolidColorBrush> card;
        d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.075f, 0.082f, 0.10f, 0.95f),
                                          card.GetAddressOf());
        ComPtr<ID2D1SolidColorBrush> border;
        d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.f, 1.f, 1.f, 0.16f),
                                          border.GetAddressOf());
        const D2D1_ROUNDED_RECT cardRect{pr, 8.f, 8.f};
        d2dContext->FillRoundedRectangle(&cardRect, card.Get());
        d2dContext->DrawRoundedRectangle(&cardRect, border.Get(), 1.f);

        // Close button (x) with hover feedback.
        {
            const auto cr = toD2D(layout.closeButton);
            ComPtr<ID2D1SolidColorBrush> closeFill;
            d2dContext->CreateSolidColorBrush(
                hoverClose ? D2D1::ColorF(1.f, 1.f, 1.f, 0.18f)
                           : D2D1::ColorF(1.f, 1.f, 1.f, 0.04f),
                closeFill.GetAddressOf());
            const D2D1_ROUNDED_RECT closeCard{cr, 5.f, 5.f};
            d2dContext->FillRoundedRectangle(&closeCard, closeFill.Get());
            ComPtr<ID2D1SolidColorBrush> closeText;
            d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.85f, 0.87f, 0.92f, 1.f),
                                              closeText.GetAddressOf());
            d2dContext->DrawTextW(L"×", 1, buttonFormat.Get(), cr, closeText.Get());
        }

        // Header/status line.
        if (!pl.panelStatus.empty()) {
            drawPanelLine(pl.panelStatus, panelStatusFormat.Get(),
                          static_cast<FLOAT>(layout.panel.x + 12),
                          static_cast<FLOAT>(layout.panel.y + 8),
                          static_cast<FLOAT>(layout.panel.width - 12 - 34), 20.f,
                          D2D1::ColorF(0.59f, 0.61f, 0.66f, 1.f));
        }

        // Rows: source (gray) + translation (white); first rows that fit.
        const FLOAT sourceLine = 18.f;
        const FLOAT translatedLine = 22.f;
        const FLOAT rowGap = 7.f;
        const FLOAT rowHeight = sourceLine + translatedLine + rowGap;
        FLOAT y = static_cast<FLOAT>(layout.contentRect.y);
        const FLOAT maxY = static_cast<FLOAT>(layout.contentRect.bottom());
        const FLOAT width = static_cast<FLOAT>(layout.contentRect.width);
        for (const OverlayTextRow& row : pl.resultRows) {
            if (y + rowHeight > maxY) break;
            FLOAT used = drawPanelLine(row.source, panelSourceFormat.Get(),
                                       static_cast<FLOAT>(layout.contentRect.x), y, width,
                                       sourceLine,
                                       D2D1::ColorF(0.62f, 0.64f, 0.70f, 1.f));
            if (used <= 0.f) return;
            y += used;
            used = drawPanelLine(row.translated, panelTranslatedFormat.Get(),
                                 static_cast<FLOAT>(layout.contentRect.x), y, width,
                                 translatedLine, D2D1::ColorF(0.95f, 0.96f, 0.98f, 1.f));
            if (used <= 0.f) return;
            y += used + rowGap;
        }
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
        if (!::RegisterClassExW(&wc)) {
            initLog("RegisterClassExW", static_cast<long>(::GetLastError()));
            return false;
        }
        impl.registered = true;
    }

    // Hidden at start: the hotkey path only calls ShowWindow (fast path).
    impl.hwnd = ::CreateWindowExW(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
        Impl::kClassName, L"TransFrame Selection Overlay",
        WS_POPUP,
        0, 0, 1, 1, nullptr, nullptr, hInst, &impl);
    if (!impl.hwnd) {
        initLog("CreateWindowExW", static_cast<long>(::GetLastError()));
        return false;
    }
    initLog("CreateWindowExW-ok", 0);

    if (!impl.createDeviceStack()) {
        initLog("createDeviceStack", 0);
        return false;
    }
    impl.resizeToVirtualDesktop();
    initLog("precreate-ok", 0);
    return true;
}

bool SelectionOverlayController::beginSession(OverlayCompletion completion) {
    auto& impl = *impl_;
    if (!impl.hwnd && !precreate()) return false;
    if (impl.sessionActive) return false;

    impl.completion = std::move(completion);
    impl.sessionActive = true;
    impl.actionInFlight = false;
    impl.hoverSlot = -1;
    impl.hoverClose = false;
    impl.state.setMode(OverlayMode::Idle);

    impl.resizeToVirtualDesktop();  // picks up display changes since last session
    impl.renderFrame();
    ::ShowWindow(impl.hwnd, SW_SHOWNA);  // no-activate: don't steal focus
    initLog("beginSession-shown", ::IsWindowVisible(impl.hwnd) ? 1 : 0);
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
    impl.endSessionInternal();
}

bool SelectionOverlayController::isActive() const {
    return impl_->sessionActive;
}

bool SelectionOverlayController::awaitingAction() const {
    return impl_->sessionActive && impl_->state.mode() == OverlayMode::Confirmed;
}

void SelectionOverlayController::setWindowDetector(WindowDetectorService* detector) {
    impl_->windowDetector = detector;
    if (detector) detector->refreshSnapshot();
}

void SelectionOverlayController::setToolbarActionHandler(OverlayActionHandler handler) {
    impl_->actionHandler = std::move(handler);
}

void SelectionOverlayController::setAutoConfirmAction(
    std::optional<OverlayToolbarAction> action) {
    impl_->autoAction = action;
}

void SelectionOverlayController::presentResults(std::vector<OverlayTextRow> rows,
                                                std::string statusUtf8) {
    auto& impl = *impl_;
    if (!impl.hwnd || !impl.sessionActive) return;
    auto* box = new ResultsBox{std::move(rows), std::move(statusUtf8)};
    if (!::PostMessageW(impl.hwnd, kPresentResultsMsg, 0,
                        reinterpret_cast<LPARAM>(box))) {
        delete box;
    }
}

void SelectionOverlayController::presentNotice(std::string noticeUtf8) {
    auto& impl = *impl_;
    if (!impl.hwnd || !impl.sessionActive) return;
    auto* box = new NoticeBox{std::move(noticeUtf8)};
    if (!::PostMessageW(impl.hwnd, kPresentNoticeMsg, 0,
                        reinterpret_cast<LPARAM>(box))) {
        delete box;
    }
}

void SelectionOverlayController::endSession() {
    auto& impl = *impl_;
    if (!impl.hwnd || !impl.sessionActive) return;
    // May be called from any thread (App workers); hop to the window thread.
    ::PostMessageW(impl.hwnd, kEndSessionMsg, 0, 0);
}

OverlayState& SelectionOverlayController::state() { return impl_->state; }

} // namespace tf
