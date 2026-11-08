// PinnedWindowManager.cpp — FR-14 pinned screenshot windows (Win32 fleet).
//
// One topmost, non-activating, tool-window HWND per pin (WS_POPUP; PMv2
// physical pixels), GDI-painted from a packed copy of the pinned pixels
// (StretchDIBits needs a tightly strided source — WGC buffers are
// 256-byte-row padded). Click-through rides on
// WS_EX_LAYERED|WS_EX_TRANSPARENT (alpha 255 = fully opaque); the
// interactive mode adds a 1px frame + a ✕ close box and enables drag-move;
// right-click flips the window back to click-through. The pure fleet
// semantics live in PinnedWindowState (header, Catch2-covered); this TU only
// syncs HWNDs to it. Main thread only.
#include "pch.h"

#include "tf/PinnedWindowManager.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace tf {

namespace {

constexpr wchar_t kPinnedWndClass[] = L"TransFramePinnedWnd";
constexpr int kCloseBoxPx = 28;  // physical; interactive-mode ✕ hit area

/// Per-window state (owned by the manager; raw pointer in GWLP_USERDATA).
struct PinWindow {
    std::uint64_t id = 0;
    HWND hwnd = nullptr;
    std::shared_ptr<PixelBuffer> pixels;
    std::vector<std::uint8_t> packed;  // tightly strided BGRA copy for paint
    bool clickThrough = true;
    bool closing = false;  // guards re-entry while the manager destroys us
    bool dragging = false;
    POINT dragStart{};
    POINT dragWindowOrigin{};
};

RECT closeBoxRect(const RECT& client) {
    return RECT{client.right - kCloseBoxPx, 0, client.right, kCloseBoxPx};
}

PinWindow* pinFromHwnd(HWND h) {
    return reinterpret_cast<PinWindow*>(::GetWindowLongPtrW(h, GWLP_USERDATA));
}

}  // namespace

struct PinnedWindowManager::Impl {
    PinnedWindowState state;
    std::vector<std::unique_ptr<PinWindow>> windows;  // parallel to state order
    std::function<void()> onChanged;
    bool classRegistered = false;
    bool inTeardown = false;

    Impl() = default;
    ~Impl() { destroyAll(); }

    void notifyChanged() {
        if (onChanged && !inTeardown) onChanged();
    }

    PinWindow* findById(std::uint64_t id) const {
        for (const auto& w : windows) {
            if (w->id == id) return w.get();
        }
        return nullptr;
    }

    void destroyAll() {
        inTeardown = true;
        for (auto& w : windows) {
            if (w->hwnd && ::IsWindow(w->hwnd)) {
                w->closing = true;
                ::DestroyWindow(w->hwnd);
            }
        }
        windows.clear();
        state.unpinAll();
        inTeardown = false;
    }

    void applyClickThroughStyle(PinWindow& w) {
        const LONG_PTR ex = ::GetWindowLongPtrW(w.hwnd, GWL_EXSTYLE);
        const LONG_PTR wanted =
            w.clickThrough ? (ex | WS_EX_TRANSPARENT) : (ex & ~static_cast<LONG_PTR>(WS_EX_TRANSPARENT));
        if (wanted != ex) ::SetWindowLongPtrW(w.hwnd, GWL_EXSTYLE, wanted);
        ::InvalidateRect(w.hwnd, nullptr, FALSE);
    }

    void closePin(std::uint64_t id) {
        for (auto it = windows.begin(); it != windows.end(); ++it) {
            if ((*it)->id == id) {
                if ((*it)->hwnd && ::IsWindow((*it)->hwnd)) {
                    (*it)->closing = true;
                    ::DestroyWindow((*it)->hwnd);
                }
                windows.erase(it);
                state.unpin(id);
                notifyChanged();
                return;
            }
        }
    }

