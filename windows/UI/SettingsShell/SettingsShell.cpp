// WinUI settings runs on one dedicated STA for the lifetime of the process.
//
// FR-35 六页签宿主：NavigationView（通用/引擎/Prompt/语言/快捷键/高级），
// SelectionChanged 切换页内容；页签懒构建、单例缓存（STA-only）。
// 页面全部代码构建（windows/UI/<Page>/），无 XAML 编译依赖。
#include "SettingsWindowWinUI.hpp"
#include "SettingsUiKit.hpp"

#include "AdvancedPage.h"
#include "EnginesPage.h"
#include "GeneralPage.h"
#include "LanguagePage.h"
#include "PromptsPage.h"
#include "ShortcutsPage.h"

#include "tf/Localization.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.XamlTypeInfo.h>
#include <winrt/Microsoft.UI.Windowing.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <microsoft.ui.xaml.window.h>
#include <MddBootstrap.h>
#include <appmodel.h>
#include <array>
#include <memory>
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

// ---- 页签宿主（以下全部 STA-only）----
constexpr int kTabCount = 6;
// [关键] 页签缓存必须显式 nullptr：UserControl 的默认构造 = 经激活工厂
// 创建真实对象，静态初始化期既无 apartment 也未 bootstrap，激活抛出的
// hresult_error 逃出静态初始化 → std::terminate（进程 fail-fast 0xC0000409，
// 冒烟/主程序均无法进入 main）。显式 nullptr 只填空包装。
static_assert(kTabCount == 6, "g_pages 初始化列表需随页签数同步");
std::array<UserControl, kTabCount> g_pages{nullptr, nullptr, nullptr,
                                           nullptr, nullptr, nullptr};
// 有磁盘重载语义的页签（OnShown 在每次激活时从 engines.json 重载）。
// composable 页（UserControlT 派生）不能经 get_self 取实现（投影对象是
// inner，不是 outer 的 produce），以 make_self 持实现本体，com_ptr 常驻进程。
winrt::com_ptr<GeneralPage> g_generalImpl;
GeneralPage* g_generalPage = nullptr;
winrt::com_ptr<PromptsPage> g_promptsImpl;
PromptsPage* g_promptsPage = nullptr;
winrt::com_ptr<LanguagePage> g_languageImpl;
LanguagePage* g_languagePage = nullptr;
winrt::com_ptr<AdvancedPage> g_advancedImpl;
AdvancedPage* g_advancedPage = nullptr;

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

/// 逻辑子树计数（ContentControl.Content / Panel.Children / Border.Child 递归）
/// —— 不依赖布局完成的确定性检查（VisualTreeHelper 需要渲染后才有子级）。
int CountLogicalTree(winrt::Windows::Foundation::IInspectable const& node) {
    int count = 0;
    if (auto panel = node.try_as<Panel>()) {
        for (auto const& child : panel.Children()) {
            count += 1 + CountLogicalTree(child);
        }
    } else if (auto border = node.try_as<Border>()) {
        if (border.Child()) count += 1 + CountLogicalTree(border.Child());
    } else if (auto content = node.try_as<ContentControl>()) {
        if (content.Content()) count += 1 + CountLogicalTree(content.Content());
    } else if (auto scroller = node.try_as<ScrollViewer>()) {
        if (scroller.Content()) count += 1 + CountLogicalTree(scroller.Content());
    } else if (auto user = node.try_as<UserControl>()) {
        if (user.Content()) count += 1 + CountLogicalTree(user.Content());
    }
    return count;
}

UserControl BuildTab(int index) {
    switch (index) {
        case 0: {
            g_generalImpl = winrt::make_self<GeneralPage>();
            g_generalPage = g_generalImpl.get();
            return g_generalImpl.as<UserControl>();
        }
        case 1:
            return winrt::make<EnginesPage>();
        case 2: {
            g_promptsImpl = winrt::make_self<PromptsPage>();
            g_promptsPage = g_promptsImpl.get();
            return g_promptsImpl.as<UserControl>();
        }
        case 3: {
            g_languageImpl = winrt::make_self<LanguagePage>();
            g_languagePage = g_languageImpl.get();
            return g_languageImpl.as<UserControl>();
        }
        case 4:
            return winrt::make<ShortcutsPage>();
        case 5: {
            g_advancedImpl = winrt::make_self<AdvancedPage>();
            g_advancedPage = g_advancedImpl.get();
            return g_advancedImpl.as<UserControl>();
        }
    }
    throw winrt::hresult_error(E_FAIL, L"未知页签");
}

