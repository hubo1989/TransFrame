// windows/Tests/manual/smoke_locale_onboard.cpp — FR-37/39/50.7 manual smoke
// (PRD stage 5b). Real-run verification of the localization + onboarding +
// notification layer against the shipped tables and Win32 surfaces:
//
//   L1 all 10 shipped tables load from windows/UI/Strings (fallback
//      App/Assets/Strings), identical key sets, >= 200 keys per language
//   L2 a hardcoded 33-key contract list resolves in every language; spot
//      values pin zh-Hans / en / ja parity (macOS key names)
//   L3 fallback chain + live language switch through the Localization
//      singleton (listener fires, t() output changes)
//   O1 system-language probe: GetUserDefaultUILanguage -> locale name ->
//      appLanguageFromLocale mapping (prints the resolved UI language)
//   O2 onboarding 3-step state machine walk + engines.json default-chain
//      merge round-trip + done-flag round-trip (pure logic)
//   N1 appnotify seam: setBackend -> notify() routes to the backend;
//      absent backend returns false
//   N2 tray balloon: hidden window + TrayIconService install ->
//      appnotify backend bridged to showBalloon (balloon visible = PASS;
//      prints N/A, non-failing, when the tray is unavailable)
//
// Build (from windows/, after building TransFrame.sln Release x64) — or run
// Tests\manual\build_smoke_locale_onboard.bat, which wraps this:
//
//   cl /nologo /std:c++20 /EHsc /utf-8 /W4 /MD ^
//      /I NativeShell\include /I Core /I Core\include /I Core\vendor ^
//      Tests\manual\smoke_locale_onboard.cpp /Fe:build\smoke_locale_onboard.exe ^
//      /link build\x64\Release\TransFrame.NativeShell\TransFrame.NativeShell.lib ^
//            build\x64\Release\TransFrame.Core\TransFrame.Core.lib ^
//            shell32.lib user32.lib advapi32.lib ole32.lib
//   build\smoke_locale_onboard.exe
//
// Exit code != 0 on any FAIL (N/A checks do not fail).

#ifdef _WIN32

#include "tf/AppNotifications.hpp"
#include "tf/Localization.hpp"
#include "tf/Onboarding.hpp"
#include "tf/TrayIcon.hpp"
#include "tf/Win32Locale.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace loc = tf::core::loc;
using tf::core::OnboardingChainChoice;

namespace {

int g_failures = 0;

void check(bool ok, const char* step, const std::string& detail = "") {
    if (ok) {
        std::printf("[PASS] %s %s\n", step, detail.c_str());
    } else {
        ++g_failures;
        std::printf("[FAIL] %s %s\n", step, detail.c_str());
    }
}

std::optional<std::string> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

fs::path findStringsDir() {
    std::error_code ec;
    for (const fs::path& candidate : {
             fs::path("UI") / "Strings",
             fs::path("..") / "UI" / "Strings",          // run from build
             fs::path("..") / ".." / "UI" / "Strings",   // exe-relative
             fs::path("App") / "Assets" / "Strings",
             fs::path("..") / "App" / "Assets" / "Strings",
         }) {
        if (fs::is_directory(candidate, ec)) return candidate;
    }
    return {};
}

/// The 33-key hardcoded contract (L2): every language must carry these.
const char* kContractKeys[] = {
    "settings.section.general", "settings.section.engines",
    "settings.section.prompts", "settings.section.languages",
    "settings.section.shortcuts", "win.settings.section.advanced",
    "settings.window.title", "settings.language", "settings.language.system",
    "win.tray.translateMode", "win.tray.exit", "win.tray.replayOnboarding",
    "menu.capture.full.screen", "win.result.title", "win.result.preview",
    "win.preview.title", "win.preview.dialogTitle", "action.close",
    "action.save", "action.cancel", "action.copy", "action.delete",
    "common.copied", "save.success.title", "save.success.message",
    "onboarding.window.title", "onboarding.welcome.title",
    "onboarding.continue", "onboarding.back", "win.update.unpackaged",
    "win.notify.uptodate", "win.onboarding.applied", "engine.available.title",
};
constexpr int kContractKeyCount = static_cast<int>(sizeof(kContractKeys) /
                                                   sizeof(kContractKeys[0]));

const char* kLanguages[] = {"en", "zh-Hans", "de", "es", "fr",
                            "it", "ja", "ko", "pt", "ru"};

// Smoke window (tray balloon host): invisible top-level, pumps nothing —
// Shell_NotifyIcon only needs a live HWND for callbacks.
constexpr wchar_t kSmokeWndClass[] = L"TransFrameSmokeLocale";
LRESULT CALLBACK smokeWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return ::DefWindowProcW(h, m, w, l);
}

}  // namespace

