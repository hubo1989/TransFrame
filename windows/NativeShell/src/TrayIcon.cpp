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

HMENU buildHMenu(const TrayMenuModel& model) {
    HMENU menu = ::CreatePopupMenu();
    // The pinned submenu owns IDs kPinnedMenuIdBase+1..; its parent entry
    // consumes one flat index (unselectable, MF_POPUP) so the flat item IDs
    // stay aligned with trayMenuItemForId.
    HMENU pinnedSub = nullptr;
    if (!model.pinnedItems.empty()) {
        pinnedSub = ::CreatePopupMenu();
        unsigned subIndex = 1;
        for (const auto& sub : model.pinnedItems) {
            const UINT flags = MF_STRING | (sub.enabled ? MF_ENABLED : MF_GRAYED);
            ::AppendMenuW(pinnedSub, flags, kPinnedMenuIdBase + subIndex,
                          sub.label.c_str());
            if (sub.separatorAfter) ::AppendMenuW(pinnedSub, MF_SEPARATOR, 0, nullptr);
            ++subIndex;
        }
    }
    UINT index = 1;
    for (const auto& item : model.items) {
        UINT flags = MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED);
        if (item.command == TrayCommand::PinnedManager && pinnedSub) {
            // Popup entry: never returns from TrackPopupMenu itself, but the
            // flat index is still consumed so trayMenuItemForId stays aligned.
            ::AppendMenuW(menu, flags | MF_POPUP, reinterpret_cast<UINT_PTR>(pinnedSub),
                          item.label.c_str());
            pinnedSub = nullptr;  // ownership passes to `menu`
        } else {
            ::AppendMenuW(menu, flags, index, item.label.c_str());
        }
        ++index;
        if (item.separatorAfter) {
            ::AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        }
    }
    if (pinnedSub) ::DestroyMenu(pinnedSub);  // no parent consumed it (disabled)
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
    nid.hIcon = ::LoadIconW(::GetModuleHandleW(nullptr), MAKEINTRESOURCEW(101));
    if (!nid.hIcon) return false;
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
    nid.hWnd = static_cast<HWND>(callbackWindow_);
    nid.uID = kTrayIconId;
    ::Shell_NotifyIconW(NIM_DELETE, &nid);
    installed_ = false;
}

void TrayIconService::setRecentShots(std::vector<RecentShot> shots) {
    recent_ = std::move(shots);
}

void TrayIconService::setPinnedWindows(std::vector<PinnedSnapshot> pinned) {
    pinned_ = std::move(pinned);
}

void TrayIconService::showMenuAtCursor() {
    const TrayMenuModel model = buildTrayMenuModel(recent_, pinned_);
    const auto owner = static_cast<HWND>(callbackWindow_);
    if (!owner) return;
    HMENU menu = buildHMenu(model);
    if (!menu) return;
    POINT pt;
    ::GetCursorPos(&pt);
    // TPM_RIGHTBUTTON + return-on-click; SetForegroundWindow is required by
    // the Shell_NotifyIcon contract so the menu dismisses when clicking away.
    ::SetForegroundWindow(owner);
    const BOOL picked = ::TrackPopupMenuEx(
        menu, TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x, pt.y, owner, nullptr);
    ::DestroyMenu(menu);
    ::PostMessageW(owner, WM_NULL, 0, 0);
    const unsigned id = static_cast<unsigned>(picked);
    if (const auto* sub = trayMenuPinnedItemForId(model.pinnedItems, id)) {
        dispatch(sub->command, sub->pinnedIndex);
        return;
    }
    if (const auto* item = trayMenuItemForId(model.items, id)) {
        dispatch(item->command, item->recentIndex);
    }
}


void TrayIconService::dispatch(TrayCommand command, int payloadIndex) {
    if (handler_) handler_(command, payloadIndex);
}

} // namespace tf
