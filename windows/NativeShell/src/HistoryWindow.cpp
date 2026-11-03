// HistoryWindow.cpp — FR-30 minimal history window (Win32 ListView shell).
#include "pch.h"

#include "tf/HistoryWindow.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <ctime>
#include <string>
#include <vector>

namespace tf {

namespace {

constexpr wchar_t kHistoryWndClass[] = L"TransFrameHistoryWnd";
constexpr int kWindowWidth = 860;
constexpr int kWindowHeight = 560;

enum ControlIds {
    kSearchEdit = 200,
    kFilterCombo = 201,
    kRefreshButton = 202,
    kFavoriteButton = 203,
    kDeleteButton = 204,
    kCountLabel = 205,
    kListView = 206,
};

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data());
    out.resize(out.size() - 1);
    return out;
}

std::wstring formatTimestamp(const core::TimePoint& t) {
    const std::time_t tt = std::chrono::system_clock::to_time_t(t);
    std::tm local{};
    if (::localtime_s(&local, &tt) != 0) return L"";
    wchar_t buf[32];
    ::swprintf_s(buf, L"%04d-%02d-%02d %02d:%02d:%02d", local.tm_year + 1900,
                 local.tm_mon + 1, local.tm_mday, local.tm_hour, local.tm_min,
                 local.tm_sec);
    return buf;
}

std::wstring categoryLabel(core::HistoryCategory c) {
    switch (c) {
        case core::HistoryCategory::Translation: return L"翻译";
        case core::HistoryCategory::Screenshot: return L"截图";
        case core::HistoryCategory::Color: return L"取色";
        case core::HistoryCategory::Upload: return L"上传";
        default: return L"";
    }
}

}  // namespace

struct HistoryWindowController::Impl {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    core::HistoryStore* store = nullptr;

    HWND searchEdit = nullptr;
    HWND filterCombo = nullptr;
    HWND refreshButton = nullptr;
    HWND favoriteButton = nullptr;
    HWND deleteButton = nullptr;
    HWND countLabel = nullptr;
    HWND list = nullptr;
    HFONT font = nullptr;

    std::vector<core::TranslationHistory> rows;

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

