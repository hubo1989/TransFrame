// tf/AppNotifications.hpp — app notification seam (FR-50.7).
//
// Unpackaged processes cannot use WinRT AppNotifications (they need MSIX
// identity), so the FR-50.7 channel is the tray balloon
// (Shell_NotifyIcon NIF_INFO, zero dependencies). The host installs a
// backend (AppMain wires it to TrayIconService::showBalloon); callers use
// notify() from anywhere and fall back to their own surface when it
// returns false (no tray installed).
//
// TODO(FR-50.7 packaged): once the app ships as MSIX, register
// AppNotificationManager with the package identity and post real action-
// center toasts here; the notify() seam stays the same.
#ifndef TF_APP_NOTIFICATIONS_HPP
#define TF_APP_NOTIFICATIONS_HPP

#include <functional>
#include <string>

namespace tf::appnotify {

enum class Type { Info = 0, Success, Warning, Error };

/// Backend contract: present (title, body, severity); returns whether the
/// notification was actually shown.
using Backend = std::function<bool(const std::wstring&, const std::wstring&, Type)>;

/// Install/replace the backend (called from the app shell at startup; the
/// function itself must be thread-safe or confined like the tray).
void setBackend(Backend backend);
void clearBackend();
bool hasBackend();

/// Post a notification. Returns false when no backend is installed or the
/// backend declined (e.g. tray missing) — callers then fall back to
/// MessageBox as before.
bool notify(const std::wstring& title, const std::wstring& body, Type type);

}  // namespace tf::appnotify

#endif  // TF_APP_NOTIFICATIONS_HPP