    // ---- window proc ------------------------------------------------------
    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);

    LRESULT handle(HWND h, PinWindow& w, UINT msg, WPARAM wp, LPARAM lp) {
        (void)wp;  // no wParam consumers today
        switch (msg) {
            case WM_ERASEBKGND:
                return 1;  // WM_PAINT fills everything
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = ::BeginPaint(h, &ps);
                if (dc) paint(w, dc);
                ::EndPaint(h, &ps);
                return 0;
            }
            case WM_LBUTTONDOWN: {
                if (w.clickThrough) break;  // transparent windows never see this
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                RECT client{};
                ::GetClientRect(h, &client);
                const RECT closeBox = closeBoxRect(client);
                if (::PtInRect(&closeBox, pt)) {
                    // ✕: close this pin (manager owns the teardown).
                    ::PostMessageW(h, WM_CLOSE, 0, 0);
                    return 0;
                }
                w.dragging = true;
                w.dragStart = pt;
                RECT wr{};
                ::GetWindowRect(h, &wr);
                w.dragWindowOrigin = POINT{wr.left, wr.top};
                ::SetCapture(h);
                return 0;
            }
            case WM_MOUSEMOVE: {
                if (!w.dragging) break;
                POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
                ::SetWindowPos(h, nullptr,
                               w.dragWindowOrigin.x + pt.x - w.dragStart.x,
                               w.dragWindowOrigin.y + pt.y - w.dragStart.y,
                               0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
                return 0;
            }
            case WM_LBUTTONUP:
                if (w.dragging) {
                    w.dragging = false;
                    ::ReleaseCapture();
                }
                return 0;
            case WM_RBUTTONUP:
                // 右键切回点击穿透 (tray 切换穿透 covers the fleet-wide case).
                if (!w.clickThrough) {
                    w.clickThrough = true;
                    state.setClickThrough(w.id, true);
                    applyClickThroughStyle(w);
                }
                return 0;
            case WM_CLOSE:
                if (!w.closing) {
                    w.closing = true;
                    closePin(w.id);
                }
                return 0;
            case WM_DESTROY:
                return 0;
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    void paint(PinWindow& w, HDC dc) {
        RECT client{};
        ::GetClientRect(w.hwnd, &client);
        const int cw = client.right - client.left;
        const int ch = client.bottom - client.top;
        if (cw <= 0 || ch <= 0 || w.packed.empty()) return;

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w.pixels->width;
        bmi.bmiHeader.biHeight = -w.pixels->height;  // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        ::SetStretchBltMode(dc, HALFTONE);
        ::StretchDIBits(dc, 0, 0, cw, ch, 0, 0, w.pixels->width, w.pixels->height,
                        w.packed.data(), &bmi, DIB_RGB_COLORS, SRCCOPY);

        if (w.clickThrough) return;  // pure image when transparent
        // Interactive affordances: 1px frame + ✕ close box.
        ::FrameRect(dc, &client, static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH)));
        const RECT box = closeBoxRect(client);
        HBRUSH back = ::CreateSolidBrush(RGB(40, 40, 40));
        ::FillRect(dc, &box, back);
        ::DeleteObject(back);
        HPEN pen = ::CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
        const HGDIOBJ oldPen = ::SelectObject(dc, pen);
        const int inset = 8;
        ::MoveToEx(dc, box.left + inset, box.top + inset, nullptr);
        ::LineTo(dc, box.right - inset, box.bottom - inset);
        ::MoveToEx(dc, box.right - inset, box.top + inset, nullptr);
        ::LineTo(dc, box.left + inset, box.bottom - inset);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(pen);
    }

    void registerClass() {
        if (classRegistered) return;
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_HREDRAW | CS_VREDRAW;
        wc.lpfnWndProc = &Impl::wndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = nullptr;
        wc.lpszClassName = kPinnedWndClass;
        ::RegisterClassExW(&wc);
        classRegistered = true;
    }
};