    void applyFont(HWND c) {
        if (font && c) ::SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    HWND makeControl(const wchar_t* cls, const wchar_t* text, DWORD style,
                     int x, int y, int w, int hgt, int id, HWND parent) {
        HWND c = ::CreateWindowExW(0, cls, text, style | WS_CHILD | WS_CLIPSIBLINGS,
                                   x, y, w, hgt, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   instance, nullptr);
        applyFont(c);
        return c;
    }

    void buildControls() {
        UINT dpi = ::GetDpiForWindow(hwnd);
        const int pad = ::MulDiv(12, static_cast<int>(dpi), 96);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        const int barH = ::MulDiv(28, static_cast<int>(dpi), 96);

        searchEdit = makeControl(WC_EDITW, L"",
                                 WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                 pad, pad, ::MulDiv(220, static_cast<int>(dpi), 96),
                                 barH, kSearchEdit, hwnd);
        ::SendMessageW(searchEdit, EM_SETCUEBANNER, TRUE,
                       reinterpret_cast<LPARAM>(L"搜索原文/译文"));
        filterCombo = makeControl(WC_COMBOBOXW, nullptr,
                                  WS_VISIBLE | CBS_DROPDOWNLIST | WS_TABSTOP,
                                  pad + ::MulDiv(232, static_cast<int>(dpi), 96), pad,
                                  ::MulDiv(90, static_cast<int>(dpi), 96),
                                  ::MulDiv(200, static_cast<int>(dpi), 96),
                                  kFilterCombo, hwnd);
        for (const wchar_t* f : {L"全部", L"翻译", L"收藏"}) {
            ::SendMessageW(filterCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(f));
        }
        ::SendMessageW(filterCombo, CB_SETCURSEL, 0, 0);

        const int bx = pad + ::MulDiv(332, static_cast<int>(dpi), 96);
        refreshButton = makeControl(WC_BUTTONW, L"刷新", WS_VISIBLE | WS_TABSTOP,
                                    bx, pad, ::MulDiv(64, static_cast<int>(dpi), 96),
                                    barH, kRefreshButton, hwnd);
        favoriteButton = makeControl(WC_BUTTONW, L"收藏", WS_VISIBLE,
                                     bx + ::MulDiv(72, static_cast<int>(dpi), 96), pad,
                                     ::MulDiv(64, static_cast<int>(dpi), 96), barH,
                                     kFavoriteButton, hwnd);
        deleteButton = makeControl(WC_BUTTONW, L"删除", WS_VISIBLE,
                                   bx + ::MulDiv(144, static_cast<int>(dpi), 96), pad,
                                   ::MulDiv(64, static_cast<int>(dpi), 96), barH,
                                   kDeleteButton, hwnd);

        list = makeControl(WC_LISTVIEWW, nullptr,
                           WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                               LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                           pad, pad + barH + ::MulDiv(8, static_cast<int>(dpi), 96),
                           rc.right - 2 * pad,
                           rc.bottom - pad * 2 - barH - ::MulDiv(28, static_cast<int>(dpi), 96),
                           kListView, hwnd);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        const int w = rc.right - 2 * pad;
        col.pszText = const_cast<LPWSTR>(L"时间");
        col.cx = ::MulDiv(150, static_cast<int>(dpi), 96);
        ListView_InsertColumn(list, 0, &col);
        col.pszText = const_cast<LPWSTR>(L"类型");
        col.cx = ::MulDiv(56, static_cast<int>(dpi), 96);
        ListView_InsertColumn(list, 1, &col);
        col.pszText = const_cast<LPWSTR>(L"原文");
        col.cx = w - ::MulDiv(206 + 320, static_cast<int>(dpi), 96);
        ListView_InsertColumn(list, 2, &col);
        col.pszText = const_cast<LPWSTR>(L"译文");
        col.cx = ::MulDiv(200, static_cast<int>(dpi), 96);
        ListView_InsertColumn(list, 3, &col);
        col.pszText = const_cast<LPWSTR>(L"收藏");
        col.cx = ::MulDiv(48, static_cast<int>(dpi), 96);
        ListView_InsertColumn(list, 4, &col);

        countLabel = makeControl(WC_STATICW, L"", WS_VISIBLE, pad,
                                 rc.bottom - pad - ::MulDiv(20, static_cast<int>(dpi), 96),
                                 w, ::MulDiv(20, static_cast<int>(dpi), 96),
                                 kCountLabel, hwnd);
        reload();
    }

    core::HistoryQuery currentQuery() const {
        core::HistoryQuery q;
        wchar_t buf[256] = {};
        if (searchEdit) {
            ::GetWindowTextW(searchEdit, buf, 256);
            const std::wstring wide(buf);
            if (!wide.empty()) {
                const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1,
                                                    nullptr, 0, nullptr, nullptr);
                if (n > 0) {
                    std::string out(static_cast<std::size_t>(n), '\0');
                    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n,
                                          nullptr, nullptr);
                    out.resize(out.size() - 1);
                    q.searchText = out;
                }
            }
        }
        const int filter = filterCombo
                               ? static_cast<int>(::SendMessageW(filterCombo, CB_GETCURSEL, 0, 0))
                               : 0;
        if (filter == 1) q.category = core::HistoryCategory::Translation;
        if (filter == 2) q.favoritesOnly = true;
        q.limit = 500;
        return q;
    }

    void reload() {
        ListView_DeleteAllItems(list);
        rows.clear();
        int count = 0;
        if (store) rows = store->query(currentQuery());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const core::TranslationHistory& e = rows[i];
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            item.iItem = static_cast<int>(i);
            const std::wstring time = formatTimestamp(e.timestamp);
            const std::wstring cat = categoryLabel(e.category);
            const std::wstring fav = e.isFavorite ? L"★" : L"";
            item.pszText = const_cast<LPWSTR>(time.c_str());
            ListView_InsertItem(list, &item);
            ListView_SetItemText(list, item.iItem, 1, const_cast<LPWSTR>(cat.c_str()));
            ListView_SetItemText(list, item.iItem, 2, const_cast<LPWSTR>(utf8ToWide(e.sourceText).c_str()));
            ListView_SetItemText(list, item.iItem, 3, const_cast<LPWSTR>(utf8ToWide(e.translatedText).c_str()));
            ListView_SetItemText(list, item.iItem, 4, const_cast<LPWSTR>(fav.c_str()));
        }
        count = static_cast<int>(rows.size());
        wchar_t countBuf[64];
        ::swprintf_s(countBuf, L"共 %d 条", count);
        ::SetWindowTextW(countLabel, countBuf);
    }

    int selectedRow() const {
        const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        if (sel < 0 || static_cast<std::size_t>(sel) >= rows.size()) return -1;
        return sel;
    }

    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                UINT dpi = ::GetDpiForWindow(h);
                font = ::CreateFontW(-::MulDiv(15, static_cast<int>(dpi), 96), 0, 0, 0,
                                     FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                     L"Segoe UI");
                buildControls();
                return 0;
            }
            case WM_COMMAND: {
                const int id = LOWORD(wp);
                const int code = HIWORD(wp);
                if (id == kSearchEdit && code == EN_CHANGE) {
                    reload();
                } else if ((id == kFilterCombo && code == CBN_SELCHANGE) ||
                           id == kRefreshButton) {
                    reload();
                } else if (id == kFavoriteButton && code == BN_CLICKED) {
                    onFavorite();
                } else if (id == kDeleteButton && code == BN_CLICKED) {
                    onDelete();
                }
                return 0;
            }
            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lp);
                if (nm && nm->idFrom == kListView && nm->code == NM_DBLCLK) {
                    onFavorite();  // double-click toggles favorite too
                }
                return 0;
            }
            case WM_CLOSE:
                ::ShowWindow(h, SW_HIDE);
                return 0;
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    void onFavorite() {
        const int sel = selectedRow();
        if (sel < 0 || !store) return;
        core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        store->setFavorite(e.id.toString(), !e.isFavorite);
        reload();
    }

    void onDelete() {
        const int sel = selectedRow();
        if (sel < 0 || !store) return;
        core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        store->remove(e.id.toString());
        reload();
    }
};

