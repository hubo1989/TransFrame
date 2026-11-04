// SettingsWindow.cpp — FR-35 minimal settings window (通用 + 快捷键), Win32.
//
// Layout (tab pages):
//   通用:   目标语言 combo · MTranServer 端点 edit · 保存 button · hint/status
//   快捷键: 5 rows (action label · current binding · 录制 button) + hint line
// Recording: click 录制 -> the window takes focus; the next non-modifier
// keydown (with GetKeyState modifiers) becomes the binding; Esc cancels.
// Conflicts: applySpecs' report drives red binding text + a hint line (FR-33).
#include "pch.h"

#include "tf/SettingsWindow.hpp"

#include <windowsx.h>

namespace tf {

namespace {
void settingsLog(const char* step, long code) {
    char path[MAX_PATH];
    const DWORD n = ::GetTempPathA(MAX_PATH, path);
    if (n == 0 || n >= MAX_PATH) return;
    lstrcatA(path, "TransFrameSettingsInit.log");
    HANDLE f = ::CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[160];
    const int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s code=%ld\n",
                                step, code);
    if (len > 0) {
        DWORD written = 0;
        ::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
    }
    ::CloseHandle(f);
}
}  // namespace


namespace {

constexpr wchar_t kSettingsWndClass[] = L"TransFrameSettingsWnd";
constexpr int kWindowWidth = 680;
constexpr int kWindowHeight = 470;

enum ControlIds {
    kTabs = 100,
    kTargetLangCombo = 101,
    kBaseUrlEdit = 102,
    kSaveButton = 103,
    kGeneralHint = 104,
    kGeneralStatus = 105,
    kRowFirst = 110,  // row r: label=kRowFirst+3r, binding=+1, button=+2
};

const wchar_t* const kLanguages[] = {
    L"zh", L"en", L"ja", L"ko", L"fr", L"de", L"es", L"ru", L"pt", L"it",
};

const wchar_t* const kActionLabels[5] = {
    L"全屏截图", L"区域截图", L"翻译模式", L"划词翻译", L"翻译并插入",
};

struct RowControls {
    HWND label = nullptr;
    HWND binding = nullptr;
    HWND button = nullptr;
};

UINT dpiScaled(UINT dpi, int px) { return static_cast<UINT>(::MulDiv(px, static_cast<int>(dpi), 96)); }

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
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (n <= 0) return {};
    std::string out(static_cast<std::size_t>(n), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), n, nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}

}  // namespace

struct SettingsWindowController::Impl {
    HWND hwnd = nullptr;
    HINSTANCE instance = nullptr;
    SettingsWindowCallbacks callbacks;

    HWND pageButtons[2] = {nullptr, nullptr};
    HWND langCombo = nullptr;
    HWND baseUrlEdit = nullptr;
    HWND saveButton = nullptr;
    HWND generalHint = nullptr;
    HWND generalStatus = nullptr;
    RowControls rows[5];
    HFONT font = nullptr;
    HBRUSH bgBrush = nullptr;
    COLORREF conflictColor = RGB(200, 30, 30);

    std::vector<HotkeySpec> specs;
    HotkeyRegistrationReport report;
    int recordingRow = -1;  // -1 = idle; else index into rows/specs
    bool pageGeneral = true;

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

