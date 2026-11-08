// ColorPicker.cpp — FR-32 magnifier eyedropper window (see header).
//
// Device stack mirrors SelectionOverlay: D3D11 device -> D2D1 device context
// -> DComp visual bound to the layered window. beginSession snapshots the
// whole virtual desktop into a BGRA cache BEFORE the overlay shows (a
// DComp-presented layered window reads back black from the GDI screen DC on
// Win11 DWM, so live sampling would only ever see this window's own
// surface). Per mouse move: read the 25x25 lens region from that cache,
// center pixel = live color; render lens (8x nearest neighbor), 1px frame,
// full-screen hairline crosshair, HEX/RGB badge + swatch.
// Click = pick from the cache + complete; Enter picks at the cursor;
// Esc/right-click = cancel. The window hides itself before the completion
// fires so the app can copy/record with a clean screen.
#include "pch.h"

#include "tf/ColorPicker.hpp"

#include <d2d1_1.h>
#include <d2d1_1helper.h>
#include <dcomp.h>
#include <d3d11.h>
#include <dwrite.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <cstring>
#include <vector>

#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

namespace tf {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kPickerWndClass[] = L"TransFrameColorPickWnd";

constexpr float kBadgePadX = 10.0f;
constexpr float kBadgePadY = 7.0f;
constexpr float kBadgeFontPx = 15.0f;
constexpr float kHintFontPx = 13.0f;
constexpr int kLensDisplayPx = ColorPickerController::kLensSourcePx *
                               ColorPickerController::kLensZoom;  // 200 px

struct D2DStack {
    ComPtr<ID3D11Device> d3dDevice;
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<ID2D1Factory1> d2dFactory;
    ComPtr<ID2D1Device> d2dDevice;
    ComPtr<ID2D1DeviceContext> d2dContext;
    ComPtr<IDCompositionDevice> dcompDevice;
    ComPtr<IDCompositionTarget> dcompTarget;
    ComPtr<IDCompositionVisual> dcompVisual;
    ComPtr<IDXGISwapChain1> swapChain;
    ComPtr<ID2D1Bitmap1> d2dTargetBitmap;
    ComPtr<IDWriteFactory> dwriteFactory;
    ComPtr<IDWriteTextFormat> badgeFormat;  // Consolas hex; Segoe UI fallback
    ComPtr<IDWriteTextFormat> hintFormat;   // Segoe UI
};

struct WndState {
    D2DStack stack;
    long deskX = 0, deskY = 0, deskW = 0, deskH = 0;  // virtual desktop px
    long cursorX = 0, cursorY = 0;
    std::uint8_t curR = 0, curG = 0, curB = 0;
    int srcW = 0, srcH = 0;  // clamped lens source size (edge of desktop)
    // Desktop snapshot taken once per session BEFORE this overlay shows.
    // A DComp-presented layered window reads back BLACK from the GDI screen
    // DC (GetPixel and BitBlt alike on Win11 DWM), so live sampling during
    // the session would only ever see this window's own surface. Every
    // sample — lens render and click pick — therefore reads this cache.
    std::vector<std::uint8_t> desktop;  // top-down BGRA, deskW x deskH
    std::size_t desktopStride = 0;      // deskW * 4
    std::vector<std::uint8_t> lensPixels;  // alpha-forced BGRA rows for D2D
};

WndState* g_state = nullptr;

}  // namespace

// ---------------------------------------------------------------------------
// Sampling (physical px, virtual-desktop coordinates)
// ---------------------------------------------------------------------------
bool colorPickSamplePixel(long x, long y, std::uint8_t* r, std::uint8_t* g,
                          std::uint8_t* b) {
    if (!r || !g || !b) return false;
    const HDC screen = ::GetDC(nullptr);
    if (!screen) return false;
    const COLORREF px = ::GetPixel(screen, static_cast<int>(x), static_cast<int>(y));
    ::ReleaseDC(nullptr, screen);
    if (px == CLR_INVALID) return false;
    *r = static_cast<std::uint8_t>(px & 0xFF);
    *g = static_cast<std::uint8_t>((px >> 8) & 0xFF);
    *b = static_cast<std::uint8_t>((px >> 16) & 0xFF);
    return true;
}