LRESULT CALLBACK PinnedWindowManager::Impl::wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(lp);
        ::SetWindowLongPtrW(h, GWLP_USERDATA,
                            reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        return ::DefWindowProcW(h, msg, wp, lp);
    }
    PinWindow* w = pinFromHwnd(h);
    if (w && w->hwnd == h) {
        if (auto* mgr = PinnedWindowManager::instanceIfExists()) {
            return mgr->impl_->handle(h, *w, msg, wp, lp);
        }
    }
    return ::DefWindowProcW(h, msg, wp, lp);
}

// ---------------------------------------------------------------------------
// Singleton
// ---------------------------------------------------------------------------
namespace {
PinnedWindowManager* g_pinnedManager = nullptr;
}

PinnedWindowManager& PinnedWindowManager::instance() {
    if (!g_pinnedManager) g_pinnedManager = new PinnedWindowManager();
    return *g_pinnedManager;
}

PinnedWindowManager* PinnedWindowManager::instanceIfExists() {
    return g_pinnedManager;
}

void PinnedWindowManager::destroyInstance() {
    delete g_pinnedManager;
    g_pinnedManager = nullptr;
}

PinnedWindowManager::PinnedWindowManager() : impl_(std::make_unique<Impl>()) {}
PinnedWindowManager::~PinnedWindowManager() = default;

// ---------------------------------------------------------------------------
// Fleet API
// ---------------------------------------------------------------------------
bool PinnedWindowManager::pin(std::uint64_t idOrZero,
                              std::shared_ptr<PixelBuffer> pixels,
                              const std::optional<std::pair<int, int>>& originPhysical) {
    if (!pixels || !pixels->valid()) return false;

    // Size: 1:1 pixels, clamped to the primary work area (aspect kept).
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int maxW = std::max(64, static_cast<int>(work.right - work.left));
    const int maxH = std::max(64, static_cast<int>(work.bottom - work.top));
    const double fit = std::min({1.0, static_cast<double>(maxW) / pixels->width,
                                 static_cast<double>(maxH) / pixels->height});
    const int w = std::max(1, static_cast<int>(pixels->width * fit));
    const int h = std::max(1, static_cast<int>(pixels->height * fit));

    const auto outcome = impl_->state.pin(idOrZero, w, h);
    if (!outcome.ok) return false;
    if (!outcome.createdNew) {  // re-pin raises the existing window
        bringToFront(outcome.id);
        return true;
    }

    impl_->registerClass();
    auto win = std::make_unique<PinWindow>();
    win->id = outcome.id;
    win->pixels = std::move(pixels);
    win->clickThrough = true;
    // Tight-stride copy for StretchDIBits (source stride may be padded).
    const std::size_t rowBytes = static_cast<std::size_t>(win->pixels->width) * 4;
    win->packed.resize(rowBytes * static_cast<std::size_t>(win->pixels->height));
    for (std::int32_t row = 0; row < win->pixels->height; ++row) {
        std::memcpy(win->packed.data() + row * rowBytes,
                    win->pixels->data.get() +
                        static_cast<std::size_t>(row) * static_cast<std::size_t>(win->pixels->stride),
                    rowBytes);
    }

    POINT pos{};
    if (originPhysical) {
        pos.x = originPhysical->first;
        pos.y = originPhysical->second;
        // Keep the (possibly clamped) pin inside the work area.
        pos.x = std::clamp(pos.x, static_cast<LONG>(work.left),
                           static_cast<LONG>(work.right - w));
        pos.y = std::clamp(pos.y, static_cast<LONG>(work.top),
                           static_cast<LONG>(work.bottom - h));
    } else {
        pos.x = work.left + (static_cast<int>(work.right - work.left) - w) / 2;
        pos.y = work.top + (static_cast<int>(work.bottom - work.top) - h) / 2;
    }

    PinWindow* raw = win.get();
    raw->hwnd = ::CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_LAYERED |
            WS_EX_TRANSPARENT,
        kPinnedWndClass, L"TransFrame 贴图", WS_POPUP, pos.x, pos.y, w, h,
        nullptr, nullptr, ::GetModuleHandleW(nullptr), raw);
    if (!raw->hwnd) {
        impl_->state.unpin(outcome.id);
        return false;
    }
    ::SetLayeredWindowAttributes(raw->hwnd, 0, 255, LWA_ALPHA);
    ::ShowWindow(raw->hwnd, SW_SHOWNOACTIVATE);
    impl_->windows.insert(impl_->windows.begin(), std::move(win));
    impl_->notifyChanged();
    return true;
}

