// ColorHistoryWindow.cpp — FR-41 color history grid (see header).
//
// One resizable top-level window; owner-drawn swatch grid under a toolbar
// (search / 仅收藏 / 复制RGB / 清空). Data = HistoryStore color entries;
// single click copies (HEX or RGB by the persisted toggle) with a 900ms
// "已复制" flash on the cell; right-click menu = copy HEX/RGB + favorite
// toggle + delete; 清空 asks first and removes only the color category.
#include "pch.h"

#include "tf/ColorHistoryWindow.hpp"
#include "tf/ColorPicker.hpp"

#include <windowsx.h>

#include "tf/Json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "comctl32.lib")

namespace tf {

namespace {

constexpr wchar_t kWndClass[] = L"TransFrameColorHistoryWnd";
constexpr int kDefaultW = 560, kDefaultH = 420;  // spec §4.1
constexpr int kMinW = 420, kMinH = 320;
constexpr UINT kSearchDebounceTimer = 11;
constexpr UINT kCopyFlashTimer = 12;

enum ControlIds {
    kSearchEdit = 300,
    kFavoritesCheck = 301,
    kRgbCheck = 302,
    kClearButton = 303,
    kCountLabel = 304,
};

// Remembered window placement (process lifetime; spec: remember position).
RECT g_lastPlacement{0, 0, 0, 0};
bool g_hasPlacement = false;

std::string utf8Of(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                          nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}
std::wstring wideOf(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    out.resize(out.size() - 1);
    return out;
}

std::wstring formatTime(const core::TimePoint& t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm local{};
    if (::localtime_s(&local, &tt) != 0) return L"";
    wchar_t buf[40];
    ::swprintf_s(buf, L"%02d-%02d %02d:%02d", local.tm_mon + 1, local.tm_mday,
                 local.tm_hour, local.tm_min);
    return buf;
}

bool copyToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !::OpenClipboard(owner)) return false;
    bool ok = false;
    if (::EmptyClipboard()) {
        const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
        if (HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
            if (void* p = ::GlobalLock(mem)) {
                std::memcpy(p, text.c_str(), bytes);
                ::GlobalUnlock(mem);
                if (::SetClipboardData(CF_UNICODETEXT, mem)) ok = true;
                else ::GlobalFree(mem);
            } else {
                ::GlobalFree(mem);
            }
        }
    }
    ::CloseClipboard();
    return ok;
}