namespace {

// ---------------------------------------------------------------------------
// Device stack (SelectionOverlay pattern, compacted)
// ---------------------------------------------------------------------------
bool ensureStack(HWND hwnd, WndState& s) {
    if (s.stack.d2dContext) return true;

    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
                                     flags, nullptr, 0, D3D11_SDK_VERSION,
                                     s.stack.d3dDevice.GetAddressOf(), nullptr, nullptr);
    if (FAILED(hr)) {
        hr = ::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
                                 nullptr, 0, D3D11_SDK_VERSION,
                                 s.stack.d3dDevice.GetAddressOf(), nullptr, nullptr);
    }
    if (FAILED(hr)) return false;
    s.stack.d3dDevice.As(&s.stack.dxgiDevice);

    D2D1_FACTORY_OPTIONS fo{};
    if (FAILED(::D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED,
                                   __uuidof(ID2D1Factory1), &fo,
                                   reinterpret_cast<void**>(s.stack.d2dFactory.GetAddressOf())))) {
        return false;
    }
    if (FAILED(s.stack.d2dFactory->CreateDevice(s.stack.dxgiDevice.Get(),
                                                s.stack.d2dDevice.GetAddressOf()))) {
        return false;
    }
    if (FAILED(s.stack.d2dDevice->CreateDeviceContext(
            D2D1_DEVICE_CONTEXT_OPTIONS_NONE, s.stack.d2dContext.GetAddressOf()))) {
        return false;
    }
    if (FAILED(::DCompositionCreateDevice(
            s.stack.dxgiDevice.Get(), __uuidof(IDCompositionDevice),
            reinterpret_cast<void**>(s.stack.dcompDevice.GetAddressOf())))) {
        return false;
    }
    if (FAILED(s.stack.dcompDevice->CreateTargetForHwnd(
            hwnd, /*topmost=*/TRUE, s.stack.dcompTarget.GetAddressOf()))) {
        return false;
    }
    if (FAILED(s.stack.dcompDevice->CreateVisual(s.stack.dcompVisual.GetAddressOf()))) {
        return false;
    }

    if (FAILED(::DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
                                     reinterpret_cast<IUnknown**>(
                                         s.stack.dwriteFactory.GetAddressOf())))) {
        return false;
    }
    s.stack.dwriteFactory->CreateTextFormat(
        L"Consolas", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, kBadgeFontPx, L"zh-CN",
        s.stack.badgeFormat.GetAddressOf());
    if (!s.stack.badgeFormat) {
        s.stack.dwriteFactory->CreateTextFormat(
            L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_SEMI_BOLD, DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL, kBadgeFontPx, L"zh-CN",
            s.stack.badgeFormat.GetAddressOf());
    }
    s.stack.dwriteFactory->CreateTextFormat(
        L"Segoe UI", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL,
        DWRITE_FONT_STRETCH_NORMAL, kHintFontPx, L"zh-CN",
        s.stack.hintFormat.GetAddressOf());
    return s.stack.badgeFormat && s.stack.hintFormat;
}

/// (Re)create / resize the composition swap chain and rebind the D2D target.
bool resizeTarget(WndState& s, UINT w, UINT h) {
    if (!s.stack.d2dContext || w == 0 || h == 0) return false;
    if (s.stack.d2dTargetBitmap) {
        s.stack.d2dContext->SetTarget(nullptr);
        s.stack.d2dTargetBitmap.Reset();
    }
    if (!s.stack.swapChain) {
        DXGI_SWAP_CHAIN_DESC1 scd{};
        scd.Width = w;
        scd.Height = h;
        scd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        scd.SampleDesc.Count = 1;
        scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scd.BufferCount = 2;
        scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        ComPtr<IDXGIAdapter> adapter;
        ComPtr<IDXGIFactory2> factory;
        if (FAILED(s.stack.dxgiDevice->GetAdapter(&adapter))) return false;
        if (FAILED(adapter->GetParent(__uuidof(IDXGIFactory2),
                                      reinterpret_cast<void**>(factory.GetAddressOf())))) {
            return false;
        }
        if (FAILED(factory->CreateSwapChainForComposition(
                s.stack.d3dDevice.Get(), &scd, nullptr, s.stack.swapChain.GetAddressOf()))) {
            return false;
        }
        s.stack.dcompVisual->SetContent(s.stack.swapChain.Get());
        if (FAILED(s.stack.dcompTarget->SetRoot(s.stack.dcompVisual.Get()))) return false;
        s.stack.dcompDevice->Commit();
    } else if (FAILED(s.stack.swapChain->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0))) {
        return false;
    }

    D2D1_BITMAP_PROPERTIES1 props{};
    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    ComPtr<IDXGISurface> surface;
    if (FAILED(s.stack.swapChain->GetBuffer(
            0, __uuidof(IDXGISurface), reinterpret_cast<void**>(surface.GetAddressOf())))) {
        return false;
    }
    if (FAILED(s.stack.d2dContext->CreateBitmapFromDxgiSurface(
            surface.Get(), props, s.stack.d2dTargetBitmap.GetAddressOf()))) {
        return false;
    }
    s.stack.d2dContext->SetTarget(s.stack.d2dTargetBitmap.Get());
    return true;
}

