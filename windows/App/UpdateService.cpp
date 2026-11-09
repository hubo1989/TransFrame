#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <appmodel.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Foundation.h>
#include <atomic>
#include <cstdint>
#include <exception>
#include "UpdateService.hpp"

#include "tf/AppNotifications.hpp"   // FR-50.7：气泡通知 seam
#include "tf/Localization.hpp"       // FR-37/38：本地化文案

namespace tf {
namespace {
namespace loc = core::loc;

std::wstring updateTitle() {
    return loc::tw("win.notify.updateTitle", L"TransFrame 更新");
}

/// FR-38：已是最新 → 气泡展示当前包版本（%@）；气泡不可用退回 MessageBox。
void announceUpToDate(const winrt::Windows::ApplicationModel::Package& package) {
    const auto v = package.Id().Version();
    wchar_t ver[64]{};
    // PackageVersion projects plain fields in this C++/WinRT version.
    swprintf(ver, 64, L"%u.%u.%u.%u", v.Major, v.Minor, v.Build, v.Revision);
    const std::wstring body = loc::formatArgsWide(
        loc::tw("win.notify.uptodate", L"当前已是最新版本（%@）"), {ver});
    const std::wstring title = updateTitle();
    if (!appnotify::notify(title, body, appnotify::Type::Info)) {
        ::MessageBoxW(nullptr, body.c_str(), title.c_str(),
                      MB_OK | MB_ICONINFORMATION);
    }
}

std::atomic<bool> checking{false};

winrt::fire_and_forget checkInstalledFeed() {
    // No application objects are captured: quitting while the request is in
    // flight cannot dereference a destroyed AppState or hidden window.
    co_await winrt::resume_background();
    // resume_background lands on a thread-pool thread with no apartment;
    // Package::Current() activation needs one (ignore mismatch — the pool
    // thread model is ours).
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }
    struct Reset { ~Reset() { checking = false; } } reset;
    try {
        using namespace winrt::Windows::ApplicationModel;
        const auto package = Package::Current();
        const auto installer = package.GetAppInstallerInfo();
        if (!installer) {
            ::MessageBoxW(nullptr,
                          loc::tw("win.update.noFeed",
                                  L"此安装未关联自动更新源。请通过发布页提供的 .appinstaller 文件安装，以启用自动更新。")
                              .c_str(),
                          updateTitle().c_str(), MB_OK | MB_ICONINFORMATION);
            co_return;
        }
        const auto result = co_await package.CheckUpdateAvailabilityAsync();
        switch (result.Availability()) {
        case PackageUpdateAvailability::Available:
        case PackageUpdateAvailability::Required:
            if (::MessageBoxW(nullptr,
                              loc::tw("win.update.available",
                                      L"发现新版本。现在打开 Windows 应用安装程序更新？")
                                  .c_str(),
                              updateTitle().c_str(),
                              MB_YESNO | MB_ICONINFORMATION) == IDYES) {
                const auto source = installer.Uri();
                if (source.SchemeName() != L"https") {
                    throw winrt::hresult_error(
                        E_INVALIDARG,
                        loc::tw("win.update.httpsRequired", L"更新源必须使用 HTTPS。"));
                }
                const std::wstring uri = L"ms-appinstaller:?source=" +
                    std::wstring(winrt::Windows::Foundation::Uri::EscapeComponent(source.AbsoluteUri()));
                const auto launched = ::ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                if (reinterpret_cast<std::intptr_t>(launched) <= 32) {
                    throw winrt::hresult_error(
                        E_FAIL, loc::tw("win.update.openFailed",
                                        L"无法打开 Windows 应用安装程序，请检查是否已安装 App Installer。"));
                }
            }
            break;
        case PackageUpdateAvailability::NoUpdates:
            announceUpToDate(package);
            break;
        case PackageUpdateAvailability::Error:
            throw winrt::hresult_error(result.ExtendedError());
        default:
            ::MessageBoxW(nullptr,
                          loc::tw("win.update.unsure", L"暂时无法确认更新，请检查网络后重试。")
                              .c_str(),
                          updateTitle().c_str(), MB_OK | MB_ICONWARNING);
            break;
        }
    } catch (const winrt::hresult_error& error) {
        const std::wstring message = loc::formatArgsWide(
            loc::tw("win.update.checkFailed", L"检查更新失败：%@"),
            {std::wstring(error.message().c_str())});
        ::MessageBoxW(nullptr, message.c_str(), updateTitle().c_str(), MB_OK | MB_ICONWARNING);
    } catch (const std::exception&) {
        ::MessageBoxW(nullptr,
                      loc::tw("win.update.checkFailedGeneric", L"检查更新失败，请稍后重试。")
                          .c_str(),
                      updateTitle().c_str(), MB_OK | MB_ICONWARNING);
    } catch (...) {
        ::MessageBoxW(nullptr,
                      loc::tw("win.update.checkFailedGeneric", L"检查更新失败，请稍后重试。")
                          .c_str(),
                      updateTitle().c_str(), MB_OK | MB_ICONWARNING);
    }
}
}  // namespace

void checkForAppUpdate(void* owner) {
    UINT32 length = 0;
    if (::GetCurrentPackageFullName(&length, nullptr) == APPMODEL_ERROR_NO_PACKAGE) {
        ::MessageBoxW(static_cast<HWND>(owner),
            loc::tw("win.update.unpackaged",
                    L"当前为免安装版，暂无自动更新。请通过发布页提供的 .appinstaller 文件安装 MSIX 版本以启用自动更新。")
                .c_str(),
            updateTitle().c_str(), MB_OK | MB_ICONINFORMATION);
        return;
    }
    if (checking.exchange(true)) return;
    checkInstalledFeed();
}

}  // namespace tf