void PinnedWindowManager::unpin(std::uint64_t id) { impl_->closePin(id); }

void PinnedWindowManager::unpinAll() {
    impl_->destroyAll();
    impl_->notifyChanged();
}

void PinnedWindowManager::bringToFront(std::uint64_t id) {
    if (impl_->state.isPinned(id)) impl_->state.bringToFront(id);
    if (PinWindow* w = impl_->findById(id)) {
        // Re-asserting TOPMOST (not just HWND_TOP) keeps pins above whatever
        // took the topmost band meanwhile.
        ::SetWindowPos(w->hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        impl_->notifyChanged();
    }
}

bool PinnedWindowManager::setClickThrough(std::uint64_t id, bool through) {
    PinWindow* w = impl_->findById(id);
    if (!w || !impl_->state.setClickThrough(id, through)) return false;
    w->clickThrough = through;
    impl_->applyClickThroughStyle(*w);
    impl_->notifyChanged();
    return true;
}

void PinnedWindowManager::toggleClickThroughFleet() {
    const auto snap = impl_->state.snapshot();
    bool anyThrough = false;
    for (const auto& p : snap) anyThrough = anyThrough || p.clickThrough;
    const bool target = !anyThrough;  // mixed/through fleet -> interactive
    impl_->state.setAllClickThrough(target);
    for (auto& w : impl_->windows) {
        w->clickThrough = target;
        impl_->applyClickThroughStyle(*w);
    }
    impl_->notifyChanged();
}

void PinnedWindowManager::gatherAll() {
    impl_->state.gatherAll();
    const auto snap = impl_->state.snapshot();
    if (snap.empty()) {
        impl_->notifyChanged();
        return;
    }
    // Grid-tile over the primary work area (聚拢到前台): near-square grid,
    // each pin scaled to fit its cell (aspect kept), centered in the cell.
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int areaW = std::max(1, static_cast<int>(work.right - work.left));
    const int areaH = std::max(1, static_cast<int>(work.bottom - work.top));
    const int n = static_cast<int>(snap.size());
    const int cols = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<double>(n)))));
    const int rows = (n + cols - 1) / cols;
    const double cellW = static_cast<double>(areaW) / cols;
    const double cellH = static_cast<double>(areaH) / rows;
    for (int i = 0; i < n; ++i) {
        PinWindow* w = impl_->findById(snap[static_cast<std::size_t>(i)].id);
        if (!w) continue;
        double scale = std::min({1.0, cellW * 0.92 / std::max(1, w->pixels->width),
                                 cellH * 0.92 / std::max(1, w->pixels->height)});
        const int ww = std::max(1, static_cast<int>(w->pixels->width * scale));
        const int wh = std::max(1, static_cast<int>(w->pixels->height * scale));
        const int col = i % cols;
        const int row = i / cols;
        const int x = static_cast<int>(work.left + col * cellW + (cellW - ww) / 2);
        const int y = static_cast<int>(work.top + row * cellH + (cellH - wh) / 2);
        ::SetWindowPos(w->hwnd, HWND_TOPMOST, x, y, ww, wh,
                       SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
    impl_->notifyChanged();
}

std::size_t PinnedWindowManager::count() const { return impl_->state.count(); }

std::vector<PinnedWindowInfo> PinnedWindowManager::snapshot() const {
    return impl_->state.snapshot();
}

void* PinnedWindowManager::hwndForId(std::uint64_t id) const {
    PinWindow* w = impl_->findById(id);
    return w ? static_cast<void*>(w->hwnd) : nullptr;
}

void PinnedWindowManager::setOnChanged(std::function<void()> callback) {
    impl_->onChanged = std::move(callback);
}

}  // namespace tf
