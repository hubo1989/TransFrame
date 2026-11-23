// OnboardingWindow.cpp — FR-39 first-run wizard (Win32, code-built).
//
// Steps: 0 hotkey intro (settings.json current values) -> 1 privacy rows
// (enabled engines, cloud/local classification) -> 2 default-chain cards
// (MTranServer local / Ollama / cloud). All strings go through the
// Localization tables (FR-37); Win32 has no screen-recording permission
// concept, so there is no permission step (PRD 阶段5b note).
#include "pch.h"

#include "tf/OnboardingWindow.hpp"

#include "tf/CoreTypes.hpp"
#include "tf/Localization.hpp"

#include <string>
#include <vector>

namespace tf {
namespace {

using core::loc::tw;

constexpr wchar_t kWndClass[] = L"TransFrameOnboardingWnd";

// ---------------------------------------------------------------------------
// Hotkey combo display (Ctrl/Alt/Shift/Win + key) — ShortcutsPageViewModel's
// FormatCombo kept in NativeShell form (VM headers are WinUI-side).
// ---------------------------------------------------------------------------
constexpr std::uint32_t kModAlt = 0x1, kModControl = 0x2, kModShift = 0x4, kModWin = 0x8;

std::wstring formatCombo(std::uint32_t vk, std::uint32_t modifiers) {
    std::wstring s;
    if (modifiers & kModControl) s += L"Ctrl+";
    if (modifiers & kModAlt) s += L"Alt+";
    if (modifiers & kModShift) s += L"Shift+";
    if (modifiers & kModWin) s += L"Win+";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        s += static_cast<wchar_t>(vk);
    } else if (vk >= 0x70 && vk <= 0x7B) {  // VK_F1..VK_F12
        s += L"F" + std::to_wstring(vk - 0x70 + 1);
    } else {
        wchar_t buf[16]{};
        swprintf_s(buf, L"0x%02X", vk);
        s += buf;
    }
    return s;
}

/// Shortcut action name -> localization key (settings.shortcut.* row labels).
const char* labelKeyForAction(const std::string& actionName) {
    if (actionName == "fullScreen") return "settings.shortcut.fullscreen";
    if (actionName == "region") return "settings.shortcut.selection";
    if (actionName == "translate") return "settings.shortcut.translation.mode";
    if (actionName == "textSelection") return "settings.shortcut.text.selection.translation";
    if (actionName == "translateInsert") return "settings.shortcut.translate.and.insert";
    return "settings.shortcut.fullscreen";
}

const wchar_t* labelFallbackForAction(const std::string& actionName) {
    if (actionName == "fullScreen") return L"全屏截图";
    if (actionName == "region") return L"区域截图";
    if (actionName == "translate") return L"翻译模式";
    if (actionName == "textSelection") return L"文本选择翻译";
    if (actionName == "translateInsert") return L"翻译并插入";
    return L"全屏截图";
}

// ---------------------------------------------------------------------------
// Wizard state
// ---------------------------------------------------------------------------
struct Wizard {
    core::OnboardingFlow flow;
    std::vector<core::HotkeyBinding> hotkeys;      // step 0 rows
    std::vector<core::OnboardingEngineRow> engines;// step 1 rows (enabled only)
    bool running = true;
    bool finished = false;                         // finish or skip (not abort-fail)
    HWND hwnd = nullptr;
    HFONT font = nullptr;
    HFONT fontTitle = nullptr;
    HFONT fontBold = nullptr;
    std::vector<HWND> stepControls;                // rebuilt per step
    int scale = 1;                                 // dpi / 96
};

int S(const Wizard& w, int v) { return v * w.scale; }

void destroyStepControls(Wizard& w) {
    for (HWND control : w.stepControls) {
        if (control) ::DestroyWindow(control);
    }
    w.stepControls.clear();
}

HWND addControl(Wizard& w, HWND control, HFONT font) {
    if (font) ::SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    w.stepControls.push_back(control);
    return control;
}

constexpr int kMargin = 24;
constexpr int kContentTop = 108;
constexpr int kBottomRow = 56;

// ---------------------------------------------------------------------------
// Step UIs
// ---------------------------------------------------------------------------
void buildStepHotkeys(Wizard& w) {
    const int width = w.scale * 560;
    int y = S(w, kContentTop + 8);
    HWND section = addControl(
        w, ::CreateWindowExW(0, L"STATIC", tw("onboarding.feature.shortcuts.title", L"全局快捷键").c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin), y,
                             width - S(w, 2 * kMargin), S(w, 22), w.hwnd, nullptr, nullptr,
                             nullptr),
        w.fontBold);
    (void)section;
    y += S(w, 32);
    for (const core::HotkeyBinding& binding : w.hotkeys) {
        HWND label = addControl(
            w, ::CreateWindowExW(0, L"STATIC",
                                 tw(labelKeyForAction(binding.actionName),
                                    labelFallbackForAction(binding.actionName)).c_str(),
                                 WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin + 8), y,
                                 S(w, 220), S(w, 22), w.hwnd, nullptr, nullptr, nullptr),
            w.font);
        (void)label;
        HWND combo = addControl(
            w, ::CreateWindowExW(0, L"STATIC",
                                 (formatCombo(binding.vk, binding.modifiers) +
                                  (binding.enabled ? L"" : L"  (off)")).c_str(),
                                 WS_CHILD | WS_VISIBLE | SS_RIGHT,
                                 width - S(w, kMargin + 8) - S(w, 220), y, S(w, 220),
                                 S(w, 22), w.hwnd, nullptr, nullptr, nullptr),
            w.font);
        (void)combo;
        y += S(w, 30);
    }
    y += S(w, 6);
    addControl(
        w, ::CreateWindowExW(0, L"STATIC", tw("win.onboarding.hotkeyHint", L"可在 设置→快捷键 修改这些热键").c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin + 8), y,
                             width - S(w, 2 * kMargin + 16), S(w, 22), w.hwnd, nullptr,
                             nullptr, nullptr),
        w.font);
}

