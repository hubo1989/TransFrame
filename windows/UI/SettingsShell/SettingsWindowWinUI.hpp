// SettingsWindowWinUI.hpp — WinUI 3 settings host bridge (C++/WinRT).
#ifndef TF_SETTINGS_WINDOW_WINUI_HPP
#define TF_SETTINGS_WINDOW_WINUI_HPP

#include <windows.h>

namespace tf::ui {

/// Opens (or foregrounds) the single WinUI 3 settings window.
/// Safe to call from the Win32 tray command; no XAML types cross this boundary.
void OpenSettingsWindow(HWND owner);

}  // namespace tf::ui

#endif  // TF_SETTINGS_WINDOW_WINUI_HPP