/// FR-37: the six tab labels (Localization keys; zh fallbacks keep the
/// pre-localization wording when no tables are loaded).
const char* const kTabKeys[kTabCount] = {
    "settings.section.general", "settings.section.engines", "settings.section.prompts",
    "settings.section.languages", "settings.section.shortcuts",
    "win.settings.section.advanced",
};
const wchar_t* const kTabFallbacks[kTabCount] = {
    L"通用", L"引擎", L"Prompt", L"语言", L"快捷键", L"高级",
};

int g_currentTab = 0;

/// Refresh the chrome labels (nav items + window title) from the tables.
void RefreshChrome(NavigationView const& nav) {
    auto items = nav.MenuItems();
    for (std::uint32_t i = 0; i < items.Size() && i < kTabCount; ++i) {
        if (auto item = items.GetAt(i).try_as<NavigationViewItem>()) {
            item.Content(winrt::box_value(
                kit::Lx(kTabKeys[i], kTabFallbacks[i])));
        }
    }
    if (g_window) {
        g_window.Title(kit::Lx("settings.window.title", L"TransFrame 设置"));
    }
}

/// Drop the lazily-cached pages so the next ShowTab rebuilds them with the
/// new language. STA-only.
void ResetPageCache() {
    for (auto& page : g_pages) page = nullptr;
    g_generalImpl = nullptr; g_generalPage = nullptr;
    g_promptsImpl = nullptr; g_promptsPage = nullptr;
    g_languageImpl = nullptr; g_languagePage = nullptr;
    g_advancedImpl = nullptr; g_advancedPage = nullptr;
}

void ShowTab(NavigationView const& nav, int index) {
    if (!g_pages[index]) g_pages[index] = BuildTab(index);
    nav.Content(g_pages[index]);
    g_currentTab = index;
    // 每次激活重载磁盘（其它页签可能改过 engines.json / 兼容实例表）。
    switch (index) {
        case 0:
            if (g_generalPage) g_generalPage->OnShown();
            break;
        case 2:
            if (g_promptsPage) g_promptsPage->OnShown();
            break;
        case 3:
            if (g_languagePage) g_languagePage->OnShown();
            break;
        case 5:
            if (g_advancedPage) g_advancedPage->OnShown();
            break;
        default:
            break;
    }
}

int IndexOfTag(winrt::Windows::Foundation::IInspectable const& tag) {
    auto prop = tag.try_as<winrt::Windows::Foundation::IPropertyValue>();
    if (prop && prop.Type() == winrt::Windows::Foundation::PropertyType::Int32) {
        return prop.GetInt32();
    }
    return -1;
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
        int index = 0;
        for (const wchar_t* label : {L"通用", L"引擎", L"Prompt", L"语言", L"快捷键", L"高级"}) {
            NavigationViewItem item;
            item.Content(winrt::box_value(label));  // replaced by RefreshChrome below
            item.Tag(winrt::box_value(index++));
            nav.MenuItems().Append(item);
        }
        nav.SelectionChanged([](NavigationView const& sender,
                                NavigationViewSelectionChangedEventArgs const& args) {
            try {
                auto item = args.SelectedItem().try_as<NavigationViewItem>();
                if (!item) return;
                const int i = IndexOfTag(item.Tag());
                if (i >= 0 && i < kTabCount) ShowTab(sender, i);
            } catch (const winrt::hresult_error& error) {
                reportFailure(nullptr, error);
            }
        });
        nav.SelectedItem(nav.MenuItems().GetAt(0));
        ShowTab(nav, 0);  // 默认页签 = 通用（spec §0：每次打开默认 general）
        RefreshChrome(nav);  // FR-37 labels from the Localization tables
        g_window = Window();
        g_window.Title(kit::Lx("settings.window.title", L"TransFrame 设置"));
        // FR-37: language switches (any thread) rebuild the chrome + pages on
        // this STA. TryEnqueue defers past the current event handler, so a
        // switch fired inside a page's SelectionChanged never destroys the
        // control whose callback is still running.
        core::loc::Localization::instance().addListener([] {
            DispatcherQueue queue{nullptr};
            {
                std::lock_guard lock(g_mutex);
                queue = g_queue;
            }
            if (!queue) return;
            queue.TryEnqueue([] {
                try {
                    if (!g_window) return;
                    auto nav = g_window.Content().as<NavigationView>();
                    ResetPageCache();
                    RefreshChrome(nav);
                    ShowTab(nav, g_currentTab);
                    nav.SelectedItem(nav.MenuItems().GetAt(
                        static_cast<std::uint32_t>(g_currentTab)));
                } catch (const winrt::hresult_error& error) {
                    reportFailure(nullptr, error);
                } catch (...) {
                    reportFailure(nullptr, winrt::hresult_error(E_FAIL, L"语言切换刷新失败"));
                }
            });
        });
        g_window.Content(nav);
        g_window.SystemBackdrop(MicaBackdrop());
        // 初始尺寸 800×600（spec §0；AppWindow.Resize 是物理像素，按 DPI 换算）。
        try {
            HWND hwnd{};
            winrt::check_hresult(g_window.as<IWindowNative>()->get_WindowHandle(&hwnd));
            const UINT dpi = ::GetDpiForWindow(hwnd);
            const double scale = dpi > 0 ? dpi / 96.0 : 1.0;
            g_window.AppWindow().Resize({static_cast<std::int32_t>(800 * scale),
                                         static_cast<std::int32_t>(600 * scale)});
        } catch (...) {
            // 尺寸非关键路径：失败保持默认窗口大小。
        }
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
        } catch (...) {
            // 非 hresult 异常同样收敛（防经 XAML 边界扩散为 terminate）。
            reportFailure(nullptr, winrt::hresult_error(E_FAIL, L"设置窗口初始化失败"));
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
}  // namespace

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

