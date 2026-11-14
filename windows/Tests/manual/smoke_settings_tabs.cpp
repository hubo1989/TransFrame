// smoke_settings_tabs.cpp — FR-35 六页签冒烟：经 OpenSettingsWindow（托盘
// 同路径）拉起设置窗口，等 STA 就绪后通过 RunSettingsTabsSmoke 在设置
// DispatcherQueue 上逐页签切换（通用/引擎/Prompt/语言/快捷键/高级），
// 断言每页渲染出真实控件子树（≥10 个逻辑子节点）且无异常，打印 PASS。
//
// Build: windows\Tests\manual\build_smoke_settings_tabs.cmd（/MD，
// WinUI3Shell.lib + NativeShell/Core 静态库 + bootstrap 导入库）。
// 运行前需 Microsoft.WindowsAppRuntime.Bootstrap.dll 在 exe 同目录
//（build 脚本负责从 nupkg 复制）。
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
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // 管道下也要实时看到进度
    std::printf("launching settings window...\n");
    tf::ui::OpenSettingsWindow(nullptr);

    // 1) 窗口可见性（冷启 = bootstrap + XAML init）。
    HWND hwnd = nullptr;
    for (int i = 0; i < 60 && !hwnd; ++i) {
        ::Sleep(250);
        FindCtx ctx;
        if (!::EnumWindows(enumProc, reinterpret_cast<LPARAM>(&ctx))) {
            hwnd = ctx.found;
        }
    }
    if (!hwnd) {
        std::printf("FAIL: no visible settings window after 15s; "
                    "check %%TEMP%%\\TransFrameShellInit.log\n");
        return 1;
    }
    std::printf("settings window up (hwnd=%p)\n", static_cast<void*>(hwnd));

    // 2) 在设置 STA 上逐页签切换并检查内容（RunSettingsTabsSmoke 内部
    //    自行轮询 DispatcherQueue 就绪；超时上限 20s 覆盖冷启余量）。
    tf::ui::SettingsTabsSmokeReport report{};
    const bool ok = tf::ui::RunSettingsTabsSmoke(20000, &report);
    std::printf("tabs checked: %d/6\n", report.tabsChecked);
    if (report.detail[0] != L'\0') {
        std::printf("detail: %ls\n", report.detail);
    }
    if (!ok) {
        std::printf("FAIL: settings tabs smoke failed\n");
        return 1;
    }
    std::printf("PASS: 6 tabs render non-empty content with no exceptions\n");
    ::Sleep(1200);  // 收尾前留一拍给 STA（进程退出即回收线程）
    return 0;
}