void buildStepPrivacy(Wizard& w) {
    const int width = w.scale * 560;
    int y = S(w, kContentTop + 8);
    addControl(
        w, ::CreateWindowExW(0, L"STATIC", tw("win.onboarding.step2.title", L"隐私与网络").c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin), y,
                             width - S(w, 2 * kMargin), S(w, 22), w.hwnd, nullptr, nullptr,
                             nullptr),
        w.fontBold);
    y += S(w, 30);
    addControl(
        w, ::CreateWindowExW(0, L"STATIC",
                             tw("win.onboarding.step2.hint", L"当前启用的翻译引擎及联网情况").c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin + 8), y,
                             width - S(w, 2 * kMargin + 16), S(w, 22), w.hwnd, nullptr,
                             nullptr, nullptr),
        w.font);
    y += S(w, 34);
    if (w.engines.empty()) {
        addControl(
            w, ::CreateWindowExW(0, L"STATIC",
                                 tw("win.onboarding.privacy.noEngines", L"尚未启用任何翻译引擎").c_str(),
                                 WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin + 8), y,
                                 width - S(w, 2 * kMargin + 16), S(w, 22), w.hwnd, nullptr,
                                 nullptr, nullptr),
            w.font);
        return;
    }
    for (const core::OnboardingEngineRow& row : w.engines) {
        const bool cloud = core::onboardingEngineSendsToCloud(row);
        std::wstring note;
        if (cloud) {
            const std::string host = core::onboardingUrlHost(row.baseURL);
            note = host.empty()
                       ? tw("win.onboarding.privacy.cloudDefault",
                            L"云端引擎 · 文本将发送到默认端点")
                       : core::loc::formatArgsWide(
                             tw("win.onboarding.privacy.cloud", L"云端引擎 · 文本将发送到 %@"),
                             {core::loc::utf8ToWide(host)});
        } else {
            note = tw("win.onboarding.privacy.local", L"本地引擎 · 不出网");
        }
        const std::wstring name = core::loc::utf8ToWide(row.engineId);
        addControl(
            w, ::CreateWindowExW(0, L"STATIC", name.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 S(w, kMargin + 8), y, S(w, 150), S(w, 22), w.hwnd, nullptr,
                                 nullptr, nullptr),
            w.fontBold);
        addControl(
            w, ::CreateWindowExW(0, L"STATIC", note.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 S(w, kMargin + 8) + S(w, 160), y,
                                 width - S(w, 2 * kMargin + 16) - S(w, 160), S(w, 22),
                                 w.hwnd, nullptr, nullptr, nullptr),
            w.font);
        y += S(w, 30);
    }
}