bool RunSettingsTabsSmoke(int timeoutMs, SettingsTabsSmokeReport* out) {
    SettingsTabsSmokeReport report{};
    report.detail[0] = L'\0';

    // 1) 等设置 STA 就绪（冷启约 2.5s）。
    DispatcherQueue queue{nullptr};
    for (int waited = 0; waited < timeoutMs && !queue; waited += 50) {
        {
            std::lock_guard lock(g_mutex);
            queue = g_queue;
        }
        if (!queue) ::Sleep(50);
    }
    if (!queue) {
        wcscpy_s(report.detail, L"settings dispatcher not ready in time");
        *out = report;
        return false;
    }

    // 2) 在设置 STA 上逐页签切换并检查内容（经 DispatcherQueue 投递）。
    //    report 走共享指针：超时返回后 STA 回调仍可能迟到，不能悬挂在栈上。
    HANDLE done = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!done) {
        wcscpy_s(report.detail, L"CreateEvent failed");
        *out = report;
        return false;
    }
    auto sharedReport = std::make_shared<SettingsTabsSmokeReport>();
    sharedReport->ok = false;
    sharedReport->tabsChecked = 0;
    bool enqueued = false;
    try {
        enqueued = queue.TryEnqueue([sharedReport, done]() {
            SettingsTabsSmokeReport& report = *sharedReport;
            try {
                auto nav = g_window.Content().as<NavigationView>();
                auto items = nav.MenuItems();
                if (items.Size() != static_cast<std::uint32_t>(kTabCount)) {
                    _snwprintf_s(report.detail, _countof(report.detail), _TRUNCATE,
                                 L"expected %d tabs, found %u", kTabCount, items.Size());
                } else {
                    for (std::uint32_t i = 0; i < items.Size(); ++i) {
                        nav.SelectedItem(items.GetAt(i));
                        auto content = nav.Content();
                        auto page = content.try_as<UserControl>();
                        if (!page) {
                            _snwprintf_s(report.detail, _countof(report.detail), _TRUNCATE,
                                         L"tab %u content is not a page control", i);
                            break;
                        }
                        const int count = CountLogicalTree(page);
                        if (count < 10) {
                            _snwprintf_s(report.detail, _countof(report.detail), _TRUNCATE,
                                         L"tab %u renders only %d controls", i, count);
                            break;
                        }
                        report.tabsChecked = static_cast<int>(i + 1);
                    }
                    report.ok = report.tabsChecked == kTabCount;
                }
            } catch (const winrt::hresult_error& error) {
                _snwprintf_s(report.detail, _countof(report.detail), _TRUNCATE, L"STA: %s",
                             error.message().c_str());
            } catch (...) {
                wcscpy_s(report.detail, L"STA: unknown exception");
            }
            ::SetEvent(done);
        });
    } catch (const winrt::hresult_error& error) {
        _snwprintf_s(report.detail, _countof(report.detail), _TRUNCATE, L"enqueue: %s",
                     error.message().c_str());
    }
    if (!enqueued) {
        if (wcslen(report.detail) == 0) wcscpy_s(report.detail, L"TryEnqueue failed");
        ::CloseHandle(done);
        *out = report;
        return false;
    }
    const DWORD wait = ::WaitForSingleObject(done, static_cast<DWORD>(timeoutMs));
    ::CloseHandle(done);
    if (wait != WAIT_OBJECT_0) {
        wcscpy_s(report.detail, L"timeout waiting for tab switch run");
        *out = report;
        return false;
    }
    *out = *sharedReport;
    return sharedReport->ok;
}

}  // namespace tf::ui