// ---------------------------------------------------------------------------
// Lens sampling + rendering
// ---------------------------------------------------------------------------

/// One-shot snapshot of the whole virtual desktop into WndState::desktop
/// (top-down BGRA). Called in beginSession BEFORE the overlay is shown, so
/// the capture can never contain this window itself.
bool captureDesktop(WndState& s) {
    if (s.deskW <= 0 || s.deskH <= 0) return false;
    const HDC screen = ::GetDC(nullptr);
    if (!screen) return false;

    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(s.deskW);
    bi.bmiHeader.biHeight = -static_cast<LONG>(s.deskH);  // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP dib = ::CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib || !bits) {
        if (dib) ::DeleteObject(dib);
        ::ReleaseDC(nullptr, screen);
        return false;
    }
    HDC mem = ::CreateCompatibleDC(screen);
    HGDIOBJ old = ::SelectObject(mem, dib);
    const BOOL blt =
        ::BitBlt(mem, 0, 0, static_cast<int>(s.deskW), static_cast<int>(s.deskH),
                 screen, static_cast<int>(s.deskX), static_cast<int>(s.deskY), SRCCOPY);
    ::SelectObject(mem, old);
    ::DeleteDC(mem);
    ::ReleaseDC(nullptr, screen);

    if (!blt) {
        ::DeleteObject(dib);
        return false;
    }
    s.desktopStride = static_cast<std::size_t>(s.deskW) * 4;
    const auto* first = static_cast<const std::uint8_t*>(bits);
    s.desktop.assign(first, first + s.desktopStride * static_cast<std::size_t>(s.deskH));
    ::DeleteObject(dib);
    return true;
}

/// Reads the lens source region and the cursor pixel from the session
/// desktop cache (never the live screen — see WndState::desktop).
void sampleLensRegion(WndState& s, long x, long y) {
    s.cursorX = x;
    s.cursorY = y;
    const long half = ColorPickerController::kLensSourcePx / 2;
    RECT src{x - half, y - half, x + half + 1, y + half + 1};
    const RECT desk{s.deskX, s.deskY, s.deskX + s.deskW, s.deskY + s.deskH};
    if (src.left < desk.left) src.left = desk.left;
    if (src.top < desk.top) src.top = desk.top;
    if (src.right > desk.right) src.right = desk.right;
    if (src.bottom > desk.bottom) src.bottom = desk.bottom;
    const int w = static_cast<int>(src.right - src.left);
    const int h = static_cast<int>(src.bottom - src.top);
    const int srcDx = static_cast<int>(x - src.left);  // cursor inside region
    const int srcDy = static_cast<int>(y - src.top);
    s.srcW = w;
    s.srcH = h;

    if (s.desktop.empty() || s.desktopStride == 0 || w <= 0 || h <= 0) return;
    // Rows padded to the lens pitch (kLensSourcePx wide) — the D2D bitmap
    // upload in renderFrame expects exactly that stride.
    const std::size_t pitch =
        static_cast<std::size_t>(ColorPickerController::kLensSourcePx) * 4;
    s.lensPixels.assign(pitch * static_cast<std::size_t>(h), 0xFF);
    for (int row = 0; row < h; ++row) {
        const std::size_t srcOff =
            static_cast<std::size_t>(src.top - s.deskY + row) * s.desktopStride +
            static_cast<std::size_t>(src.left - s.deskX) * 4;
        std::memcpy(s.lensPixels.data() + static_cast<std::size_t>(row) * pitch,
                    s.desktop.data() + srcOff, static_cast<std::size_t>(w) * 4);
        for (int col = 3; col < w * 4; col += 4) {
            s.lensPixels[static_cast<std::size_t>(row) * pitch +
                         static_cast<std::size_t>(col)] = 0xFF;  // opaque for D2D
        }
    }
    const std::size_t center =
        static_cast<std::size_t>(srcDy) * pitch + static_cast<std::size_t>(srcDx) * 4;
    s.curB = s.lensPixels[center];
    s.curG = s.lensPixels[center + 1];
    s.curR = s.lensPixels[center + 2];
}

