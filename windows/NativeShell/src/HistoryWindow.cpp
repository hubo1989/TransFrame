// HistoryWindow.cpp — FR-30 full history window (Win32).
//
// macOS HistoryView parity in a ListView shell:
//   * left category filter (全部/翻译/截图/取色/上传) + 仅看收藏 toggle
//   * search box -> HistoryStore LIKE (原文/译文), 300ms debounce
//   * per-category rows: translation 原文→译文+语言+时间; screenshot 96px
//     WIC thumbnail + 时间; color 色块+HEX; upload URL+provider+时间
//   * context menu / buttons: 收藏切换、删除、复制原文/译文/全部
//     (双击: 截图在预览窗打开, 其余复制译文)
//   * 导出 JSON/CSV/TXT through IFileSaveDialog (serializeHistoryExport)
//   * capacity line 共 N 条 · 收藏 M 条
// The store semantics stay in Core (HistoryStore); this TU only renders.
#include "pch.h"

#include "tf/HistoryWindow.hpp"

#include "tf/HistoryExport.hpp"
#include "tf/PreviewWindow.hpp"
#include "tf/RecentShots.hpp"
#include "tf/ThumbnailCodec.hpp"

#include <commctrl.h>
#include <shobjidl.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace tf {

namespace {

using Microsoft::WRL::ComPtr;

constexpr wchar_t kHistoryWndClass[] = L"TransFrameHistoryWnd";
constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 640;
constexpr UINT kSearchDebounceTimer = 7;  // 300ms after the last keystroke

// Thumbnail/icon column: WIC renders screenshot thumbs at 96px (FR-30).
constexpr int kThumbPx = 96;

enum ControlIds {
    kSearchEdit = 200,
    kExportButton = 210,
    kRefreshButton = 211,
    kFavoriteButton = 212,
    kCopyButton = 213,
    kDeleteButton = 214,
    kCatAll = 220,
    kCatTranslation = 221,
    kCatScreenshot = 222,
    kCatColor = 223,
    kCatUpload = 224,
    kFavoritesCheck = 230,
    kListView = 240,
    kCountLabel = 241,
};

enum ContextMenuIds {
    kMenuCopySource = 300,
    kMenuCopyTranslated = 301,
    kMenuCopyBoth = 302,
    kMenuToggleFavorite = 303,
    kMenuDelete = 304,
    kMenuOpenPreview = 305,
};

std::wstring utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return {};
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(static_cast<std::size_t>(n), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), n);
    out.resize(out.size() - 1);
    return out;
}

std::string wideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return {};
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0,
                                        nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
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

/// First line, clipped for one-line list cells.
std::wstring firstLine(const std::string& utf8) {
    std::wstring wide = utf8ToWide(utf8);
    const std::size_t nl = wide.find(L'\n');
    if (nl != std::wstring::npos) wide.resize(nl);
    return wide;
}

bool copyTextToClipboard(HWND owner, const std::wstring& text) {
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

/// BGRA pixels -> top-down 32bpp HBITMAP (thumbnail / color swatch source).
HBITMAP bitmapFromPixels(const PixelBuffer& px) {
    if (!px.valid()) return nullptr;
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = px.width;
    bmi.bmiHeader.biHeight = -px.height;  // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bm = ::CreateDIBSection(nullptr, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bm || !bits) return nullptr;
    const std::size_t rowBytes = static_cast<std::size_t>(px.width) * 4;
    for (std::int32_t row = 0; row < px.height; ++row) {
        std::memcpy(static_cast<std::uint8_t*>(bits) + row * rowBytes,
                    px.data.get() + static_cast<std::size_t>(row) *
                                        static_cast<std::size_t>(px.stride),
                    rowBytes);
    }
    return bm;
}

/// Solid swatch for a "#RRGGBB" history color entry.
HBITMAP swatchBitmap(const std::string& hex) {
    if (hex.size() < 7 || hex[0] != '#') return nullptr;
    const auto byteAt = [&](std::size_t i) {
        auto digit = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        return digit(hex[i]);
    };
    const int r = byteAt(1), g = byteAt(2), b = byteAt(3);
    if (r < 0 || g < 0 || b < 0) return nullptr;
    PixelBuffer px;
    px.width = 48;
    px.height = 48;
    px.stride = 48 * 4;
    px.data = std::shared_ptr<std::uint8_t[]>(new std::uint8_t[48 * 48 * 4]);
    for (int i = 0; i < 48 * 48; ++i) {
        px.data.get()[i * 4 + 0] = static_cast<std::uint8_t>(b << 4 | b);
        px.data.get()[i * 4 + 1] = static_cast<std::uint8_t>(g << 4 | g);
        px.data.get()[i * 4 + 2] = static_cast<std::uint8_t>(r << 4 | r);
        px.data.get()[i * 4 + 3] = 255;
    }
    return bitmapFromPixels(px);
}

}  // namespace