std::filesystem::path settingsJsonPath() {
    wchar_t wappdata[MAX_PATH]{};
    const DWORD n = ::GetEnvironmentVariableW(L"APPDATA", wappdata, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return {};
    return std::filesystem::path(wappdata) / L"TransFrame" / L"settings.json";
}

bool readCopyRgbSetting() {
    std::error_code ec;
    const auto path = settingsJsonPath();
    if (path.empty() || !std::filesystem::exists(path, ec)) return false;
    std::ifstream in(path, std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    const Json root = Json::parse(ss.str(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) return false;
    return root.contains("TransFrame.colorHistoryCopyRgb") &&
           root.at("TransFrame.colorHistoryCopyRgb").is_boolean() &&
           root.at("TransFrame.colorHistoryCopyRgb").get<bool>();
}

void writeCopyRgbSetting(bool rgb) {
    const auto path = settingsJsonPath();
    if (path.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::string existing;
    if (std::filesystem::exists(path, ec)) {
        std::ifstream in(path, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        existing = ss.str();
    }
    Json root = Json::parse(existing, nullptr, false);
    if (root.is_discarded() || !root.is_object()) root = Json::object();
    root["TransFrame.colorHistoryCopyRgb"] = rgb;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << root.dump(2) << "\n";
}

}  // namespace

// ---------------------------------------------------------------------------
// Pure layout (unit-tested)
// ---------------------------------------------------------------------------
ColorHistoryLayout colorHistoryGridLayout(int clientW, int clientH, int dpi96) {
    ColorHistoryLayout out;
    const double scale = dpi96 > 0 ? static_cast<double>(dpi96) / 96.0 : 1.0;
    const auto px = [&](int logical) { return static_cast<int>(std::lround(logical * scale)); };
    out.swatch = std::max(48, px(48));            // spec: >=48 logical px
    out.cellW = out.swatch + px(18);
    out.cellH = out.swatch + px(32);              // + HEX line + time line
    out.gridX = px(10);
    out.gridY = px(10);
    out.columns = std::max(1, (clientW - 2 * out.gridX) / out.cellW);
    const int gridH = std::max(0, clientH - out.gridY);
    out.rows = std::max(0, gridH / out.cellH);
    return out;
}

// ---------------------------------------------------------------------------
// Controller pimpl
// ---------------------------------------------------------------------------
struct ColorHistoryWindowController::Impl {
    HWND hwnd = nullptr;
    HWND searchEdit = nullptr;
    HWND favoritesCheck = nullptr;
    HWND rgbCheck = nullptr;
    HWND clearButton = nullptr;
    HWND countLabel = nullptr;
    HFONT hexFont = nullptr;
    HFONT uiFont = nullptr;
    ColorHistoryLayout layout;
    int scrollRow = 0;        // first visible row (wheel scroll)
    int focusIndex = -1;      // keyboard focus cell
    int flashIndex = -1;      // "已复制" flash cell
    UINT64 flashStart = 0;    // when the flash began (unused beyond bookkeeping)

    int scale(int logical) const {
        const UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : 96;
        return static_cast<int>(std::lround(logical * dpi / 96.0));
    }
    int toolbarH() const { return scale(38); }

    int cellAt(POINT pt) const {
        const int gx = pt.x - layout.gridX;
        const int gy = pt.y - toolbarH() - layout.gridY + scrollRow * layout.cellH;
        if (gx < 0 || gy < 0) return -1;
        const int col = gx / layout.cellW;
        const int row = gy / layout.cellH;
        if (col >= layout.columns) return -1;
        const int index = row * layout.columns + col;
        return index;
    }
    RECT cellRect(int index) const {
        RECT rc{0, 0, 0, 0};
        const int row = index / layout.columns;
        const int col = index % layout.columns;
        rc.left = layout.gridX + col * layout.cellW;
        rc.top = toolbarH() + layout.gridY + (row - scrollRow) * layout.cellH;
        rc.right = rc.left + layout.cellW - scale(8);
        rc.bottom = rc.top + layout.cellH - scale(6);
        return rc;
    }
};

namespace {

ColorHistoryWindowController::Impl* g_impl = nullptr;
ColorHistoryWindowController* g_controller = nullptr;

/// Re-run the query with whatever the toolbar currently says (search text +
/// favorites checkbox) — used after favorite toggles / deletes / clears so
/// the view refreshes without losing the active filter.
void refreshFromToolbar() {
    if (!g_impl || !g_controller || !g_impl->searchEdit) return;
    wchar_t text[128];
    ::GetWindowTextW(g_impl->searchEdit, text, 128);
    g_controller->applySearchForTest(text);
}

void paintGrid(ColorHistoryWindowController::Impl* s,
               const std::vector<core::TranslationHistory>& rows) {
    PAINTSTRUCT ps;
    HDC dc = ::BeginPaint(s->hwnd, &ps);
    RECT client{};
    ::GetClientRect(s->hwnd, &client);

    // Double buffer.
    HDC mem = ::CreateCompatibleDC(dc);
    HBITMAP buf = ::CreateCompatibleBitmap(dc, client.right, client.bottom);
    HGDIOBJ oldBmp = ::SelectObject(mem, buf);

    ::FillRect(mem, &client, reinterpret_cast<HBRUSH>(::GetStockObject(WHITE_BRUSH)));

    const int total = static_cast<int>(rows.size());
    if (total == 0) {
        const wchar_t empty[] = L"还没有取色记录 · 从托盘菜单打开取色器开始";
        ::SetBkMode(mem, TRANSPARENT);
        ::SetTextColor(mem, RGB(110, 110, 120));
        RECT emptyRc = client;
        emptyRc.top += s->toolbarH();
        ::DrawTextW(mem, empty, -1, &emptyRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    } else {
        HGDIOBJ oldFont = ::SelectObject(mem, s->uiFont);
        for (int i = s->scrollRow * s->layout.columns;
             i < total && (i / s->layout.columns) - s->scrollRow < s->layout.rows; ++i) {
            const core::TranslationHistory& row = rows[static_cast<std::size_t>(i)];
            const RECT cell = s->cellRect(i);
            RECT swatch{cell.left, cell.top, cell.left + s->layout.swatch,
                        cell.top + s->layout.swatch};

            // Focus / picked-highlight ring.
            if (i == s->focusIndex || (s->flashIndex == i)) {
                RECT ring = cell;
                ::InflateRect(&ring, s->scale(2), s->scale(2));
                HBRUSH ringBrush =
                    ::CreateSolidBrush(i == s->focusIndex ? RGB(255, 149, 84) : RGB(76, 175, 80));
                ::FrameRect(mem, &ring, ringBrush);
                ::DeleteObject(ringBrush);
            }

            const auto rgb = colorPickParseHex(row.translatedText);
            const COLORREF color = rgb.has_value()
                                       ? RGB((*rgb)[0], (*rgb)[1], (*rgb)[2])
                                       : RGB(200, 200, 200);
            HBRUSH swatchBrush = ::CreateSolidBrush(color);
            ::FillRect(mem, &swatch, swatchBrush);
            ::DeleteObject(swatchBrush);
            ::DrawEdge(mem, &swatch, BDR_SUNKENOUTER, BF_RECT);

            // HEX under the swatch (mono), time under that.
            ::SetBkMode(mem, TRANSPARENT);
            ::SetTextColor(mem, RGB(40, 40, 46));
            RECT textRc{cell.left, swatch.bottom + s->scale(3), cell.right,
                        swatch.bottom + s->scale(3) + s->scale(16)};
            const std::wstring hex = wideOf(row.translatedText);
            ::SelectObject(mem, s->hexFont);
            ::DrawTextW(mem, hex.c_str(), -1, &textRc,
                        DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            ::SelectObject(mem, s->uiFont);
            ::SetTextColor(mem, RGB(128, 128, 134));
            RECT timeRc{textRc.left, textRc.bottom, textRc.right,
                        textRc.bottom + s->scale(14)};
            const std::wstring when = formatTime(row.timestamp);
            ::DrawTextW(mem, when.c_str(), -1, &timeRc,
                        DT_CENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

            // Favorite star top-right of the swatch.
            if (row.isFavorite) {
                ::SetTextColor(mem, RGB(255, 170, 0));
                RECT star{swatch.right - s->scale(16), swatch.top,
                          swatch.right + s->scale(2), swatch.top + s->scale(18)};
                ::DrawTextW(mem, L"★", -1, &star, DT_CENTER | DT_SINGLELINE);
            }

            // Copy flash overlay ("已复制").
            if (s->flashIndex == i) {
                HBRUSH flash = ::CreateSolidBrush(RGB(30, 30, 34));
                RECT badge = swatch;
                ::OffsetRect(&badge, 0, (s->layout.swatch - s->scale(22)) / 2);
                badge.bottom = badge.top + s->scale(22);
                ::FillRect(mem, &badge, flash);
                ::DeleteObject(flash);
                ::SetTextColor(mem, RGB(255, 255, 255));
                const wchar_t copied[] = L"已复制";
                ::DrawTextW(mem, copied, -1, &badge, DT_CENTER | DT_SINGLELINE);
            }
        }
        ::SelectObject(mem, oldFont);
    }

    ::BitBlt(dc, 0, 0, client.right, client.bottom, mem, 0, 0, SRCCOPY);
    ::SelectObject(mem, oldBmp);
    ::DeleteObject(buf);
    ::DeleteDC(mem);
    ::EndPaint(s->hwnd, &ps);
}

void doCopy(int index, bool rgb) {
    if (!g_impl || !g_controller || index < 0 || index >= g_controller->rowCount()) {
        return;
    }
    const std::string hex = g_controller->hexAt(index);
    const auto parsed = colorPickParseHex(hex);
    std::wstring text = wideOf(hex);
    if (rgb && parsed.has_value()) {
        text = wideOf(colorPickRgbText((*parsed)[0], (*parsed)[1], (*parsed)[2]));
    }
    copyToClipboard(g_impl->hwnd, text);
    g_impl->flashIndex = index;
    g_impl->flashStart = ::GetTickCount64();
    ::SetTimer(g_impl->hwnd, kCopyFlashTimer, 900, nullptr);
    ::InvalidateRect(g_impl->hwnd, nullptr, FALSE);
}

void confirmClearAll(HWND hwnd, ColorHistoryWindowController* self) {
    const wchar_t msg[] = L"清空全部取色记录？仅影响取色历史，翻译与截图记录保留。";
    if (::MessageBoxW(hwnd, msg, L"取色历史", MB_YESNO | MB_ICONQUESTION) != IDYES) {
        return;
    }
    self->clearAllForTest();
}

LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    ColorHistoryWindowController::Impl* s = g_impl;
    switch (msg) {
        case WM_COMMAND: {
            const int id = LOWORD(wp);
            const int code = HIWORD(wp);
            if (id == kSearchEdit && code == EN_CHANGE) {
                ::SetTimer(hwnd, kSearchDebounceTimer, 300, nullptr);
            } else if (id == kFavoritesCheck && code == BN_CLICKED) {
                refreshFromToolbar();
            } else if (id == kRgbCheck && code == BN_CLICKED) {
                if (s) {
                    g_controller->setCopyFormatRgbForTest(
                        ::SendMessageW(s->rgbCheck, BM_GETCHECK, 0, 0) == BST_CHECKED);
                }
            } else if (id == kClearButton && code == BN_CLICKED) {
                confirmClearAll(hwnd, g_controller);
            }
            return 0;
        }
        case WM_TIMER: {
            if (wp == kSearchDebounceTimer) {
                ::KillTimer(hwnd, kSearchDebounceTimer);
                refreshFromToolbar();
            } else if (wp == kCopyFlashTimer) {
                ::KillTimer(hwnd, kCopyFlashTimer);
                if (s && s->flashIndex >= 0) {
                    s->flashIndex = -1;
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            return 0;
        }
        case WM_LBUTTONDOWN: {
            if (!s) break;
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const int index = s->cellAt(pt);
            if (index >= 0 && index < g_controller->rowCount()) {
                s->focusIndex = index;
                doCopy(index, g_controller->copyFormatRgb());
            }
            return 0;
        }
        case WM_RBUTTONDOWN: {
            if (!s) break;
            const POINT pt{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            const int index = s->cellAt(pt);
            if (index < 0 || index >= g_controller->rowCount()) break;
            s->focusIndex = index;
            const HMENU menu = ::CreatePopupMenu();
            ::AppendMenuW(menu, MF_STRING, 1, L"复制 HEX");
            ::AppendMenuW(menu, MF_STRING, 2, L"复制 RGB");
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(menu, MF_STRING, 3, L"收藏 / 取消收藏");
            ::AppendMenuW(menu, MF_STRING, 4, L"删除");
            POINT screen = pt;
            ::ClientToScreen(hwnd, &screen);
            const int cmd = ::TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                             screen.x, screen.y, 0, hwnd, nullptr);
            ::DestroyMenu(menu);
            if (cmd == 1) doCopy(index, false);
            else if (cmd == 2) doCopy(index, true);
            else if (cmd == 3) g_controller->toggleFavoriteAtForTest(index);
            else if (cmd == 4) g_controller->deleteAtForTest(index);
            return 0;
        }
        case WM_KEYDOWN: {
            if (!s) break;
            const int total = g_controller->rowCount();
            if (wp == VK_ESCAPE) {
                if (s->searchEdit) ::SetWindowTextW(s->searchEdit, L"");
                return 0;
            }
            if (total == 0) break;
            const int cols = std::max(1, s->layout.columns);
            int next = s->focusIndex;
            switch (wp) {
                case VK_LEFT: next = s->focusIndex - 1; break;
                case VK_RIGHT: next = s->focusIndex + 1; break;
                case VK_UP: next = s->focusIndex - cols; break;
                case VK_DOWN: next = s->focusIndex + cols; break;
                case VK_HOME: next = 0; break;
                case VK_END: next = total - 1; break;
                case VK_RETURN:
                    if (s->focusIndex >= 0) {
                        doCopy(s->focusIndex, g_controller->copyFormatRgb());
                    }
                    return 0;
                case VK_SPACE:
                    if (s->focusIndex >= 0) {
                        g_controller->toggleFavoriteAtForTest(s->focusIndex);
                    }
                    return 0;
                case VK_DELETE:
                    if (s->focusIndex >= 0) {
                        g_controller->deleteAtForTest(s->focusIndex);
                    }
                    return 0;
                default: break;
            }
            if (wp == VK_LEFT || wp == VK_RIGHT || wp == VK_UP || wp == VK_DOWN ||
                wp == VK_HOME || wp == VK_END) {
                next = std::clamp(next, 0, total - 1);
                if (next != s->focusIndex) {
                    s->focusIndex = next;
                    // Keep the focus cell in view.
                    const int row = next / cols;
                    if (row < s->scrollRow) s->scrollRow = row;
                    const int maxRow = s->scrollRow + std::max(0, s->layout.rows - 1);
                    if (row > maxRow) s->scrollRow = row - std::max(0, s->layout.rows - 1);
                    ::InvalidateRect(hwnd, nullptr, FALSE);
                }
                return 0;
            }
            break;
        }
        case WM_MOUSEWHEEL: {
            if (!s) break;
            const int delta = GET_WHEEL_DELTA_WPARAM(wp);
            const int step = delta > 0 ? -1 : 1;
            const int maxRow = std::max(0, (g_controller->rowCount() + s->layout.columns - 1) /
                                                s->layout.columns - s->layout.rows);
            s->scrollRow = std::clamp(s->scrollRow + step, 0, maxRow);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_SIZE: {
            if (!s) break;
            RECT client{};
            ::GetClientRect(hwnd, &client);
            s->layout = colorHistoryGridLayout(client.right, client.bottom - s->toolbarH(),
                                               ::GetDpiForWindow(hwnd));
            const int pad = s->scale(8);
            ::MoveWindow(s->searchEdit, pad, pad, s->scale(180), s->scale(22), TRUE);
            int bx = pad + s->scale(188);
            ::MoveWindow(s->favoritesCheck, bx, pad - s->scale(2), s->scale(74),
                         s->scale(24), TRUE);
            bx += s->scale(78);
            ::MoveWindow(s->rgbCheck, bx, pad - s->scale(2), s->scale(84), s->scale(24),
                         TRUE);
            bx += s->scale(88);
            ::MoveWindow(s->clearButton, bx, pad - s->scale(2), s->scale(68),
                         s->scale(24), TRUE);
            ::MoveWindow(s->countLabel,
                         std::max<int>(bx + s->scale(76), client.right - s->scale(150)),
                         pad, s->scale(140), s->scale(22), TRUE);
            ::InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMinTrackSize.x = kMinW;
            mmi->ptMinTrackSize.y = kMinH;
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;  // painted in WM_PAINT (double buffer)
        case WM_PAINT: {
            if (s && g_controller) {
                paintGrid(s, g_controller->visibleRows());
            } else {
                PAINTSTRUCT ps;
                HDC dc = ::BeginPaint(hwnd, &ps);
                (void)dc;
                ::EndPaint(hwnd, &ps);
            }
            return 0;
        }
        case WM_DESTROY: {
            if (s) {
                WINDOWPLACEMENT wp_{};
                wp_.length = sizeof(wp_);
                ::GetWindowPlacement(hwnd, &wp_);
                g_lastPlacement = wp_.rcNormalPosition;
                g_hasPlacement = true;
                if (s->hexFont) ::DeleteObject(s->hexFont);
                if (s->uiFont) ::DeleteObject(s->uiFont);
            }
            return 0;
        }
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

// ---------------------------------------------------------------------------
// Controller
// ---------------------------------------------------------------------------
ColorHistoryWindowController::ColorHistoryWindowController(core::HistoryStore* store)
    : store_(store) {
    impl_ = new Impl();
    g_impl = impl_;
    g_controller = this;
    copyRgb_ = readCopyRgbSetting();
}

ColorHistoryWindowController::~ColorHistoryWindowController() {
    if (g_impl == impl_) g_impl = nullptr;
    if (g_controller == this) g_controller = nullptr;
    delete impl_;
}

bool ColorHistoryWindowController::registerClass(void* hinstance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wndProc;
    wc.hInstance = static_cast<HINSTANCE>(hinstance);
    wc.hCursor = ::LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = kWndClass;
    // Idempotent: a second registration (AppMain path + smoke driver) is a
    // success, not an error.
    const ATOM atom = ::RegisterClassExW(&wc);
    return atom != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void ColorHistoryWindowController::show() {
    if (!store_) return;
    const HINSTANCE inst = static_cast<HINSTANCE>(::GetModuleHandleW(nullptr));
    if (!registerClass(inst)) return;

    if (!impl_->hwnd) {
        int x = CW_USEDEFAULT, y = CW_USEDEFAULT, w = kDefaultW, h = kDefaultH;
        if (g_hasPlacement) {
            x = g_lastPlacement.left;
            y = g_lastPlacement.top;
            w = std::max(kMinW, static_cast<int>(g_lastPlacement.right - g_lastPlacement.left));
            h = std::max(kMinH, static_cast<int>(g_lastPlacement.bottom - g_lastPlacement.top));
        }
        impl_->hwnd = ::CreateWindowExW(0, kWndClass, L"取色历史",
                                        WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr,
                                        nullptr, inst, nullptr);

        const int pad = impl_->scale(8);
        impl_->searchEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP |
            ES_AUTOHSCROLL, pad, pad, impl_->scale(180), impl_->scale(22), impl_->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kSearchEdit)), inst, nullptr);
        impl_->favoritesCheck = ::CreateWindowExW(
            0, L"BUTTON", L"仅收藏", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            pad + impl_->scale(188), pad - impl_->scale(2), impl_->scale(74),
            impl_->scale(24), impl_->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kFavoritesCheck)), inst, nullptr);
        impl_->rgbCheck = ::CreateWindowExW(
            0, L"BUTTON", L"复制 RGB", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            pad + impl_->scale(266), pad - impl_->scale(2), impl_->scale(84),
            impl_->scale(24), impl_->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kRgbCheck)), inst, nullptr);
        impl_->clearButton = ::CreateWindowExW(
            0, L"BUTTON", L"清空全部", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            pad + impl_->scale(354), pad - impl_->scale(2), impl_->scale(68),
            impl_->scale(24), impl_->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kClearButton)), inst, nullptr);
        impl_->countLabel = ::CreateWindowExW(
            0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_RIGHT, 0, pad,
            impl_->scale(140), impl_->scale(22), impl_->hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kCountLabel)), inst, nullptr);

        impl_->hexFont = ::CreateFontW(-impl_->scale(13), 0, 0, 0, FW_SEMIBOLD, FALSE,
                                       FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                       FIXED_PITCH | FF_MODERN, L"Consolas");
        impl_->uiFont = ::CreateFontW(-impl_->scale(12), 0, 0, 0, FW_NORMAL, FALSE, FALSE,
                                      FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                      CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                      DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
        ::SendMessageW(impl_->searchEdit, WM_SETFONT,
                      reinterpret_cast<WPARAM>(impl_->uiFont), TRUE);
        ::SendMessageW(impl_->favoritesCheck, WM_SETFONT,
                      reinterpret_cast<WPARAM>(impl_->uiFont), TRUE);
        ::SendMessageW(impl_->rgbCheck, WM_SETFONT,
                      reinterpret_cast<WPARAM>(impl_->uiFont), TRUE);
        ::SendMessageW(impl_->clearButton, WM_SETFONT,
                      reinterpret_cast<WPARAM>(impl_->uiFont), TRUE);
        ::SendMessageW(impl_->countLabel, WM_SETFONT,
                      reinterpret_cast<WPARAM>(impl_->uiFont), TRUE);
        ::SendMessageW(impl_->rgbCheck, BM_SETCHECK,
                       copyRgb_ ? BST_CHECKED : BST_UNCHECKED, 0);

        RECT client{};
        ::GetClientRect(impl_->hwnd, &client);
        impl_->layout = colorHistoryGridLayout(client.right,
                                               client.bottom - impl_->toolbarH(),
                                               ::GetDpiForWindow(impl_->hwnd));
    }

    applySearchForTest(L"");
    ::ShowWindow(impl_->hwnd, SW_SHOW);
    ::SetForegroundWindow(impl_->hwnd);
    ::InvalidateRect(impl_->hwnd, nullptr, FALSE);
}

void ColorHistoryWindowController::close() {
    if (impl_->hwnd) ::ShowWindow(impl_->hwnd, SW_HIDE);
}

bool ColorHistoryWindowController::isVisible() const {
    return impl_->hwnd && ::IsWindowVisible(impl_->hwnd) != FALSE;
}

void ColorHistoryWindowController::notifyColorPicked() {
    if (!isVisible()) return;
    refreshFromToolbar();  // keep the active filter; new row lands on top
    if (!rows_.empty()) {
        impl_->flashIndex = 0;  // highlight the newest (first) cell
        impl_->flashStart = ::GetTickCount64();
        ::SetTimer(impl_->hwnd, kCopyFlashTimer, 900, nullptr);
    }
    ::InvalidateRect(impl_->hwnd, nullptr, FALSE);
}

std::string ColorHistoryWindowController::hexAt(int index) const {
    if (index < 0 || index >= static_cast<int>(rows_.size())) return {};
    return rows_[static_cast<std::size_t>(index)].translatedText;
}

void ColorHistoryWindowController::applySearchForTest(const std::wstring& text) {
    if (!store_) return;
    std::string search = utf8Of(text);
    // The search box takes raw text; treat '#' and spaces as removable so
    // "1A2" and "#1A2" behave the same (spec example uses the bare fragment).
    if (!search.empty() && search[0] == '#') search.erase(0, 1);
    const bool favoritesOnly =
        impl_->favoritesCheck &&
        ::SendMessageW(impl_->favoritesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
    rows_ = store_->colorEntries(search, favoritesOnly);
    impl_->scrollRow = 0;
    if (impl_->focusIndex >= static_cast<int>(rows_.size())) impl_->focusIndex = -1;
    if (impl_->hwnd) {
        const std::wstring count = std::to_wstring(rows_.size()) + L" 色";
        ::SetWindowTextW(impl_->countLabel, count.c_str());
        ::InvalidateRect(impl_->hwnd, nullptr, FALSE);
    }
}

void ColorHistoryWindowController::toggleFavoriteAtForTest(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size()) || !store_) return;
    const core::TranslationHistory& row = rows_[static_cast<std::size_t>(index)];
    store_->setFavorite(row.id.toString(), !row.isFavorite);
    refreshFromToolbar();
}

void ColorHistoryWindowController::deleteAtForTest(int index) {
    if (index < 0 || index >= static_cast<int>(rows_.size()) || !store_) return;
    const core::TranslationHistory& row = rows_[static_cast<std::size_t>(index)];
    store_->remove(row.id.toString());
    refreshFromToolbar();
    if (impl_->focusIndex >= static_cast<int>(rows_.size())) {
        impl_->focusIndex = static_cast<int>(rows_.size()) - 1;
    }
}

void ColorHistoryWindowController::clearAllForTest() {
    if (!store_) return;
    std::string error;
    store_->removeCategory(core::HistoryCategory::Color, &error);
    refreshFromToolbar();
}

void ColorHistoryWindowController::setCopyFormatRgbForTest(bool rgb) {
    copyRgb_ = rgb;
    writeCopyRgbSetting(rgb);
}

}  // namespace tf