void renderFrame(WndState& s) {
    ID2D1DeviceContext* dc = s.stack.d2dContext.Get();
    if (!dc || !s.stack.d2dTargetBitmap) return;
    dc->BeginDraw();
    dc->Clear(D2D1_COLOR_F{0, 0, 0, 0});  // transparent — desktop stays visible

    const D2D1_SIZE_U target = s.stack.d2dTargetBitmap->GetPixelSize();
    const FLOAT sw = static_cast<FLOAT>(target.width);
    const FLOAT sh = static_cast<FLOAT>(target.height);

    ComPtr<ID2D1SolidColorBrush> accent, badgeBg, badgeFg, cross, swatch;
    dc->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 0.55f, 0.35f, 1.0f}, &accent);
    dc->CreateSolidColorBrush(D2D1_COLOR_F{0.06f, 0.06f, 0.08f, 0.88f}, &badgeBg);
    dc->CreateSolidColorBrush(D2D1_COLOR_F{0.96f, 0.96f, 0.98f, 1.0f}, &badgeFg);
    dc->CreateSolidColorBrush(D2D1_COLOR_F{1.0f, 1.0f, 1.0f, 0.85f}, &cross);

    // Full-screen hairline crosshair through the cursor.
    const FLOAT cx = static_cast<FLOAT>(s.cursorX - s.deskX);
    const FLOAT cy = static_cast<FLOAT>(s.cursorY - s.deskY);
    if (cross) {
        dc->DrawLine(D2D1_POINT_2F{0.0f, cy}, D2D1_POINT_2F{sw, cy}, cross.Get(), 1.0f);
        dc->DrawLine(D2D1_POINT_2F{cx, 0.0f}, D2D1_POINT_2F{cx, sh}, cross.Get(), 1.0f);
    }

    // Lens square: offset down-right of the cursor, flipped near edges. The
    // 28px offset also guarantees the lens never covers the sampled pixel.
    const FLOAT lens = static_cast<FLOAT>(kLensDisplayPx);
    const FLOAT off = 28.0f;
    FLOAT lx = cx + off;
    FLOAT ly = cy + off;
    if (lx + lens > sw) lx = cx - lens - off;
    if (ly + lens + 92.0f > sh) ly = cy - lens - 100.0f;
    if (lx < 0.0f) lx = 0.0f;
    if (ly < 0.0f) ly = 0.0f;
    const D2D1_RECT_F lensRect{lx, ly, lx + lens, ly + lens};

    if (!s.lensPixels.empty()) {
        D2D1_BITMAP_PROPERTIES1 bmpProps{};
        bmpProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        bmpProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        ComPtr<ID2D1Bitmap1> srcBmp;
        if (SUCCEEDED(dc->CreateBitmap(
                D2D1_SIZE_U{static_cast<UINT>(s.srcW), static_cast<UINT>(s.srcH)},
                s.lensPixels.data(), ColorPickerController::kLensSourcePx * 4, bmpProps,
                srcBmp.GetAddressOf()))) {
            const D2D1_RECT_F srcRect{0.0f, 0.0f, static_cast<FLOAT>(s.srcW),
                                      static_cast<FLOAT>(s.srcH)};
            dc->DrawBitmap(srcBmp.Get(), lensRect, 1.0f,
                           D2D1_INTERPOLATION_MODE_NEAREST_NEIGHBOR, &srcRect, nullptr);
        }
    }
    if (accent) dc->DrawRectangle(lensRect, accent.Get(), 1.0f);

    // Badge: live swatch + HEX (mono) + RGB + hint line.
    const std::uint8_t r = s.curR, g = s.curG, b = s.curB;
    dc->CreateSolidColorBrush(
        D2D1_COLOR_F{r / 255.0f, g / 255.0f, b / 255.0f, 1.0f}, &swatch);
    const D2D1_RECT_F badgeRect{lx, ly + lens + 6.0f, lx + lens, ly + lens + 62.0f};
    if (badgeBg) dc->FillRectangle(badgeRect, badgeBg.Get());
    const D2D1_RECT_F swatchRect{badgeRect.left + kBadgePadX, badgeRect.top + kBadgePadY,
                                 badgeRect.left + kBadgePadX + 22.0f,
                                 badgeRect.top + kBadgePadY + 22.0f};
    if (swatch) {
        dc->FillRectangle(swatchRect, swatch.Get());
        if (cross) dc->DrawRectangle(swatchRect, cross.Get(), 1.0f);
    }
    if (badgeFg && s.stack.badgeFormat) {
        const std::string hex = colorPickHex(r, g, b);
        const std::wstring wHex(hex.begin(), hex.end());
        const D2D1_RECT_F hexRect{swatchRect.right + 8.0f, badgeRect.top + 3.0f,
                                  badgeRect.right - kBadgePadX, badgeRect.top + 27.0f};
        dc->DrawTextW(wHex.c_str(), static_cast<UINT32>(wHex.size()),
                      s.stack.badgeFormat.Get(), hexRect, badgeFg.Get());
    }
    if (badgeFg && s.stack.hintFormat) {
        const std::string rgb = colorPickRgbText(r, g, b);
        const std::wstring wRgb(rgb.begin(), rgb.end());
        const D2D1_RECT_F rgbRect{badgeRect.left + kBadgePadX, badgeRect.top + 28.0f,
                                  badgeRect.right - kBadgePadX, badgeRect.bottom};
        dc->DrawTextW(wRgb.c_str(), static_cast<UINT32>(wRgb.size()),
                      s.stack.hintFormat.Get(), rgbRect, badgeFg.Get());
    }
    if (badgeBg && badgeFg && s.stack.hintFormat) {
        const D2D1_RECT_F hintRect{lx, badgeRect.bottom + 4.0f, lx + lens,
                                   badgeRect.bottom + 26.0f};
        const wchar_t hint[] = L"单击取色 · Esc 取消";
        dc->FillRectangle(hintRect, badgeBg.Get());
        dc->DrawTextW(hint, ARRAYSIZE(hint), s.stack.hintFormat.Get(), hintRect,
                      badgeFg.Get());
    }

    dc->EndDraw();
    s.stack.swapChain->Present(1, 0);
    s.stack.dcompDevice->Commit();
}