struct HistoryWindowController::Impl {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    core::HistoryStore* store = nullptr;

    HWND searchEdit = nullptr;
    HWND exportButton = nullptr;
    HWND refreshButton = nullptr;
    HWND favoriteButton = nullptr;
    HWND copyButton = nullptr;
    HWND deleteButton = nullptr;
    HWND favoritesCheck = nullptr;
    HWND list = nullptr;
    HWND countLabel = nullptr;
    HFONT font = nullptr;
    HIMAGELIST images = nullptr;
    std::map<std::string, int> swatchIndex;  // hex -> image list index

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

    int scale(int units) const {
        const UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : 96;
        return ::MulDiv(units, static_cast<int>(dpi), 96);
    }

    void buildControls() {
        const int pad = scale(12);
        const int barH = scale(30);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);

        // ---- top bar: search + actions ----------------------------------
        searchEdit = makeControl(WC_EDITW, L"",
                                 WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                 pad, pad, scale(280), barH, kSearchEdit, hwnd);
        ::SendMessageW(searchEdit, EM_SETCUEBANNER, TRUE,
                       reinterpret_cast<LPARAM>(L"搜索原文/译文"));
        int bx = pad + scale(288);
        const int btnW = scale(68);
        exportButton = makeControl(WC_BUTTONW, L"导出 ▾",
                                   WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   bx, pad, btnW, barH, kExportButton, hwnd);
        bx += btnW + scale(6);
        refreshButton = makeControl(WC_BUTTONW, L"刷新", WS_VISIBLE | WS_TABSTOP,
                                    bx, pad, btnW, barH, kRefreshButton, hwnd);
        bx += btnW + scale(6);
        favoriteButton = makeControl(WC_BUTTONW, L"收藏", WS_VISIBLE,
                                     bx, pad, btnW, barH, kFavoriteButton, hwnd);
        bx += btnW + scale(6);
        copyButton = makeControl(WC_BUTTONW, L"复制", WS_VISIBLE,
                                 bx, pad, btnW, barH, kCopyButton, hwnd);
        bx += btnW + scale(6);
        deleteButton = makeControl(WC_BUTTONW, L"删除", WS_VISIBLE,
                                   bx, pad, btnW, barH, kDeleteButton, hwnd);

        // ---- left panel: category filter + favorites toggle ---------------
        const int leftW = scale(104);
        const int panelTop = pad + barH + scale(8);
        struct Cat { int id; const wchar_t* label; };
        const Cat cats[] = {{kCatAll, L"全部"},
                            {kCatTranslation, L"翻译"},
                            {kCatScreenshot, L"截图"},
                            {kCatColor, L"取色"},
                            {kCatUpload, L"上传"}};
        constexpr int kCatCount = sizeof(cats) / sizeof(cats[0]);
        int cy = panelTop + scale(6);
        const DWORD radioBase = WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON;
        for (int i = 0; i < kCatCount; ++i) {
            const DWORD style = i == 0 ? radioBase | WS_GROUP : radioBase;
            HWND radio = makeControl(WC_BUTTONW, cats[i].label, style, pad, cy, leftW,
                                     scale(26), cats[i].id, hwnd);
            if (i == 0) ::SendMessageW(radio, BM_SETCHECK, BST_CHECKED, 0);
            cy += scale(26);
        }
        favoritesCheck = makeControl(WC_BUTTONW, L"仅看收藏",
                                     WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                     pad, cy + scale(10), leftW, scale(26),
                                     kFavoritesCheck, hwnd);

