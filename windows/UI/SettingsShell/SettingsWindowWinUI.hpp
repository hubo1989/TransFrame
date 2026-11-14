// SettingsWindowWinUI.hpp — WinUI 3 settings host bridge (C++/WinRT).
#ifndef TF_SETTINGS_WINDOW_WINUI_HPP
#define TF_SETTINGS_WINDOW_WINUI_HPP

#include <windows.h>

namespace tf::ui {

/// Opens (or foregrounds) the single WinUI 3 settings window.
/// Safe to call from the Win32 tray command; no XAML types cross this boundary.
void OpenSettingsWindow(HWND owner);

/// Smoke-test report (RunSettingsTabsSmoke). detail is UTF-16, NUL-terminated.
struct SettingsTabsSmokeReport {
    bool ok;
    int tabsChecked;
    wchar_t detail[256];
};

/// Test-only hook for the manual smoke harness (windows/Tests/manual):
/// blocks until the settings window is up (bounded by timeoutMs), then on the
/// settings DispatcherQueue switches each of the six tabs and verifies every
/// tab renders real page content (non-placeholder control subtree).
/// Thread-safe; returns false (with reason in out->detail) on timeout/failure.
bool RunSettingsTabsSmoke(int timeoutMs, SettingsTabsSmokeReport* out);

/// Test-only: switch the settings window to tab `index` (0..5) and return
/// once the selection change has been dispatched on the settings STA. The
/// caller owns pacing between switches (settle/render wait, capture, ...).
/// DispatcherQueue FIFO guarantees consecutive calls run in order.
bool SwitchSettingsTab(int index, int timeoutMs);

}  // namespace tf::ui

#endif  // TF_SETTINGS_WINDOW_WINUI_HPP