// ---------------------------------------------------------------------------
// WndProc
// ---------------------------------------------------------------------------
ColorPickerController& pickerController() { return ColorPickerController::instance(); }

LRESULT CALLBACK pickerWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    WndState* s = g_state;
    switch (msg) {
        case WM_MOUSEMOVE: {
            if (!s) break;
            sampleLensRegion(*s, GET_X_LPARAM(lp) + s->deskX,
                             GET_Y_LPARAM(lp) + s->deskY);
            renderFrame(*s);
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!s) break;
            const long x = GET_X_LPARAM(lp) + s->deskX;
            const long y = GET_Y_LPARAM(lp) + s->deskY;
            // Sample through the BitBlt lens path, never GetPixel: on the
            // Win11 DWM desktop GetPixel reads this topmost window's own
            // redirection surface (black where the D2D clear is transparent),
            // while BitBlt SRCCOPY excludes layered windows — the same reason
            // sampleLensRegion never sees its own lens.
            sampleLensRegion(*s, x, y);
            ColorPickResult result;
            result.picked = true;
            result.hex = colorPickHex(s->curR, s->curG, s->curB);
            result.r = s->curR;
            result.g = s->curG;
            result.b = s->curB;
            result.screenX = x;
            result.screenY = y;
            pickerController().finishSession(result);
            return 0;
        }
        case WM_KEYDOWN: {
            if (wp == VK_ESCAPE) {
                pickerController().finishSession(ColorPickResult{});
                return 0;
            }
            if (wp == VK_RETURN && s) {  // keyboard pick at the cursor
                ColorPickResult result;
                result.picked = true;
                result.hex = colorPickHex(s->curR, s->curG, s->curB);
                result.r = s->curR;
                result.g = s->curG;
                result.b = s->curB;
                result.screenX = s->cursorX;
                result.screenY = s->cursorY;
                pickerController().finishSession(result);
                return 0;
            }
            break;
        }
        case WM_RBUTTONDOWN:
        case WM_MBUTTONDOWN:
            pickerController().finishSession(ColorPickResult{});
            return 0;
        case WM_SETCURSOR:
            ::SetCursor(::LoadCursor(nullptr, IDC_CROSS));
            return TRUE;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// Controller