        // ---- list ---------------------------------------------------------
        const int listX = pad + leftW + scale(8);
        list = makeControl(WC_LISTVIEWW, nullptr,
                           WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL |
                               LVS_SHOWSELALWAYS | LVS_NOSORTHEADER,
                           listX, panelTop, rc.right - listX - pad,
                           rc.bottom - panelTop - pad - scale(26), kListView, hwnd);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES |
                                                     LVS_EX_LABELTIP | LVS_EX_DOUBLEBUFFER);

        images = ::ImageList_Create(kThumbPx, kThumbPx, ILC_COLOR32, 16, 16);
        if (images) ListView_SetImageList(list, images, LVSIL_SMALL);

        LVCOLUMNW col{};
        col.mask = LVCF_TEXT | LVCF_WIDTH;
        col.pszText = const_cast<LPWSTR>(L"缩略");
        col.cx = scale(kThumbPx + 8);
        ListView_InsertColumn(list, 0, &col);
        col.pszText = const_cast<LPWSTR>(L"类型");
        col.cx = scale(52);
        ListView_InsertColumn(list, 1, &col);
        col.pszText = const_cast<LPWSTR>(L"内容");
        col.cx = scale(360);
        ListView_InsertColumn(list, 2, &col);
        col.pszText = const_cast<LPWSTR>(L"详情");
        col.cx = scale(230);
        ListView_InsertColumn(list, 3, &col);
        col.pszText = const_cast<LPWSTR>(L"收藏");
        col.cx = scale(40);
        ListView_InsertColumn(list, 4, &col);

        countLabel = makeControl(WC_STATICW, L"", WS_VISIBLE, listX,
                                 rc.bottom - pad - scale(22), rc.right - listX - pad,
                                 scale(22), kCountLabel, hwnd);
        reload();
    }

    void layout() {
        if (!list) return;
        const int pad = scale(12);
        const int barH = scale(30);
        const int leftW = scale(104);
        const int panelTop = pad + barH + scale(8);
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        const int listX = pad + leftW + scale(8);
        ::MoveWindow(list, listX, panelTop, std::max<LONG>(1, rc.right - listX - pad),
                     std::max<LONG>(1, rc.bottom - panelTop - pad - scale(26)), TRUE);
        if (countLabel) {
            ::MoveWindow(countLabel, listX, rc.bottom - pad - scale(22),
                         std::max<LONG>(1, rc.right - listX - pad), scale(22), TRUE);
        }
    }

    std::optional<core::HistoryCategory> currentCategory() const {
        const struct { int id; std::optional<core::HistoryCategory> cat; } table[] = {
            {kCatAll, std::nullopt},
            {kCatTranslation, core::HistoryCategory::Translation},
            {kCatScreenshot, core::HistoryCategory::Screenshot},
            {kCatColor, core::HistoryCategory::Color},
            {kCatUpload, core::HistoryCategory::Upload},
        };
        for (const auto& row : table) {
            if (HWND btn = ::GetDlgItem(hwnd, row.id)) {
                if (::SendMessageW(btn, BM_GETCHECK, 0, 0) == BST_CHECKED) return row.cat;
            }
        }
        return std::nullopt;
    }

    core::HistoryQuery currentQuery() const {
        core::HistoryQuery q;
        wchar_t buf[256] = {};
        if (searchEdit) {
            ::GetWindowTextW(searchEdit, buf, 256);
            if (buf[0]) q.searchText = wideToUtf8(buf);
        }
        q.category = currentCategory();
        q.favoritesOnly = favoritesCheck &&
                          ::SendMessageW(favoritesCheck, BM_GETCHECK, 0, 0) == BST_CHECKED;
        q.limit = 500;
        return q;
    }

    /// Icon index for a row (thumbnails / swatches); -1 = no image.
    int imageIndexFor(const core::TranslationHistory& e) {
        if (!images) return -1;
        if (e.category == core::HistoryCategory::Color) {
            const std::string hex = e.translatedText;
            const auto it = swatchIndex.find(hex);
            if (it != swatchIndex.end()) return it->second;
            HBITMAP swatch = swatchBitmap(hex);
            if (!swatch) return -1;
            const int index = ::ImageList_Add(images, swatch, nullptr);
            ::DeleteObject(swatch);
            if (index >= 0) swatchIndex.emplace(hex, index);
            return index;
        }
        if (e.category == core::HistoryCategory::Screenshot && e.hasThumbnail()) {
            // FR-30: 96px WIC thumbnail straight from the stored JPEG blob.
            PixelBuffer px = decodeImageScaled(e.thumbnailData->data(),
                                               e.thumbnailData->size(), kThumbPx);
            HBITMAP bm = bitmapFromPixels(px);
            if (!bm) return -1;
            const int index = ::ImageList_Add(images, bm, nullptr);
            ::DeleteObject(bm);
            return index;
        }
        return -1;
    }

    void reload() {
        ListView_DeleteAllItems(list);
        if (images) {
            ::ImageList_RemoveAll(images);
            swatchIndex.clear();
        }
        rows.clear();
        if (store) rows = store->query(currentQuery());
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const core::TranslationHistory& e = rows[i];
            LVITEMW item{};
            item.mask = LVIF_TEXT;
            const int image = imageIndexFor(e);
            if (image >= 0) {
                item.mask |= LVIF_IMAGE;
                item.iImage = image;
            }
            item.iItem = static_cast<int>(i);
            std::wstring cell0;
            switch (e.category) {
                case core::HistoryCategory::Color:
                    cell0 = utf8ToWide(e.translatedText);  // HEX with swatch
                    break;
                case core::HistoryCategory::Screenshot:
                    cell0 = firstLine(e.sourceText);
                    break;
                default:
                    break;
            }
            item.pszText = const_cast<LPWSTR>(cell0.c_str());
            ListView_InsertItem(list, &item);
            ListView_SetItemText(list, item.iItem, 1,
                                 const_cast<LPWSTR>(categoryLabel(e.category).c_str()));

            std::wstring content;
            std::wstring detail;
            const std::wstring time = formatTimestamp(e.timestamp);
            switch (e.category) {
                case core::HistoryCategory::Translation:
                    content = firstLine(e.sourceText) + L" → " +
                              firstLine(e.translatedText);
                    detail = utf8ToWide(e.sourceLanguage) + L" → " +
                             utf8ToWide(e.targetLanguage) + L" · " + time;
                    break;
                case core::HistoryCategory::Screenshot:
                    detail = time;
                    break;
                case core::HistoryCategory::Color:
                    detail = utf8ToWide(e.sourceLanguage) + L" · " + time;
                    break;
                case core::HistoryCategory::Upload:
                    content = firstLine(e.translatedText);  // URL
                    detail = utf8ToWide(e.sourceLanguage) + L" · " + time;
                    break;
                default:
                    break;
            }
            ListView_SetItemText(list, item.iItem, 2, const_cast<LPWSTR>(content.c_str()));
            ListView_SetItemText(list, item.iItem, 3, const_cast<LPWSTR>(detail.c_str()));
            const std::wstring favMark = e.isFavorite ? L"★" : L"";
            ListView_SetItemText(list, item.iItem, 4, const_cast<LPWSTR>(favMark.c_str()));
        }
        updateCapacity();
    }

    void updateCapacity() {
        if (!countLabel) return;
        wchar_t buf[96] = {};
        if (store) {
            ::swprintf_s(buf, L"共 %lld 条 · 收藏 %lld 条",
                         static_cast<long long>(store->totalEntries()),
                         static_cast<long long>(store->favoriteCount()));
        } else {
            ::wcscpy_s(buf, L"历史存储不可用");
        }
        ::SetWindowTextW(countLabel, buf);
    }

    void flash(const std::wstring& message) {
        if (countLabel) ::SetWindowTextW(countLabel, message.c_str());
    }

    int selectedRow() const {
        const int sel = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        if (sel < 0 || static_cast<std::size_t>(sel) >= rows.size()) return -1;
        return sel;
    }

    void onToggleFavorite() {
        const int sel = selectedRow();
        if (sel < 0 || !store) return;
        core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        if (!store->setFavorite(e.id.toString(), !e.isFavorite)) return;
        e.isFavorite = !e.isFavorite;
        const std::wstring favMark = e.isFavorite ? L"★" : L"";
        ListView_SetItemText(list, sel, 4, const_cast<LPWSTR>(favMark.c_str()));
        updateCapacity();
    }

    void onDelete() {
        const int sel = selectedRow();
        if (sel < 0 || !store) return;
        core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        if (!store->remove(e.id.toString())) return;
        reload();
    }

    void onCopy(int mode) {  // 0 source, 1 translated, 2 both
        const int sel = selectedRow();
        if (sel < 0) return;
        const core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        std::wstring text;
        if (mode == 0) text = utf8ToWide(e.sourceText);
        if (mode == 1) text = utf8ToWide(e.translatedText);
        if (mode == 2) {
            text = utf8ToWide(e.sourceText) + L"\n" + utf8ToWide(e.translatedText);
        }
        flash(copyTextToClipboard(hwnd, text) ? L"已复制到剪贴板" : L"复制失败");
    }

    void onOpenPreview() {
        const int sel = selectedRow();
        if (sel < 0) return;
        const core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        if (e.category != core::HistoryCategory::Screenshot) return;
        PixelBuffer px = shots::loadRecentShotPixels(e);
        if (!px.valid()) {
            flash(L"截图文件已清理");
            return;
        }
        PreviewWindowSpec spec;
        spec.image = std::make_shared<PixelBuffer>(std::move(px));
        spec.suggestedFileName = L"TransFrame_history_" +
                                 utf8ToWide(e.id.toString()).substr(0, 8);
        spec.titleSuffix = L" · " + std::to_wstring(spec.image->width) + L"×" +
                           std::to_wstring(spec.image->height);
        PreviewWindowController::instance().show(spec);
    }

    // ---- export ------------------------------------------------------------
    void onExportMenu() {
        POINT pt{};
        RECT rc{};
        ::GetWindowRect(exportButton, &rc);
        pt.x = rc.left;
        pt.y = rc.bottom;
        HMENU menu = ::CreatePopupMenu();
        ::AppendMenuW(menu, MF_STRING, 1, L"导出 JSON…");
        ::AppendMenuW(menu, MF_STRING, 2, L"导出 CSV…");
        ::AppendMenuW(menu, MF_STRING, 3, L"导出 TXT…");
        ::SetForegroundWindow(hwnd);
        const int picked = ::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x,
                                              pt.y, hwnd, nullptr);
        ::DestroyMenu(menu);
        ::PostMessageW(hwnd, WM_NULL, 0, 0);
        if (picked == 1) exportFlow(HistoryExportFormat::Json, L"json", L"JSON 文件");
        if (picked == 2) exportFlow(HistoryExportFormat::Csv, L"csv", L"CSV 文件");
        if (picked == 3) exportFlow(HistoryExportFormat::Txt, L"txt", L"文本文件");
    }

    void exportFlow(HistoryExportFormat format, const wchar_t* ext, const wchar_t* filterName) {
        // Re-read the current filter (rows may be stale after edits).
        std::vector<core::TranslationHistory> current =
            store ? store->query(currentQuery()) : std::vector<core::TranslationHistory>{};
        if (current.empty()) {
            flash(L"没有可导出的条目");
            return;
        }
        // COM for the common dialog (same apartment dance as PreviewWindow).
        const HRESULT com = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        struct ComGuard {
            HRESULT hr;
            ~ComGuard() { if (SUCCEEDED(hr)) ::CoUninitialize(); }
        } comGuard{com};
        constexpr HRESULT kRpcEAlreadyInitialized = 0x80010106;
        if (FAILED(com) && com != kRpcEAlreadyInitialized) {
            flash(L"COM 初始化失败，无法打开保存对话框");
            return;
        }

        ComPtr<IFileSaveDialog> dialog;
        if (FAILED(::CoCreateInstance(CLSID_FileSaveDialog, nullptr, CLSCTX_ALL,
                                      IID_PPV_ARGS(&dialog)))) {
            flash(L"无法打开保存对话框");
            return;
        }
        const std::wstring pattern = std::wstring(L"*.") + ext;
        const COMDLG_FILTERSPEC filters[] = {{filterName, pattern.c_str()},
                                             {L"所有文件", L"*.*"}};
        dialog->SetFileTypes(2, filters);
        dialog->SetFileTypeIndex(1);
        dialog->SetDefaultExtension(ext);
        const std::wstring defaultName = L"TransFrame历史." + std::wstring(ext);
        dialog->SetFileName(defaultName.c_str());
        dialog->SetTitle(L"导出翻译历史");
        const HRESULT shown = dialog->Show(hwnd);
        if (shown == HRESULT_FROM_WIN32(ERROR_CANCELLED)) return;
        if (FAILED(shown)) {
            flash(L"保存对话框失败");
            return;
        }
        ComPtr<IShellItem> item;
        PWSTR pathRaw = nullptr;
        if (FAILED(dialog->GetResult(&item)) || !item ||
            FAILED(item->GetDisplayName(SIGDN_FILESYSPATH, &pathRaw)) || !pathRaw) {
            return;
        }
        const std::wstring path(pathRaw);
        ::CoTaskMemFree(pathRaw);

        const std::string content = serializeHistoryExport(current, format);
        std::ofstream out(std::filesystem::path(path), std::ios::binary | std::ios::trunc);
        if (!out) {
            flash(L"无法写入导出文件");
            return;
        }
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.close();
        if (!out) {
            flash(L"写入导出文件失败");
            return;
        }
        flash(L"已导出 " + std::to_wstring(current.size()) + L" 条 → " + path);
    }

    void showContextMenu() {
        const int sel = selectedRow();
        if (sel < 0) return;
        const core::TranslationHistory& e = rows[static_cast<std::size_t>(sel)];
        HMENU menu = ::CreatePopupMenu();
        if (e.category == core::HistoryCategory::Screenshot) {
            ::AppendMenuW(menu, MF_STRING, kMenuOpenPreview, L"在预览窗打开");
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
        ::AppendMenuW(menu, MF_STRING, kMenuCopySource, L"复制原文");
        ::AppendMenuW(menu, MF_STRING, kMenuCopyTranslated, L"复制译文");
        ::AppendMenuW(menu, MF_STRING, kMenuCopyBoth, L"复制全部");
        ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(menu, MF_STRING, kMenuToggleFavorite,
                      e.isFavorite ? L"取消收藏" : L"收藏");
        ::AppendMenuW(menu, MF_STRING, kMenuDelete, L"删除");
        POINT pt{};
        ::GetCursorPos(&pt);
        ::SetForegroundWindow(hwnd);
        const int picked = ::TrackPopupMenuEx(menu, TPM_RETURNCMD | TPM_NONOTIFY, pt.x,
                                              pt.y, hwnd, nullptr);
        ::DestroyMenu(menu);
        ::PostMessageW(hwnd, WM_NULL, 0, 0);
        switch (picked) {
            case kMenuOpenPreview: onOpenPreview(); break;
            case kMenuCopySource: onCopy(0); break;
            case kMenuCopyTranslated: onCopy(1); break;
            case kMenuCopyBoth: onCopy(2); break;
            case kMenuToggleFavorite: onToggleFavorite(); break;
            case kMenuDelete: onDelete(); break;
            default: break;
        }
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
            case WM_SIZE:
                layout();
                return 0;
            case WM_TIMER:
                if (wp == kSearchDebounceTimer) {
                    ::KillTimer(h, kSearchDebounceTimer);
                    reload();
                }
                return 0;
            case WM_COMMAND: {
                const int id = LOWORD(wp);
                const int code = HIWORD(wp);
                if (id == kSearchEdit && code == EN_CHANGE) {
                    // Debounce: reload 300ms after the last keystroke.
                    ::SetTimer(h, kSearchDebounceTimer, 300, nullptr);
                } else if (code == BN_CLICKED &&
                           (id == kCatAll || id == kCatTranslation || id == kCatScreenshot ||
                            id == kCatColor || id == kCatUpload || id == kFavoritesCheck)) {
                    reload();
                } else if (id == kRefreshButton && code == BN_CLICKED) {
                    reload();
                } else if (id == kExportButton && code == BN_CLICKED) {
                    onExportMenu();
                } else if (id == kFavoriteButton && code == BN_CLICKED) {
                    onToggleFavorite();
                } else if (id == kCopyButton && code == BN_CLICKED) {
                    onCopy(1);
                } else if (id == kDeleteButton && code == BN_CLICKED) {
                    onDelete();
                }
                return 0;
            }
            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lp);
                if (nm && nm->idFrom == kListView) {
                    if (nm->code == NM_RCLICK) {
                        showContextMenu();
                    } else if (nm->code == NM_DBLCLK) {
                        // 截图行在预览窗打开，其余双击复制译文。
                        const int sel = selectedRow();
                        if (sel >= 0 &&
                            rows[static_cast<std::size_t>(sel)].category ==
                                core::HistoryCategory::Screenshot) {
                            onOpenPreview();
                        } else {
                            onCopy(1);
                        }
                    }
                }
                return 0;
            }
            case WM_CLOSE:
                ::ShowWindow(h, SW_HIDE);
                return 0;
            case WM_DESTROY:
                if (images) {
                    ::ImageList_Destroy(images);
                    images = nullptr;
                }
                if (font) {
                    ::DeleteObject(font);
                    font = nullptr;
                }
                return 0;
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
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
    if (impl_ && impl_->hwnd) ::DestroyWindow(impl_->hwnd);
    impl_.reset();
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