int main() {
    std::printf("== TransFrame locale/onboarding/notification smoke (stage 5b) ==\n");

    // ---------------- L1: load the 10 shipped tables ----------------
    const fs::path stringsDir = findStringsDir();
    if (stringsDir.empty()) {
        check(false, "L1 strings dir", "UI/Strings not found (run from windows/)");
        std::printf("== smoke complete: %d failure(s) ==\n", g_failures + 1);
        return 1;
    }
    std::printf("      tables: %s\n", stringsDir.string().c_str());

    std::string loadErr;
    bool loaded = loc::Localization::instance().loadDirectory(stringsDir, &loadErr);
    check(loaded, "L1 loadDirectory(en.json required)", loadErr);

    std::optional<std::vector<std::string>> referenceKeys;
    bool identicalKeySets = true;
    std::size_t totalKeys = 0;
    for (const char* lang : kLanguages) {
        const auto text = readFile(stringsDir / (std::string(lang) + ".json"));
        if (!text) { check(false, "L1 file", lang); identicalKeySets = false; continue; }
        std::string err;
        const auto table = loc::StringTable::fromJson(*text, &err);
        if (!err.empty()) { check(false, "L1 parse", std::string(lang) + ": " + err); continue; }
        if (table.size() < 200) {
            check(false, "L1 count floor", std::string(lang) +
                      " has only " + std::to_string(table.size()) + " keys");
        }
        if (!referenceKeys) {
            referenceKeys = table.keys();
            totalKeys = table.size();
        } else if (table.keys() != *referenceKeys) {
            identicalKeySets = false;
        }
    }
    check(identicalKeySets, "L1 identical key sets across 10 languages",
          "keys=" + std::to_string(totalKeys));

    // ---------------- L2: hardcoded key contract + spot values ----------------
    bool contractOk = true;
    for (const char* key : kContractKeys) {
        for (const char* lang : kLanguages) {
            const auto text = readFile(stringsDir / (std::string(lang) + ".json"));
            if (!text) { contractOk = false; break; }
            const auto table = loc::StringTable::fromJson(*text);
            if (!table.contains(key)) {
                contractOk = false;
                std::printf("      missing %s in %s\n", key, lang);
            }
        }
    }
    check(contractOk, "L2 33-key contract present in every language");

    // Spot values: zh-Hans / en / ja parity for a few macOS-reused keys.
    loc::Localization::instance().setLanguage("zh-Hans");
    check(loc::t("win.tray.exit", "?") == "\xE9\x80\x80\xE5\x87\xBA",  // 退出
          "L2 zh-Hans win.tray.exit", loc::t("win.tray.exit", "?"));
    check(loc::t("settings.section.general", "?") == "\xE9\x80\x9A\xE7\x94\xA8",  // 通用
          "L2 zh-Hans settings.section.general", loc::t("settings.section.general", "?"));
    loc::Localization::instance().setLanguage("en");
    check(loc::t("win.tray.exit", "?") == "Exit", "L2 en win.tray.exit",
          loc::t("win.tray.exit", "?"));
    loc::Localization::instance().setLanguage("ja");
    check(loc::t("win.tray.exit", "?") == "\xE7\xB5\x82\xE4\xBA\x86",  // 終了
          "L2 ja win.tray.exit", loc::t("win.tray.exit", "?"));

    // ---------------- L3: fallback chain + live switch ----------------
    loc::Localization::instance().setLanguage("en");
    check(loc::t("definitely.not.a.key", "fb") == "fb",
          "L3 missing key falls to caller default");
    int fired = 0;
    auto token = loc::Localization::instance().addListener([&] { ++fired; });
    loc::Localization::instance().setLanguage("de");
    check(fired == 1, "L3 listener fired on switch");
    const std::string exitDe = loc::t("win.tray.exit", "?");
    check(exitDe != "Exit" && exitDe != "?", "L3 de table serves t()",
          exitDe);
    loc::Localization::instance().removeListener(token);

    // ---------------- O1: system language probe ----------------
    const std::string sysLocale = tf::win32SystemUiLocaleName();
    const auto resolvedSys = tf::core::appLanguageFromLocale(sysLocale);
    const std::string resolvedName =
        resolvedSys ? tf::core::toRaw(*resolvedSys) : std::string("(unmapped -> en)");
    check(true, "O1 system UI locale probe",
          "locale=" + (sysLocale.empty() ? std::string("(empty)") : sysLocale) +
              " -> " + resolvedName);

    // ---------------- O2: onboarding flow + merges (pure logic) ----------------
    {
        tf::core::OnboardingFlow flow;
        flow.next();  // step 0 -> 1
        flow.next();  // step 1 -> 2
        const bool gated = !flow.canContinue();  // no card yet
        flow.choice = OnboardingChainChoice::OllamaLocal;
        const bool finishable = flow.finish().has_value();
        check(flow.step == 2 && gated && finishable,
              "O2 flow machine (3 steps, choice gates finish)");

        const std::string engines = R"({"appLanguage":"system","engines":[{"engine":"mtran","enabled":true}]})";
        const std::string merged =
            tf::core::mergeOnboardingChain(engines, OnboardingChainChoice::OllamaLocal);
        const bool chainOk = merged.find("\"engine\": \"ollama\"") != std::string::npos &&
                             merged.find("\"globalChain\"") != std::string::npos &&
                             merged.find("\"appLanguage\"") != std::string::npos;
        check(chainOk, "O2 default-chain merge preserves keys + writes chain");

        const std::string flagged =
            tf::core::serializeOnboardingDoneFlag(R"({"keep": 1})");
        check(tf::core::parseOnboardingDone(flagged) &&
                  flagged.find("\"keep\"") != std::string::npos,
              "O2 done-flag round-trip");
    }

    // ---------------- N1: appnotify seam ----------------
    {
        int backendCalls = 0;
        tf::appnotify::setBackend(
            [&](const std::wstring&, const std::wstring&, tf::appnotify::Type) {
                ++backendCalls;
                return true;
            });
        const bool routed = tf::appnotify::notify(L"s", L"b", tf::appnotify::Type::Info);
        tf::appnotify::clearBackend();
        const bool noBackend = !tf::appnotify::hasBackend() &&
                               !tf::appnotify::notify(L"s", L"b", tf::appnotify::Type::Info);
        check(routed && backendCalls == 1 && noBackend,
              "N1 appnotify seam (backend routing + absent-backend false)");
    }

    // ---------------- N2: tray balloon (visible = PASS; N/A when no tray) ----
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = &smokeWndProc;
        wc.hInstance = ::GetModuleHandleW(nullptr);
        wc.lpszClassName = kSmokeWndClass;
        ::RegisterClassExW(&wc);
        HWND host = ::CreateWindowExW(WS_EX_TOOLWINDOW, kSmokeWndClass,
                                      L"TransFrame smoke", WS_POPUP, 0, 0, 0, 0,
                                      nullptr, nullptr, wc.hInstance, nullptr);
        tf::TrayIconService tray([](tf::TrayCommand, int) {});
        tray.setCallbackWindow(host);
        if (!tray.install() || !tray.isInstalled()) {
            std::printf("[N/A ] N2 tray balloon (tray unavailable in this session)\n");
        } else {
            tf::appnotify::setBackend(
                [&](const std::wstring& title, const std::wstring& body,
                    tf::appnotify::Type type) {
                    const auto icon =
                        type == tf::appnotify::Type::Error
                            ? tf::BalloonIcon::Error
                            : (type == tf::appnotify::Type::Warning
                                   ? tf::BalloonIcon::Warning
                                   : tf::BalloonIcon::Info);
                    return tray.showBalloon(title, body, icon);
                });
            const bool shown = tf::appnotify::notify(
                L"TransFrame smoke",
                tf::core::loc::utf8ToWide(
                    tf::core::loc::t("win.notify.uptodate", "smoke")),
                tf::appnotify::Type::Info);
            check(shown, "N2 tray balloon shown (visible in the tray area)");
            tf::appnotify::clearBackend();
        }
        tray.remove();
        if (host) ::DestroyWindow(host);
    }

    std::printf("== smoke complete: %d failure(s) ==\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}

#else
int main() { return 0; }  // Windows-only smoke
#endif
