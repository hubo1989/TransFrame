// WinUI settings runs on one dedicated STA for the lifetime of the process.
#include "SettingsWindowWinUI.hpp"
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <microsoft.ui.xaml.window.h>
#include <MddBootstrap.h>
#include <appmodel.h>
#include <mutex>
#include <thread>

namespace tf::ui {
namespace {
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Media;
using namespace winrt::Microsoft::UI::Dispatching;

std::mutex g_mutex;
bool g_starting = false;
DispatcherQueue g_queue{nullptr};
Window g_window{nullptr}; // Accessed only on the settings STA.

void logFailure(const wchar_t* message) {
    ::OutputDebugStringW(message);
    wchar_t path[MAX_PATH]{};
    const DWORD n = ::GetTempPathW(MAX_PATH, path);
    if (!n || n >= MAX_PATH - 30) return;
    wcscat_s(path, L"TransFrameShellInit.log");
    HANDLE f = ::CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD written;
    ::WriteFile(f, message, static_cast<DWORD>(wcslen(message) * sizeof(wchar_t)), &written, nullptr);
    ::CloseHandle(f);
}

void reportFailure(HWND owner, const winrt::hresult_error& error) {
    wchar_t text[1024]{};
    _snwprintf_s(text, _countof(text), _TRUNCATE,
                 L"无法打开设置（0x%08lX）。\n%s\n",
                 static_cast<unsigned long>(error.code().value), error.message().c_str());
    logFailure(text);
    ::MessageBoxW(owner, text, L"TransFrame 设置", MB_OK | MB_ICONERROR);
}

void activateWindow() {
    auto native = g_window.as<IWindowNative>();
    HWND hwnd{};
    winrt::check_hresult(native->get_WindowHandle(&hwnd));
    ::ShowWindow(hwnd, ::IsIconic(hwnd) ? SW_RESTORE : SW_SHOW);
    g_window.Activate();
    ::SetForegroundWindow(hwnd);
}

struct SettingsApplication : ApplicationT<SettingsApplication, winrt::Microsoft::UI::Xaml::Markup::IXamlMetadataProvider> {
    winrt::Microsoft::UI::Xaml::XamlTypeInfo::XamlControlsXamlMetaDataProvider metadata;
    auto GetXamlType(winrt::Windows::UI::Xaml::Interop::TypeName const& type) { return metadata.GetXamlType(type); }
    auto GetXamlType(winrt::hstring const& name) { return metadata.GetXamlType(name); }
    auto GetXmlnsDefinitions() { return metadata.GetXmlnsDefinitions(); }
    void OnLaunched(LaunchActivatedEventArgs const&) {
        try {
        // WinUI controls require both Application and their theme resources.
        Resources().MergedDictionaries().Append(XamlControlsResources());
        NavigationView nav;
        nav.PaneDisplayMode(NavigationViewPaneDisplayMode::Left);
        nav.IsSettingsVisible(false);
        nav.IsBackButtonVisible(NavigationViewBackButtonVisible::Collapsed);
        for (const auto* label : {L"通用", L"引擎", L"Prompt", L"语言", L"快捷键", L"高级"}) {
            NavigationViewItem item;
            item.Content(winrt::box_value(label));
            nav.MenuItems().Append(item);
        }
        nav.SelectedItem(nav.MenuItems().GetAt(0));
        TextBlock placeholder;
        placeholder.Text(L"页签内容由 windows/UI/<Page>/ 提供");
        placeholder.Margin(Thickness{24, 24, 24, 24});
        nav.Content(placeholder);
        g_window = Window();
        g_window.Title(L"TransFrame 设置");
        g_window.Content(nav);
        g_window.SystemBackdrop(MicaBackdrop());
        // Keep the XAML Application alive: WinUI cannot be restarted after
        // its last window closes. Subsequent tray clicks reuse this window.
        g_window.Closed([](auto const&, WindowEventArgs const& args) {
            args.Handled(true);
            HWND hwnd{};
            winrt::check_hresult(g_window.as<IWindowNative>()->get_WindowHandle(&hwnd));
            ::ShowWindow(hwnd, SW_HIDE);
        });
        activateWindow();
        std::lock_guard lock(g_mutex);
        g_queue = DispatcherQueue::GetForCurrentThread();
        g_starting = false;
        } catch (const winrt::hresult_error& error) {
            reportFailure(nullptr, error);
            Application::Current().Exit();
        }
    }
};

void runSettings(HWND owner) noexcept {
    bool bootstrapped = false;
    bool apartment = false;
    try {
        UINT32 length = 0;
        const LONG identity = ::GetCurrentPackageFullName(&length, nullptr);
        if (identity == APPMODEL_ERROR_NO_PACKAGE) {
            // Packaged apps resolve WASDK through their manifest dependency;
            // the dynamic-dependency bootstrapper is for unpackaged apps only.
            PACKAGE_VERSION minimum{};
            winrt::check_hresult(::MddBootstrapInitialize(0x00010006, L"", minimum));
            bootstrapped = true;
        } else if (identity != ERROR_INSUFFICIENT_BUFFER) {
            winrt::check_hresult(HRESULT_FROM_WIN32(identity));
        }
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        apartment = true;
        Application::Start([](auto const&) {
            winrt::make<SettingsApplication>();
        });
    } catch (const winrt::hresult_error& error) {
        reportFailure(owner, error);
    } catch (...) {
        reportFailure(owner, winrt::hresult_error(E_FAIL, L"设置窗口初始化失败"));
    }
    {
        std::lock_guard lock(g_mutex);
        g_queue = nullptr;
        g_starting = false;
    }
    g_window = nullptr;
    if (apartment) winrt::uninit_apartment();
    if (bootstrapped) ::MddBootstrapShutdown();
}
} // namespace

void OpenSettingsWindow(HWND owner) {
    DispatcherQueue queue{nullptr};
    {
        std::lock_guard lock(g_mutex);
        if (g_starting) return;
        queue = g_queue;
        if (!queue) g_starting = true;
    }
    if (queue) {
        if (!queue.TryEnqueue([owner] {
            try { activateWindow(); }
            catch (const winrt::hresult_error& error) { reportFailure(owner, error); }
        })) {
            reportFailure(owner, winrt::hresult_error(E_FAIL, L"设置窗口消息队列已停止"));
        }
        return;
    }
    try {
        // A thread-pool coroutine may already be MTA. Never switch it to STA.
        std::thread(runSettings, owner).detach();
    } catch (...) {
        { std::lock_guard lock(g_mutex); g_starting = false; }
        reportFailure(owner, winrt::hresult_error(E_FAIL, L"无法创建设置窗口线程"));
    }
}
} // namespace tf::ui