void buildStepChain(Wizard& w) {
    const int width = w.scale * 560;
    int y = S(w, kContentTop + 8);
    addControl(
        w, ::CreateWindowExW(0, L"STATIC",
                             tw("win.onboarding.step3.title", L"默认翻译链").c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin), y,
                             width - S(w, 2 * kMargin), S(w, 22), w.hwnd, nullptr, nullptr,
                             nullptr),
        w.fontBold);
    y += S(w, 30);
    addControl(
        w, ::CreateWindowExW(0, L"STATIC",
                             tw("win.onboarding.step3.hint", L"选择截图翻译的默认引擎，稍后可在 设置→引擎 修改").c_str(),
                             WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin + 8), y,
                             width - S(w, 2 * kMargin + 16), S(w, 22), w.hwnd, nullptr,
                             nullptr, nullptr),
        w.font);
    y += S(w, 34);

    struct Card {
        core::OnboardingChainChoice choice;
        const char* titleKey;
        const wchar_t* titleFallback;
        const char* hintKey;
        const wchar_t* hintFallback;
    };
    const Card cards[] = {
        {core::OnboardingChainChoice::MtranLocal, "win.onboarding.chain.mtran.title",
         L"MTranServer（本地）", "win.onboarding.chain.mtran.hint",
         L"需在本机 8989 端口运行 MTranServer；文本不出网"},
        {core::OnboardingChainChoice::OllamaLocal, "win.onboarding.chain.ollama.title",
         L"Ollama（本地）", "win.onboarding.chain.ollama.hint",
         L"本机 Ollama 服务（默认端口 11434）；文本不出网"},
        {core::OnboardingChainChoice::CloudOpenAI, "win.onboarding.chain.cloud.title",
         L"云引擎（OpenAI 兼容）", "win.onboarding.chain.cloud.hint",
         L"翻译质量更好；需在 设置→引擎 配置 API Key，文本会发送到云端"},
    };
    DWORD groupFlag = WS_GROUP;  // first radio starts the group
    for (const Card& card : cards) {
        HWND radio = ::CreateWindowExW(
            0, L"BUTTON", tw(card.titleKey, card.titleFallback).c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | groupFlag,
            S(w, kMargin + 4), y, width - S(w, 2 * kMargin + 8), S(w, 24), w.hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(100 +
                static_cast<int>(card.choice))), nullptr, nullptr);
        addControl(w, radio, w.fontBold);
        groupFlag = 0;
        y += S(w, 26);
        addControl(
            w, ::CreateWindowExW(0, L"STATIC", tw(card.hintKey, card.hintFallback).c_str(),
                                 WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin + 24), y,
                                 width - S(w, 2 * kMargin + 32), S(w, 42), w.hwnd, nullptr,
                                 nullptr, nullptr),
            w.font);
        y += S(w, 46);
    }
    // Reflect an existing selection after the rebuild.
    if (w.flow.choice) {
        ::CheckRadioButton(
            w.hwnd, 100, 102,
            100 + static_cast<int>(*w.flow.choice));
    }
}

