// ResultWindow.cpp — borderless Win32 popup: OCR text + translation + copy.
//
// Layout (client px, DPI-unaware v1 — PMv2 markers land with the real UI):
//   [status line          ] [Copy] [Close]
//   [source text (gray)   ]
//   [translated text (big)]
//   Esc / kill-focus closes. WM_PAINT draws with GDI (no D2D needed here —
//   this is a small static popup, not a 120Hz surface).
#include "pch.h"

#include "tf/ResultWindow.hpp"

#include <windowsx.h>

namespace tf {

namespace {

constexpr wchar_t kResultWndClass[] = L"TransFrameResultWnd";
constexpr int kWindowWidth = 480;
constexpr int kWindowHeight = 320;

struct Layout {
    RECT status;
    RECT copyBtn;
    RECT closeBtn;
    RECT source;
    RECT translated;
};

Layout computeLayout(const RECT& client, int fontHeight) {
    const int pad = 10;
    const int btnH = fontHeight + 10;
    const int btnW = 78;
    Layout l{};
    l.status = {pad, pad, client.right - pad - 2 * (btnW + 8), pad + btnH};
    l.copyBtn = {client.right - pad - 2 * btnW - 8, pad,
                 client.right - pad - btnW - 8, pad + btnH};
    l.closeBtn = {client.right - pad - btnW, pad, client.right - pad, pad + btnH};
    const int textTop = pad + btnH + 8;
    const int half = (client.bottom - textTop - pad) / 2;
    l.source = {pad, textTop, client.right - pad, textTop + half};
    l.translated = {pad, textTop + half + 4, client.right - pad, client.bottom - pad};
    return l;
}

} // namespace

struct ResultWindowController::Impl {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    HFONT font = nullptr;
    HFONT fontBold = nullptr;
    ResultWindowContent content;
    Layout layout{};
    bool ownFont = false;

    static Impl* selfFromHwnd(HWND h) {
        return reinterpret_cast<Impl*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
    }

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        Impl* self = selfFromHwnd(h);
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
            ::SetWindowLongPtrW(h, GWLP_USERDATA,
                                reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            self = static_cast<Impl*>(cs->lpCreateParams);
        }
        if (self) return self->handle(h, msg, wp, lp);
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                // DPI-scaled fonts (system font family, UI scale).
                UINT dpi = ::GetDpiForWindow(h);
                const int base = ::GetSystemMetricsForDpi(SM_CYCAPTION, dpi);
                font = ::CreateFontW(-::MulDiv(15, dpi, 96), 0, 0, 0, FW_NORMAL,
                                     FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                     L"Segoe UI");
                fontBold = ::CreateFontW(-::MulDiv(16, dpi, 96), 0, 0, 0, FW_SEMIBOLD,
                                         FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                         CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                         L"Segoe UI");
                ownFont = true;
                (void)base;
                return 0;
            }
            case WM_DESTROY: {
                if (ownFont) {
                    if (font) ::DeleteObject(font);
                    if (fontBold) ::DeleteObject(fontBold);
                    font = fontBold = nullptr;
                    ownFont = false;
                }
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;  // paint fills everything
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = ::BeginPaint(h, &ps);
                paint(h, dc);
                ::EndPaint(h, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN:
            case WM_NCLBUTTONDOWN: {
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                // Buttons swallow the press (no drag from a button).
                if (::PtInRect(&layout.copyBtn, pt) || ::PtInRect(&layout.closeBtn, pt)) {
                    return 0;
                }
                // Drag anywhere else on the popup (no caption).
                ::ReleaseCapture();
                ::SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
                return 0;
            }
            case WM_LBUTTONUP: {
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (::PtInRect(&layout.copyBtn, pt)) {
                    copyToClipboard(h, content.translatedText);
                    flashStatus(h);
                    return 0;
                }
                if (::PtInRect(&layout.closeBtn, pt)) {
                    ::ShowWindow(h, SW_HIDE);
                    return 0;
                }
                return 0;
            }
            case WM_SETCURSOR: {
                const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                if (LOWORD(lp) == HTCLIENT &&
                    (::PtInRect(&layout.copyBtn, pt) || ::PtInRect(&layout.closeBtn, pt))) {
                    ::SetCursor(::LoadCursorW(nullptr, IDC_HAND));
                    return TRUE;
                }
                break;
            }
            case WM_KILLFOCUS:
                // Focus-loss auto-close keeps the desktop clean (v1 behavior;
                // pinning arrives with the task-D overlay manager).
                ::ShowWindow(h, SW_HIDE);
                return 0;
            case WM_KEYDOWN:
                if (wp == VK_ESCAPE) {
                    ::ShowWindow(h, SW_HIDE);
                    return 0;
                }
                break;
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    void paint(HWND h, HDC dc) {
        RECT client{};
        ::GetClientRect(h, &client);

        // Card background + border.
        HBRUSH bg = ::CreateSolidBrush(RGB(24, 26, 32));
        HPEN border = ::CreatePen(PS_SOLID, 1, RGB(70, 74, 86));
        ::FillRect(dc, &client, bg);
        HGDIOBJ oldPen = ::SelectObject(dc, border);
        ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(dc, client.left, client.top, client.right, client.bottom);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(border);
        ::DeleteObject(bg);

        int fontHeight = 15;
        if (font) {
            HGDIOBJ old = ::SelectObject(dc, font);
            TEXTMETRICW tm{};
            if (::GetTextMetricsW(dc, &tm)) fontHeight = tm.tmHeight;
            ::SelectObject(dc, old);
        }
        layout = computeLayout(client, fontHeight);

        // Status line.
        HGDIOBJ old = ::SelectObject(dc, font);
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, RGB(150, 155, 168));
        RECT st = layout.status;
        ::DrawTextW(dc, content.statusLine.c_str(), -1, &st,
                    DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS | DT_NOPREFIX);

        // Buttons: Copy / Close (owner draw: border + centered text).
        drawButton(dc, layout.copyBtn, L"复制译文", RGB(0, 120, 215));
        drawButton(dc, layout.closeBtn, L"关闭", RGB(60, 64, 74));

        // Source text (gray, small).
        ::SetTextColor(dc, RGB(130, 135, 148));
        RECT src = layout.source;
        ::DrawTextW(dc, content.sourceText.c_str(), -1, &src,
                    DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_END_ELLIPSIS);

        // Translated text (white, larger, semibold).
        ::SelectObject(dc, fontBold);
        ::SetTextColor(dc, RGB(240, 242, 248));
        RECT tr = layout.translated;
        ::DrawTextW(dc, content.translatedText.c_str(), -1, &tr,
                    DT_WORDBREAK | DT_EDITCONTROL | DT_NOPREFIX | DT_END_ELLIPSIS);
        ::SelectObject(dc, old);
    }

    void drawButton(HDC dc, const RECT& r, const wchar_t* text, COLORREF accent) {
        HBRUSH fill = ::CreateSolidBrush(
            RGB(GetRValue(accent) / 4, GetGValue(accent) / 4, GetBValue(accent) / 4));
        ::FillRect(dc, &r, fill);
        ::DeleteObject(fill);
        HPEN pen = ::CreatePen(PS_SOLID, 1, accent);
        HGDIOBJ oldPen = ::SelectObject(dc, pen);
        HGDIOBJ oldBrush = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(dc, r.left, r.top, r.right, r.bottom);
        ::SelectObject(dc, oldBrush);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(pen);
        ::SetTextColor(dc, RGB(235, 238, 245));
        ::DrawTextW(dc, text, -1, const_cast<RECT*>(&r),
                    DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }

    void flashStatus(HWND h) {
        // Brief "copied" feedback: repaint with an updated status line.
        content.statusLine += L"  · 已复制";
        ::InvalidateRect(h, nullptr, FALSE);
    }

    static void copyToClipboard(HWND h, const std::wstring& text) {
        if (!::OpenClipboard(h)) return;
        ::EmptyClipboard();
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HANDLE mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* p = ::GlobalLock(mem)) {
                std::memcpy(p, text.c_str(), bytes);
                ::GlobalUnlock(mem);
                ::SetClipboardData(CF_UNICODETEXT, mem);
            } else {
                ::GlobalFree(mem);
            }
        }
        ::CloseClipboard();
    }
};

