// smoke_settings_winui.cpp — manual smoke for the WinUI 3 settings shell.
// Calls tf::ui::OpenSettingsWindow directly (same path the tray uses),
// waits, then reports whether a top-level window titled "TransFrame 设置"
// exists. Prints PASS/FAIL; exits (process teardown closes the STA thread).
//
// Build (from a VS x64 Native Tools prompt, repo root):
//   cl /std:c++20 /EHsc /utf-8 smoke_settings_winui.cpp ^
//      /I windows\UI\SettingsShell ^
//      /I "%USERPROFILE%\.nuget\packages\microsoft.windowsappsdk\1.6.250205002\include" ^
//      /link windows\build\x64\Release\WinUI3Shell\WinUI3Shell.lib ^
//      "%USERPROFILE%\.nuget\packages\microsoft.windowsappsdk\1.6.250205002\lib\win10-x64\Microsoft.WindowsAppRuntime.Bootstrap.lib" ^
//      windowsapp.lib user32.lib
#include "SettingsWindowWinUI.hpp"

#include <windows.h>
#include <cstdio>

namespace {
struct FindCtx {
    HWND found = nullptr;
};

BOOL CALLBACK enumProc(HWND hwnd, LPARAM lp) {
    wchar_t title[128]{};
    if (::GetWindowTextW(hwnd, title, 128) > 0 &&
        wcscmp(title, L"TransFrame 设置") == 0 && ::IsWindowVisible(hwnd)) {
        reinterpret_cast<FindCtx*>(lp)->found = hwnd;
        return FALSE;
    }
    return TRUE;
}
}  // namespace

int main() {
    std::printf("launching settings window...\n");
    tf::ui::OpenSettingsWindow(nullptr);
    // First launch pays bootstrap + XAML init; give it room.
    for (int i = 0; i < 40; ++i) {
        ::Sleep(250);
        FindCtx ctx;
        if (!::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx)) && ctx.found) {
            std::printf("PASS: settings window visible after ~%dms (hwnd=%p)\n",
                        (i + 1) * 250, static_cast<void*>(ctx.found));
            ::Sleep(1500);  // let it settle for a visual check
            return 0;
        }
    }
    std::printf("FAIL: no visible settings window after 10s; check %%TEMP%%\\TransFrameShellInit.log\n");
    return 1;
}