void rebuildStep(Wizard& w) {
    destroyStepControls(w);
    switch (w.flow.step) {
        case 0: buildStepHotkeys(w); break;
        case 1: buildStepPrivacy(w); break;
        default: buildStepChain(w); break;
    }
    // Chrome text depends on the step (continue vs finish + choice gating).
    HWND back = ::GetDlgItem(w.hwnd, 200);
    HWND skip = ::GetDlgItem(w.hwnd, 201);
    HWND cont = ::GetDlgItem(w.hwnd, 202);
    ::SetWindowTextW(cont, w.flow.step == w.flow.kStepCount - 1
                                ? tw("onboarding.complete.start", L"开始使用 TransFrame").c_str()
                                : tw("onboarding.continue", L"继续").c_str());
    ::EnableWindow(back, w.flow.canBack());
    ::EnableWindow(cont, w.flow.canContinue());
    ::ShowWindow(skip, w.flow.step == w.flow.kStepCount - 1 ? SW_HIDE : SW_SHOW);
}

// ---------------------------------------------------------------------------
// Window proc + modal loop
// ---------------------------------------------------------------------------
LRESULT CALLBACK onboardingWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Wizard* w = reinterpret_cast<Wizard*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    switch (msg) {
        case WM_COMMAND: {
            if (!w) break;
            const int id = LOWORD(wp);
            if (HIWORD(wp) == BN_CLICKED) {
                if (id >= 100 && id <= 102) {
                    w->flow.choice = static_cast<core::OnboardingChainChoice>(id - 100);
                    ::EnableWindow(::GetDlgItem(hwnd, 202), w->flow.canContinue());
                    return 0;
                }
                switch (id) {
                    case 200:  // back
                        w->flow.back();
                        rebuildStep(*w);
                        return 0;
                    case 201:  // skip — marker only, never writes engines.json
                        w->flow.choice.reset();
                        w->running = false;
                        w->finished = true;
                        return 0;
                    case 202:  // continue / finish
                        if (!w->flow.canContinue()) return 0;
                        if (w->flow.step == w->flow.kStepCount - 1) {
                            w->running = false;
                            w->finished = true;
                        } else {
                            w->flow.next();
                            rebuildStep(*w);
                        }
                        return 0;
                    default:
                        break;
                }
            }
            break;
        }
        case WM_CLOSE:
            if (w) {
                w->flow.choice.reset();  // closing == skip (marker only)
                w->running = false;
                w->finished = true;
            }
            return 0;
        case WM_DESTROY:
            return 0;
        default:
            break;
    }
    return ::DefWindowProcW(hwnd, msg, wp, lp);
}

struct ClassRegistration {
    ATOM atom = 0;
    ClassRegistration() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = onboardingWndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
        wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        wc.lpszClassName = kWndClass;
        atom = ::RegisterClassExW(&wc);
    }
};

}  // namespace