// ---------------------------------------------------------------------------
// Controller methods (mirror ResultWindow pattern)
// ---------------------------------------------------------------------------
HistoryWindowController::HistoryWindowController(core::HistoryStore* store)
    : impl_(new Impl()) {
    impl_->store = store;
}

HistoryWindowController::~HistoryWindowController() {
    if (impl_->hwnd) ::DestroyWindow(impl_->hwnd);
    delete impl_;
}

bool HistoryWindowController::registerClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Impl::wndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = ::GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = kHistoryWndClass;
    return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void HistoryWindowController::show() {
    if (impl_->hwnd) {
        impl_->reload();
        ::ShowWindow(impl_->hwnd, SW_SHOW);
        ::SetForegroundWindow(impl_->hwnd);
        return;
    }
    impl_->instance = ::GetModuleHandleW(nullptr);
    registerClass(impl_->instance);
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int x = work.left + (work.right - work.left - kWindowWidth) / 2;
    const int y = work.top + (work.bottom - work.top - kWindowHeight) / 3;
    impl_->hwnd = ::CreateWindowExW(
        0, kHistoryWndClass, L"翻译历史",
        WS_OVERLAPPEDWINDOW | WS_CAPTION,
        x, y, kWindowWidth, kWindowHeight, nullptr, nullptr,
        impl_->instance, impl_.get());
    if (!impl_->hwnd) return;
    ::ShowWindow(impl_->hwnd, SW_SHOW);
    ::SetForegroundWindow(impl_->hwnd);
}

void HistoryWindowController::close() {
    if (impl_ && impl_->hwnd) {
        ::DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

bool HistoryWindowController::isVisible() const {
    return impl_ && impl_->hwnd && ::IsWindowVisible(impl_->hwnd);
}

}  // namespace tf