bool ResultWindowController::registerClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Impl::wndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kResultWndClass;
    return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

ResultWindowController::ResultWindowController() : impl_(new Impl) {}

ResultWindowController::~ResultWindowController() {
    if (impl_->hwnd) ::DestroyWindow(impl_->hwnd);
    // impl_ (unique_ptr) releases itself.
}

bool ResultWindowController::show(const ResultWindowContent& content) {
    auto& impl = *impl_;

    if (!impl.hwnd) {
        impl.instance = ::GetModuleHandleW(nullptr);
        if (!registerClass(impl.instance)) return false;
        impl.hwnd = ::CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
            kResultWndClass, L"TransFrame Result",
            WS_POPUP | WS_BORDER,
            CW_USEDEFAULT, CW_USEDEFAULT, kWindowWidth, kWindowHeight,
            nullptr, nullptr, impl.instance, &impl);
        if (!impl.hwnd) return false;
    }

    impl.content = content;

    // Place below the captured region; clamp to the work area of the
    // monitor containing the region.
    const HMONITOR monitor = ::MonitorFromPoint(
        POINT{static_cast<LONG>(content.anchorPhysical.x),
              static_cast<LONG>(content.anchorPhysical.y)},
        MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (::GetMonitorInfoW(monitor, &mi)) {
        RECT wr{mi.rcWork};
        int x = static_cast<int>(content.anchorPhysical.x);
        int y = static_cast<int>(content.anchorPhysical.y + content.anchorPhysical.height + 8);
        if (y + kWindowHeight > wr.bottom) {
            y = static_cast<int>(content.anchorPhysical.y) - kWindowHeight - 8;
        }
        if (y < wr.top) y = wr.top + 8;
        if (x + kWindowWidth > wr.right) x = wr.right - kWindowWidth - 8;
        if (x < wr.left) x = wr.left + 8;
        ::SetWindowPos(impl.hwnd, HWND_TOPMOST, x, y, kWindowWidth, kWindowHeight,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    } else {
        ::SetWindowPos(impl.hwnd, HWND_TOPMOST, CW_USEDEFAULT, CW_USEDEFAULT,
                       kWindowWidth, kWindowHeight, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    ::ShowWindow(impl.hwnd, SW_SHOWNOACTIVATE);
    return true;
}

void ResultWindowController::close() {
    if (impl_->hwnd) ::ShowWindow(impl_->hwnd, SW_HIDE);
}

bool ResultWindowController::isVisible() const {
    return impl_->hwnd && ::IsWindowVisible(impl_->hwnd) != FALSE;
}

} // namespace tf
