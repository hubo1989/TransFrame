// TrayIcon.cpp — Shell_NotifyIcon plumbing (Windows only).
// The portable menu model lives in src/TrayMenu.cpp (buildTrayMenu).
#include "pch.h"

#include "tf/TrayIcon.hpp"

#include <shellapi.h>

namespace tf {

// ---------------------------------------------------------------------------
// Windows plumbing
// ---------------------------------------------------------------------------

namespace {

constexpr UINT kTrayIconId = 1;
constexpr UINT kTrayCallbackMessage = WM_APP + 0x5446;  // 'TF'

HMENU buildHMenu(const std::vector<TrayMenuItem>& items) {
    HMENU menu = ::CreatePopupMenu();
    UINT index = 0;
    for (const auto& item : items) {
        if (item.command == TrayCommand::None && !item.label.empty()) {
            // (no None entries produced by buildTrayMenu today)
        }
        const UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED);
        ::AppendMenuW(menu, flags, index++, item.label.c_str());
        if (item.separatorAfter) {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
    }
    return menu;
}

} // namespace

TrayIconService::TrayIconService(TrayCommandHandler handler)
    : handler_(std::move(handler)) {}

TrayIconService::~TrayIconService() {
    remove();
}

bool TrayIconService::install() {
    if (installed_) return true;

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    // Callback parent: the App's hidden message window when provided (the
    // reliable route — GetForegroundWindow can be null/destroyed by the time
    // the user clicks the icon).
    nid.hWnd = callbackWindow_ ? static_cast<HWND>(callbackWindow_) : ::GetForegroundWindow();
    nid.uID = kTrayIconId;
    nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = kTrayCallbackMessage;
    nid.hIcon = ::LoadIconW(nullptr, IDI_APPLICATION);  // camera icon: task C asset
    ::wcscpy_s(nid.szTip, L"TransFrame");

    installed_ = ::Shell_NotifyIconW(NIM_ADD, &nid) != FALSE;
    return installed_;
}

void TrayIconService::setCallbackWindow(void* hwnd) {
    const bool reinstall = installed_;
    if (reinstall) remove();
    callbackWindow_ = static_cast<HWND>(hwnd);
    if (reinstall) install();
}

void TrayIconService::remove() {
    if (!installed_) return;
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.uID = kTrayIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    installed_ = false;
}

void TrayIconService::setRecentShots(std::vector<RecentShot> shots) {
    recent_ = std::move(shots);
}

void TrayIconService::showMenuAtCursor() {
    const auto items = buildTrayMenu(recent_);
    HMENU menu = buildHMenu(items);
    POINT pt;
    ::GetCursorPos(&pt);
    // TPM_RIGHTBUTTON + return-on-click; SetForegroundWindow is required by
    // the Shell_NotifyIcon contract so the menu dismisses when clicking away.
    ::SetForegroundWindow(::GetForegroundWindow());
    const BOOL picked = ::TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, nullptr, nullptr);
    ::DestroyMenu(menu);
    if (picked == 0) return;  // dismissed without choice

    const std::size_t idx = static_cast<std::size_t>(picked) - 1;
    if (idx < items.size()) {
        dispatch(items[idx].command, items[idx].recentIndex);
    }
}


void TrayIconService::dispatch(TrayCommand command, int recentIndex) {
    if (handler_) handler_(command, recentIndex);
}

} // namespace tf