OnboardingOutcome ShowOnboardingWizard(void* owner, const std::string& settingsJsonUtf8,
                                       const std::string& enginesJsonUtf8) {
    OnboardingOutcome outcome;
    static const ClassRegistration registration;
    if (!registration.atom) return outcome;

    Wizard w;
    w.hotkeys = core::resolveOnboardingHotkeys(settingsJsonUtf8);
    for (const core::OnboardingEngineRow& row : core::parseOnboardingEngines(enginesJsonUtf8)) {
        if (row.enabled) w.engines.push_back(row);
    }

    // DPI-scaled 560x500 client, centered on the owner (or the work area).
    const UINT dpi = ::GetDpiForSystem();
    w.scale = dpi > 0 ? static_cast<int>(dpi / 96) : 1;
    if (w.scale < 1) w.scale = 1;
    const int cw = S(w, 560), ch = S(w, 500);
    RECT place{};  // owner may be a hidden message window: center on monitor
    if (owner && ::GetWindowRect(static_cast<HWND>(owner), &place)) {
        place = {place.left, place.top, place.left + cw, place.top + ch};
    } else {
        if (!::GetWindowRect(::GetDesktopWindow(), &place)) place = {0, 0, cw, ch};
        place = {(place.right - cw) / 2, (place.bottom - ch) / 2,
                 (place.right + cw) / 2, (place.bottom + ch) / 2};
    }
    AdjustWindowRect(&place, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, FALSE);
    w.hwnd = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, kWndClass,
        tw("onboarding.window.title", L"欢迎使用 TransFrame").c_str(),
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU, place.left, place.top,
        place.right - place.left, place.bottom - place.top,
        static_cast<HWND>(owner), nullptr, nullptr, nullptr);
    if (!w.hwnd) return outcome;
    ::SetWindowLongPtrW(w.hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&w));
    outcome.shown = true;

    w.font = ::CreateFontW(-S(w, 14), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    w.fontTitle = ::CreateFontW(-S(w, 22), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    w.fontBold = ::CreateFontW(-S(w, 14), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");

    const int width = S(w, 560);
    // Chrome: welcome title + message (steps 0) / step-neutral heading.
    HWND title = ::CreateWindowExW(0, L"STATIC",
                                   tw("onboarding.welcome.title", L"欢迎使用 TransFrame").c_str(),
                                   WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin), S(w, 22),
                                   width - S(w, 2 * kMargin), S(w, 34), w.hwnd, nullptr,
                                   nullptr, nullptr);
    HWND message = ::CreateWindowExW(
        0, L"STATIC", tw("onboarding.welcome.message",
                          L"让我们为您设置屏幕截图和翻译功能。只需一分钟即可完成。").c_str(),
        WS_CHILD | WS_VISIBLE | SS_LEFT, S(w, kMargin), S(w, 60), width - S(w, 2 * kMargin),
        S(w, 44), w.hwnd, nullptr, nullptr, nullptr);
    ::SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(w.fontTitle), TRUE);
    ::SendMessageW(message, WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);

    // Bottom buttons: 200 back / 201 skip / 202 continue.
    const int btnW = S(w, 120), btnH = S(w, 32), btnY = S(w, 500 - kBottomRow);
    HWND back = ::CreateWindowExW(0, L"BUTTON", tw("onboarding.back", L"上一步").c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, width - S(w, kMargin) - S(w, 3 * 130),
                                  btnY, btnW, btnH, w.hwnd, reinterpret_cast<HMENU>(200),
                                  nullptr, nullptr);
    HWND cont = ::CreateWindowExW(0, L"BUTTON", tw("onboarding.continue", L"继续").c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                  width - S(w, kMargin) - btnW, btnY, btnW, btnH, w.hwnd,
                                  reinterpret_cast<HMENU>(202), nullptr, nullptr);
    HWND skip = ::CreateWindowExW(0, L"BUTTON", tw("win.onboarding.skip", L"跳过").c_str(),
                                  WS_CHILD | WS_VISIBLE | WS_TABSTOP, S(w, kMargin), btnY,
                                  btnW, btnH, w.hwnd, reinterpret_cast<HMENU>(201), nullptr,
                                  nullptr);
    for (HWND btn : {back, cont, skip}) {
        ::SendMessageW(btn, WM_SETFONT, reinterpret_cast<WPARAM>(w.font), TRUE);
    }

    rebuildStep(w);
    ::ShowWindow(w.hwnd, SW_SHOW);
    ::UpdateWindow(w.hwnd);

    MSG msg{};
    while (w.running && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(w.hwnd, &msg)) {  // TAB/arrows between controls
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    outcome.finished = w.finished;
    outcome.choice = w.finished ? w.flow.finish() : std::nullopt;
    destroyStepControls(w);
    if (w.fontTitle) ::DeleteObject(w.fontTitle);
    if (w.fontBold) ::DeleteObject(w.fontBold);
    if (w.font) ::DeleteObject(w.font);
    ::DestroyWindow(w.hwnd);
    return outcome;
}

}  // namespace tf
