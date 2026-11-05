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
    co_await winrt::resume_background();
    try {
        // Framework-dependent WASDK: resolve the 1.6 runtime before any
        // Microsoft.UI.Xaml activation, otherwise WinUI 3 classes throw
        // (the tray command then looks like a no-op).
        constexpr UINT32 kMajorMinor = 0x00010006;  // Windows App SDK 1.6
        PACKAGE_VERSION minVersion{};
        minVersion.Version = (static_cast<UINT64>(6000)) |
                             (static_cast<UINT64>(519) << 16) |
                             (static_cast<UINT64>(329) << 32);
        const HRESULT boot = ::MddBootstrapInitialize(kMajorMinor, L"", minVersion);
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
        auto controller = DispatcherQueueController::CreateOnCurrentThread();

        auto nav = NavigationView();
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
        window.Activate();

        controller.DispatcherQueue().RunEventLoop();
    } catch (...) {
        // A XAML failure must never wedge the Win32 tray thread.
        ::OutputDebugStringW(L"TransFrame: WinUI 3 settings host failed\n");
    }
    std::lock_guard<std::mutex> lock(g_windowMutex);
    g_launching = false;
}

}  // namespace

void OpenSettingsWindow(HWND /*owner*/) {
    std::lock_guard<std::mutex> lock(g_windowMutex);
    if (g_launching) return;
    g_launching = true;
    LaunchAsync();
}

}  // namespace tf::ui