    void applyFont(HWND h) const {
        if (font && h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }

    HWND makeControl(const wchar_t* cls, const wchar_t* text, DWORD style,
                     int x, int y, int w, int hgt, int id, HWND parent) const {
        HWND c = ::CreateWindowExW(0, cls, text, style | WS_CHILD | WS_CLIPSIBLINGS,
                                   x, y, w, hgt, parent,
                                   reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                   instance, nullptr);
        applyFont(c);
        return c;
    }

    RECT pageRect() const {
        RECT rc{};
        ::GetClientRect(hwnd, &rc);
        UINT dpi = ::GetDpiForWindow(hwnd);
        const int pad = dpiScaled(dpi, 14);
        RECT page{static_cast<LONG>(rc.left + pad),
                  static_cast<LONG>(rc.top + pad + dpiScaled(dpi, 30)),
                  static_cast<LONG>(rc.right - pad),
                  static_cast<LONG>(rc.bottom - pad)};
        return page;
    }

    void buildControls() {
        UINT dpi = ::GetDpiForWindow(hwnd);
        // MSIX-container note: comctl32 classes (SysTabControl32, ComboBox)
        // fail to register inside the packaged process (CreateWindowExW
        // returns null, GetLastError 183) while user32 classes work — the
        // page switcher is therefore two plain buttons.
        settingsLog("pg-btn0-create", 0);
        pageButtons[0] = makeControl(WC_BUTTONW, L"通用", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     dpiScaled(dpi, 8), dpiScaled(dpi, 8),
                                     dpiScaled(dpi, 80), dpiScaled(dpi, 26), kTabs, hwnd);
        settingsLog("pg-btn1-create", 0);
        pageButtons[1] = makeControl(WC_BUTTONW, L"快捷键", WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     dpiScaled(dpi, 92), dpiScaled(dpi, 8),
                                     dpiScaled(dpi, 80), dpiScaled(dpi, 26), kTabs + 1, hwnd);
        settingsLog("general-page-begin", 0);
        buildGeneralPage();
        settingsLog("general-page-done", 0);
        settingsLog("hotkey-page-begin", 0);
        buildHotkeyPage();
        settingsLog("hotkey-page-done", 0);
        showPage(true);
        settingsLog("showPage-done", 0);
    }

    void buildGeneralPage() {
        UINT dpi = ::GetDpiForWindow(hwnd);
        const RECT page = pageRect();
        const int x1 = page.left;
        const int x2 = page.left + dpiScaled(dpi, 110);
        const int y0 = page.top + dpiScaled(dpi, 12);
        const int rowH = dpiScaled(dpi, 30);
        const int ctlW = dpiScaled(dpi, 360);

        makeControl(WC_STATICW, L"目标语言", WS_VISIBLE, x1, y0 + 4,
                    dpiScaled(dpi, 100), dpiScaled(dpi, 20), kGeneralHint, hwnd);
        langCombo = makeControl(WC_EDITW, L"zh",
                                WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                x2, y0, dpiScaled(dpi, 60), dpiScaled(dpi, 24),
                                kTargetLangCombo, hwnd);
        makeControl(WC_STATICW, L"（zh/en/ja/ko/fr/de/es/ru/pt/it）",
                    WS_VISIBLE, x2 + dpiScaled(dpi, 70), y0 + 4,
                    ctlW - dpiScaled(dpi, 70), dpiScaled(dpi, 20), kGeneralHint, hwnd);

        const int y1 = y0 + rowH + dpiScaled(dpi, 8);
        makeControl(WC_STATICW, L"MTranServer 端点", WS_VISIBLE, x1, y1 + 4,
                    dpiScaled(dpi, 100), dpiScaled(dpi, 20), kGeneralHint, hwnd);
        baseUrlEdit = makeControl(WC_EDITW, L"",
                                  WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                  x2, y1, ctlW, dpiScaled(dpi, 24), kBaseUrlEdit, hwnd);
        ::SendMessageW(baseUrlEdit, EM_SETCUEBANNER, TRUE,
                       reinterpret_cast<LPARAM>(L"http://localhost:8989"));

        const int y2 = y1 + rowH + dpiScaled(dpi, 8);
        saveButton = makeControl(WC_BUTTONW, L"保存", WS_VISIBLE | WS_TABSTOP,
                                 x2, y2, dpiScaled(dpi, 90), dpiScaled(dpi, 26),
                                 kSaveButton, hwnd);
        generalStatus = makeControl(WC_STATICW, L"", WS_VISIBLE, x2 + dpiScaled(dpi, 100),
                                    y2 + 6, ctlW, dpiScaled(dpi, 20), kGeneralStatus, hwnd);

        const int y3 = y2 + rowH + dpiScaled(dpi, 4);
        makeControl(WC_STATICW,
                    L"写入 %APPDATA%\\TransFrame\\engines.json（与引擎管理共享）",
                    WS_VISIBLE, x1, y3, dpiScaled(dpi, 520), dpiScaled(dpi, 20),
                    kGeneralHint, hwnd);
    }

    void buildHotkeyPage() {
        UINT dpi = ::GetDpiForWindow(hwnd);
        const RECT page = pageRect();
        const int xLabel = page.left;
        const int xBinding = page.left + dpiScaled(dpi, 120);
        const int xButton = page.left + dpiScaled(dpi, 330);
        int y = page.top + dpiScaled(dpi, 12);
        const int rowH = dpiScaled(dpi, 34);
        for (int i = 0; i < 5; ++i) {
            rows[i].label = makeControl(WC_STATICW, kActionLabels[i], WS_VISIBLE,
                                        xLabel, y + 4, dpiScaled(dpi, 110),
                                        dpiScaled(dpi, 20), kRowFirst + 3 * i, hwnd);
            rows[i].binding = makeControl(WC_STATICW, L"", WS_VISIBLE,
                                          xBinding, y + 4, dpiScaled(dpi, 190),
                                          dpiScaled(dpi, 20), kRowFirst + 3 * i + 1, hwnd);
            rows[i].button = makeControl(WC_BUTTONW, L"录制…", WS_VISIBLE | WS_TABSTOP,
                                         xButton, y, dpiScaled(dpi, 80),
                                         dpiScaled(dpi, 24), kRowFirst + 3 * i + 2, hwnd);
            y += rowH;
        }
        makeControl(WC_STATICW,
                    L"点击录制后按下新组合键（需含至少一个修饰键），Esc 取消；冲突会标红",
                    WS_VISIBLE, xLabel, y + dpiScaled(dpi, 8), dpiScaled(dpi, 540),
                    dpiScaled(dpi, 20), kGeneralHint, hwnd);
    }

    void showPage(bool general) {
        pageGeneral = general;
        const BOOL g = general ? TRUE : FALSE;
        if (langCombo) ::ShowWindow(langCombo, g);
        if (baseUrlEdit) ::ShowWindow(baseUrlEdit, g);
        if (saveButton) ::ShowWindow(saveButton, g);
        if (generalStatus) ::ShowWindow(generalStatus, g);
        for (const RowControls& r : rows) {
            if (r.label) ::ShowWindow(r.label, general ? FALSE : TRUE);
            if (r.binding) ::ShowWindow(r.binding, general ? FALSE : TRUE);
            if (r.button) ::ShowWindow(r.button, general ? FALSE : TRUE);
        }
        stopRecording();
    }

    void reloadFromCallbacks() {
        if (callbacks.currentSpecs) specs = callbacks.currentSpecs();
        // Startup conflicts included: the app applies specs before the window
        // opens, so the initial red marks must come from that report (FR-33).
        if (callbacks.lastReport) report = callbacks.lastReport();
        else {
            report = HotkeyRegistrationReport{};
            report.allRegistered = true;
        }
        for (int i = 0; i < 5; ++i) refreshRow(i);

        if (!callbacks.loadEngineConfig) return;
        const core::EngineConfig current = callbacks.loadEngineConfig();
        if (langCombo) {
            ::SetWindowTextW(langCombo, utf8ToWide(current.targetLang).c_str());
        }
        if (baseUrlEdit) {
            const std::wstring url = utf8ToWide(current.mtranBaseUrl);
            ::SetWindowTextW(baseUrlEdit, url.c_str());
        }
        if (generalStatus) ::SetWindowTextW(generalStatus, L"");
    }

    const HotkeySpec* specFor(int row) const {
        for (const HotkeySpec& s : specs) {
            if (static_cast<int>(s.action) == row) return &s;
        }
        return nullptr;
    }

    bool rowHasConflict(int row) const {
        for (const HotkeyConflict& c : report.conflicts) {
            if (static_cast<int>(c.action) == row) return true;
        }
        return false;
    }

    void refreshRow(int row) {
        const HotkeySpec* s = specFor(row);
        std::wstring text = s && s->enabled ? formatHotkeySpec(*s) : L"未设置";
        if (rowHasConflict(row)) text += L"（冲突）";
        ::SetWindowTextW(rows[row].binding, text.c_str());
        ::InvalidateRect(rows[row].binding, nullptr, TRUE);
    }

    void refreshAllRows() {
        for (int i = 0; i < 5; ++i) refreshRow(i);
    }

    void stopRecording() {
        if (recordingRow < 0) return;
        recordingRow = -1;
        for (const RowControls& r : rows) {
            if (r.button) ::SetWindowTextW(r.button, L"录制…");
        }
        if (generalStatus && !pageGeneral) {
            ::SetWindowTextW(generalStatus, L"");
        }
    }

    void startRecording(int row) {
        recordingRow = row;
        for (int i = 0; i < 5; ++i) {
            ::SetWindowTextW(rows[i].button, i == row ? L"按组合键…(Esc 取消)" : L"录制…");
        }
        ::SetFocus(hwnd);
    }

    /// Returns true when the keydown completed (or cancelled) a recording.
    bool handleRecordingKey(UINT vk) {
        if (recordingRow < 0) return false;
        const int row = recordingRow;
        if (vk == VK_ESCAPE) {
            stopRecording();
            return true;
        }
        if (vk == VK_CONTROL || vk == VK_MENU || vk == VK_SHIFT ||
            vk == VK_LWIN || vk == VK_RWIN) {
            return true;  // wait for the non-modifier key
        }
        const bool alt = (::GetKeyState(VK_MENU) & 0x8000) != 0;
        const bool ctrl = (::GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool shift = (::GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool win = ((::GetKeyState(VK_LWIN) | ::GetKeyState(VK_RWIN)) & 0x8000) != 0;
        stopRecording();

        HotkeySpec next;
        next.action = static_cast<HotkeyAction>(row);
        next.vk = vk;
        next.modifiers = (alt ? kModAlt : 0u) | (ctrl ? kModControl : 0u) |
                         (shift ? kModShift : 0u) | (win ? kModWin : 0u);
        next.enabled = true;

        if (!isHotkeySpecValid(next)) {
            if (generalStatus) ::SetWindowTextW(generalStatus, L"快捷键需包含至少一个修饰键（Ctrl/Alt/Shift/Win）");
            return true;
        }
        std::vector<HotkeySpec> edited = specs;
        for (HotkeySpec& s : edited) {
            if (s.action == next.action) s = next;
        }
        if (findDuplicateSpec(edited)) {
            if (generalStatus) ::SetWindowTextW(generalStatus, L"该组合键已被本应用其他动作占用");
            return true;
        }
        if (callbacks.applySpecs) {
            report = callbacks.applySpecs(edited);
            specs = edited;
            refreshAllRows();
            if (generalStatus) {
                ::SetWindowTextW(generalStatus, report.allRegistered
                                                    ? L"已应用并保存"
                                                    : L"部分快捷键注册失败（标红项与其他程序冲突）");
            }
        }
        return true;
    }

    LRESULT handle(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
        switch (msg) {
            case WM_CREATE: {
                settingsLog("WM_CREATE-enter", 0);
                UINT dpi = ::GetDpiForWindow(h);
                font = ::CreateFontW(-::MulDiv(15, static_cast<int>(dpi), 96), 0, 0, 0,
                                     FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                     OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                                     L"Segoe UI");
                buildControls();
                settingsLog("buildControls-done", pageButtons[0] ? 1 : 0);
                reloadFromCallbacks();
                settingsLog("reload done", 0);
                ::SetTimer(h, 1, 500, nullptr);  // heartbeat: message-pump liveness
                return 0;
            }
            case WM_COMMAND: {
                const int id = LOWORD(wp);
                const int code = HIWORD(wp);
                if ((id == kTabs || id == kTabs + 1) && code == BN_CLICKED) {
                    showPage(id == kTabs);
                    ::InvalidateRect(h, nullptr, TRUE);
                    return 0;
                }
                if (id == kSaveButton && code == BN_CLICKED) {
                    onGeneralSave();
                    return 0;
                }
                if (code == BN_CLICKED && id >= kRowFirst && id < kRowFirst + 15) {
                    const int offset = id - kRowFirst;
                    if (offset % 3 == 2) startRecording(offset / 3);
                    return 0;
                }
                break;
            }
            case WM_TIMER:
                if (wp == 1) { ::KillTimer(h, 1); settingsLog("HEARTBEAT pump-alive", 0); }
                return 0;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                ::BeginPaint(h, &ps);
                ::EndPaint(h, &ps);
                settingsLog("WM_PAINT", 0);
                return 0;
            }
            case WM_KEYDOWN:
                if (recordingRow >= 0) {
                    handleRecordingKey(static_cast<UINT>(wp));
                    return 0;
                }
                break;
            case WM_CTLCOLORSTATIC: {
                const HDC dc = reinterpret_cast<HDC>(wp);
                const HWND ctl = reinterpret_cast<HWND>(lp);
                for (int i = 0; i < 5; ++i) {
                    if (ctl == rows[i].binding && rowHasConflict(i)) {
                        ::SetTextColor(dc, conflictColor);
                        if (!bgBrush) bgBrush = ::GetSysColorBrush(COLOR_WINDOW);
                        return reinterpret_cast<LRESULT>(bgBrush);
                    }
                }
                break;
            }
            case WM_CLOSE:
                ::ShowWindow(h, SW_HIDE);
                return 0;
            default:
                break;
        }
        return ::DefWindowProcW(h, msg, wp, lp);
    }

    void onGeneralSave() {
        if (!callbacks.saveEngineConfig) return;
        core::EngineConfig next;
        wchar_t buf[256] = {};
        if (langCombo) {
            ::GetWindowTextW(langCombo, buf, 256);
            next.targetLang = wideToUtf8(buf);
        }
        if (baseUrlEdit) {
            wchar_t url[512] = {};
            ::GetWindowTextW(baseUrlEdit, url, 512);
            next.mtranBaseUrl = wideToUtf8(url);
        }
        if (next.targetLang.empty()) next.targetLang = "zh";
        const std::wstring err = callbacks.saveEngineConfig(next);
        if (generalStatus) {
            ::SetWindowTextW(generalStatus, err.empty() ? L"已保存" : err.c_str());
        }
    }
};

// ---------------------------------------------------------------------------
// Controller methods
// ---------------------------------------------------------------------------
SettingsWindowController::SettingsWindowController(SettingsWindowCallbacks callbacks)
    : impl_(new Impl()) {
    impl_->callbacks = std::move(callbacks);
}

SettingsWindowController::~SettingsWindowController() {
    close();
    impl_.reset();
}

bool SettingsWindowController::registerClass(HINSTANCE instance) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Impl::wndProc;
    wc.hInstance = instance;
    wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = ::GetSysColorBrush(COLOR_WINDOW);
    wc.lpszClassName = kSettingsWndClass;
    return ::RegisterClassExW(&wc) != 0 || ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

void SettingsWindowController::show() {
    if (impl_->hwnd) {
        impl_->reloadFromCallbacks();
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
        0, kSettingsWndClass, L"TransFrame 设置",
        WS_OVERLAPPEDWINDOW & ~(WS_MAXIMIZEBOX | WS_THICKFRAME),
        x, y, kWindowWidth, kWindowHeight, nullptr, nullptr,
        impl_->instance, impl_.get());
    if (!impl_->hwnd) return;
    ::ShowWindow(impl_->hwnd, SW_SHOW);
    ::SetForegroundWindow(impl_->hwnd);
}

void SettingsWindowController::close() {
    if (impl_ && impl_->hwnd) {
        ::DestroyWindow(impl_->hwnd);
        impl_->hwnd = nullptr;
    }
}

bool SettingsWindowController::isVisible() const {
    return impl_ && impl_->hwnd && ::IsWindowVisible(impl_->hwnd);
}

}  // namespace tf
