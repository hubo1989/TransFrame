// SettingsShell.cpp — WinUI 3 C++/WinRT main window host (navigation shell).
// Built entirely in code: no XAML compiler/PRI dependency (the Build Tools
// probe showed PRI generation is unavailable there). NavigationView and
// MicaBackdrop are native WinUI 3 controls, so Fluent 2 styling is preserved.
#include "SettingsWindowWinUI.hpp"

#include <windows.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <MddBootstrap.h>
#include <mutex>

namespace tf::ui {

namespace {
void shellLog(const char* step, long v) {
    char path[MAX_PATH];
    if (::GetTempPathA(MAX_PATH, path) == 0) return;
    lstrcatA(path, "TransFrameShellInit.log");
    HANDLE f = ::CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    char line[160];
    const int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "%s v=%ld\n", step, v);
    DWORD written = 0;
    if (len > 0) ::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
    ::CloseHandle(f);
}
}  // namespace

namespace {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Dispatching;

std::mutex g_windowMutex;
bool g_launching = false;

struct PageDef {
    const wchar_t* label;
    const wchar_t* tag;
};

constexpr PageDef kPages[] = {
    {L"通用",   L"general"},
    {L"引擎",   L"engines"},
    {L"Prompt", L"prompts"},
    {L"语言",   L"languages"},
    {L"快捷键", L"shortcuts"},
    {L"高级",   L"advanced"},
};

winrt::fire_and_forget LaunchAsync() {
    shellLog("launch-enter", 0);
    co_await winrt::resume_background();
    shellLog("resumed-bg", 0);
    try {
        // Framework-dependent WASDK: resolve the 1.6 runtime before any
        // Microsoft.UI.Xaml activation, otherwise WinUI 3 classes throw
        // (the tray command then looks like a no-op).
        constexpr UINT32 kMajorMinor = 0x00010006;  // Windows App SDK 1.6
        PACKAGE_VERSION minVersion{};
        minVersion.Version = (static_cast<UINT64>(6000)) |
                             (static_cast<UINT64>(519) << 16) |
                             (static_cast<UINT64>(329) << 32);
        shellLog("before-bootstrap", 0);
        const HRESULT boot = ::MddBootstrapInitialize(kMajorMinor, L"", minVersion);
        shellLog("bootstrap-hr", static_cast<long>(boot));
        if (FAILED(boot) && boot != HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS)) {
            wchar_t msg[96];
            _snwprintf_s(msg, _countof(msg), _TRUNCATE,
                         L"TransFrame: MddBootstrapInitialize failed hr=0x%08lX\n",
                         static_cast<unsigned long>(boot));
            ::OutputDebugStringW(msg);
            std::lock_guard<std::mutex> lock(g_windowMutex);
            g_launching = false;
            co_return;
        }
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        shellLog("apartment-ok", 0);
        auto controller = DispatcherQueueController::CreateOnCurrentThread();
        shellLog("dispatcher-ok", 0);

        shellLog("first-control", 0);
        auto nav = NavigationView();
        shellLog("nav-created", 0);
        nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        nav.IsSettingsVisible(false);
        nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        for (const auto& def : kPages) {
            NavigationViewItem item;
            item.Content(winrt::box_value(def.label));
            item.Tag(winrt::box_value(def.tag));
            nav.MenuItems().Append(item);
        }
        nav.SelectedItem(nav.MenuItems().GetAt(0));

        auto placeholder = TextBlock();
        placeholder.Text(L"页签内容由 windows/UI/<Page>/ 提供");
        placeholder.Margin(Thickness{24, 24, 24, 24});
        nav.Content(placeholder);

        auto window = Window();
        window.Title(L"TransFrame 设置");
        window.Content(nav);
        window.SystemBackdrop(MicaBackdrop());
        shellLog("backdrop-ok", 0);
        window.Activate();
        shellLog("window-activated", 0);

        controller.DispatcherQueue().RunEventLoop();
    } catch (const winrt::hresult_error& e) {
        char msg[200] = {};
        WideCharToMultiByte(CP_UTF8, 0, e.message().c_str(), -1, msg, sizeof(msg), nullptr, nullptr);
        char line[280];
        const int len = _snprintf_s(line, sizeof(line), _TRUNCATE, "EXCEPTION hr=0x%08lX %s\n",
                                    static_cast<unsigned long>(e.code().value), msg);
        if (len > 0) {
            char path[MAX_PATH];
            if (::GetTempPathA(MAX_PATH, path) > 0) {
                lstrcatA(path, "TransFrameShellInit.log");
                HANDLE f = ::CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                                         OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                if (f != INVALID_HANDLE_VALUE) {
                    DWORD written = 0;
                    ::WriteFile(f, line, static_cast<DWORD>(len), &written, nullptr);
                    ::CloseHandle(f);
                }
            }
        }
    } catch (...) {
        shellLog("EXCEPTION-unknown", 0);
    }
    std::lock_guard<std::mutex> lock(g_windowMutex);
    g_launching = false;
}

}  // namespace

void OpenSettingsWindow(HWND /*owner*/) {
    shellLog("open-called", 0);
    std::lock_guard<std::mutex> lock(g_windowMutex);
    if (g_launching) { shellLog("open-rejected-busy", 0); return; }
    g_launching = true;
    LaunchAsync();
}

}  // namespace tf::ui