// ---------------------------------------------------------------------------
bool ColorPickerController::registerClass(void* hinstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = pickerWndProc;
    wc.hInstance = static_cast<HINSTANCE>(hinstance);
    wc.hCursor = ::LoadCursor(nullptr, IDC_CROSS);
    wc.lpszClassName = kPickerWndClass;
    // Idempotent: a second registration (AppMain precreate + smoke driver)
    // is a success, not an error.
    const ATOM atom = ::RegisterClassExW(&wc);
    return atom != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

ColorPickerController& ColorPickerController::instance() {
    static ColorPickerController controller;
    return controller;
}

bool ColorPickerController::precreate() {
    if (hwnd_) return true;
    const HINSTANCE inst = static_cast<HINSTANCE>(::GetModuleHandleW(nullptr));
    if (!registerClass(inst)) return false;

    const long vx = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    const long vy = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    const long vw = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const long vh = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);

    hwnd_ = ::CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
                              kPickerWndClass, L"TransFrame Color Picker", WS_POPUP,
                              vx, vy, vw, vh, nullptr, nullptr, inst, nullptr);
    if (!hwnd_) return false;
    ::SetLayeredWindowAttributes(static_cast<HWND>(hwnd_), 0, 255, LWA_ALPHA);

    auto* s = new WndState();
    s->deskX = vx;
    s->deskY = vy;
    s->deskW = vw;
    s->deskH = vh;
    g_state = s;

    if (!ensureStack(static_cast<HWND>(hwnd_), *s) ||
        !resizeTarget(*s, static_cast<UINT>(vw), static_cast<UINT>(vh))) {
        return false;
    }
    return true;
}

bool ColorPickerController::beginSession(ColorPickCompletion onDone) {
    if (active_ || !hwnd_) return false;
    WndState* s = g_state;
    if (!s || !s->stack.d2dContext) return false;
    completion_ = std::move(onDone);
    active_ = true;
    auto* hwnd = static_cast<HWND>(hwnd_);

    // Recompute the virtual desktop (topology may have changed since start).
    s->deskX = ::GetSystemMetrics(SM_XVIRTUALSCREEN);
    s->deskY = ::GetSystemMetrics(SM_YVIRTUALSCREEN);
    s->deskW = ::GetSystemMetrics(SM_CXVIRTUALSCREEN);
    s->deskH = ::GetSystemMetrics(SM_CYVIRTUALSCREEN);
    // Snapshot the desktop BEFORE this overlay becomes visible — once the
    // layered DComp window is up, the GDI screen DC reads back its black
    // surface, so every later sample must come from the cache (the session
    // lasts seconds; macOS pickers snapshot the same way).
    if (!captureDesktop(*s)) {
        active_ = false;
        return false;
    }
    ::SetWindowPos(hwnd, HWND_TOPMOST, s->deskX, s->deskY, s->deskW, s->deskH,
                   SWP_SHOWWINDOW);
    resizeTarget(*s, static_cast<UINT>(s->deskW), static_cast<UINT>(s->deskH));

    POINT cursor{};
    ::GetCursorPos(&cursor);
    sampleLensRegion(*s, cursor.x, cursor.y);
    renderFrame(*s);
    ::SetForegroundWindow(hwnd);
    ::SetFocus(hwnd);
    return true;
}

void ColorPickerController::finishSession(const ColorPickResult& result) {
    if (!active_) return;
    active_ = false;
    ::ShowWindow(static_cast<HWND>(hwnd_), SW_HIDE);
    ColorPickCompletion done = std::move(completion_);
    completion_ = nullptr;
    if (done) done(result);
}

void ColorPickerController::cancelSession() {
    finishSession(ColorPickResult{});
}

}  // namespace tf
