# TransFrame Windows — Win10 21H2 降级矩阵（PRD §6.13 / 附录 C）

> 静态审计基线：分支基点 `45a474f`。范围：`windows/**`（含 NativeShell、UI、App、
> Tests）。目标支持矩阵下界 **Windows 10 21H2 (build 19044, UniversalApiContract 14)**，
> 上界 Win11 23H2。方法：全仓库搜索 Win11-only API 的使用点，逐处核对运行时防护
> （ApiInformation / try-catch / OS 版本判断），**未在 Win10 虚拟机实测**——每项的
> "验证方式"列给出可执行的替代验证手段。
>
> 策略层（可测的版本判断）：`tf::supportsMicaBackdrop()` /
> `tf::supportsBorderlessCapture()` / `tf::isWindows11OrLater()` 定义于
> `windows/NativeShell/include/tf/PlatformFeatures.hpp`，Catch2 用例在
> `windows/NativeShell/tests/PlatformFeaturesTests.cpp`。

## 降级矩阵

| # | API / 能力 | 引入版本 | Win10 21H2 行为 | 防护位置（file:line） | 验证方式 |
|---|---|---|---|---|---|
| 1 | `GraphicsCaptureSession.IsBorderRequired` | Win11 21H2 (contract 15) | 属性不存在，系统保留黄框（产品接受，PRD §6.13） | `windows/NativeShell/src/WGCCaptureEngineFull.cpp:106` — `ApiInformation::IsPropertyPresent` + try/catch；策略回退 `windows/NativeShell/src/CaptureService.cpp` `wgcSupportsBorderless()`（含 OS build 双保险） | `windows/NativeShell/tests/CaptureServiceTests.cpp:83-87`（contract 12/14/15/16 边界 Catch2）；真机验证：Win10 抓屏可见黄框 |
| 2 | WGC 整体可用性（`Direct3D11CaptureFramePool`） | Win10 1803 (contract 12) | 可用（这是下限之上的能力） | `windows/NativeShell/src/WGCCaptureEngine.cpp:37` `probeWgcAvailability()` — `IsApiContractPresent(…, 12)` + try/catch；失败回退 DXGI/GDI 引擎链 | Catch2 引擎链选择用例（`CaptureServiceTests.cpp`）；Win10 禁用图形捕获策略后应自动落到 DXGI |
| 3 | WinUI 3 `SystemBackdrop` / `MicaBackdrop` | Win11 21H2 (build 22000) | WinUI 1.6 在 Win10 上静默 no-op，不崩溃但窗口无 Mica（此前发现：无防护，已修） | `windows/UI/SettingsShell/SettingsShell.cpp:287-302` — `tf::supportsMicaBackdrop()`（OS build ≥ 22000）+ try/catch，Win10 分支显式设 `ElementTheme::Default` 走纯色 | Catch2：`PlatformFeaturesTests.cpp`（19044/21999/22000/22621 边界）；Win10 真机打开设置窗口确认无白框 |
| 4 | App Notifications（`AppNotificationManager`，需要打包身份） | Win10 1903+（但需 MSIX identity） | 免安装运行无打包身份 → 不能用；打包运行可用但当前后端仍是托盘气球 | `windows/App/AppMain.cpp:1583` — `appnotify::setBackend` 注入托盘气球后端，打包/免装统一走 NIIF_USER；`windows/App/UpdateService.cpp:37` 气泡失败回退 `MessageBoxW` | `windows/Tests/manual/smoke_locale_onboard.cpp` N1 用例（seam 路由 Catch2 已绿）；Win10 免装跑"检查更新→已是最新"应弹 MessageBox |
| 5 | WinUI 3 / Windows App SDK 1.6 整体 | Win10 1809+ | 受支持（WASDK 1.6 支持 Win10 1809+） | 打包清单 `MinVersion=10.0.19041.0`（`windows/App/Package.appxmanifest:25`）；bootstrapper DLL 按架构随包分发（`TransFrame.App.vcxproj:82`） | MSIX 打包自检 `check_stage_content()` 强制 bootstrap DLL 入包（`windows/scripts/test_build_msix.py` StageContentTests） |
| 6 | `DwmGetWindowAttribute`（`DWMWA_EXTENDED_FRAME_BOUNDS` / `DWMWA_CLOAKED`） | Win10 全版本 | 可用 | `windows/NativeShell/src/WindowDetectorWin32.cpp:55,61` — 调用处 `SUCCEEDED`/`FAILED` 判断，失败安全回退 `GetWindowRect` | `windows/NativeShell/tests/WindowDetectorTests.cpp`；Win10 真机窗口枚举正确性 |
| 7 | DXGI Desktop Duplication（回退引擎） | Win10 全版本 | 可用 | 引擎链自动回退（WGC 探测失败 → DXGI → GDI），`windows/NativeShell/src/CaptureService.cpp` | 禁用 WGC（组策略）后抓屏仍工作 |
| 8 | 捕获会话/剪辑板/置顶等纯 Win32 路径（`SetWinEventHook`、`AddClipboardFormatListener`、`SetWindowPos` 等） | Win10 全版本 | 可用 | 无 Win11-only API；逐文件审计确认（见"审计面"） | 六冒烟 `windows/Tests/manual/build_smoke_*.cmd` |

## 审计面（已搜索确认的面）

- `IsBorderRequired` / `DWMSBT` / `Mica` / `SystemBackdrop` / `AppNotification` /
  `ApiInformation` / `DwmSetWindowAttribute` / `Windows.Graphics.Capture` —— 命中点全部
  落入上表 1-6。
- `TransFrame/`（macOS 源）不在 Windows 支持矩阵内，未审计也不需审计。
- WinUI 3 页面（六页签）使用的基础控件均为 WASDK 1.6 的 Win10 兼容集，无
  `MapControl` 等 Win11-only 控件。

## 本轮修复（静态审计发现）

1. **SettingsShell Mica 无防护**（#3）：`g_window.SystemBackdrop(MicaBackdrop())`
   原为裸调用。WinUI 1.6 在 Win10 上虽不致崩溃，但 backdrop 无效会让设置窗口呈现
   空白底；已加 `tf::supportsMicaBackdrop()` 版本门 + try/catch，Win10 分支设置
   纯色主题兜底。
2. **borderless 探测无双保险**（#1）：`wgcSupportsBorderless()` 原本只看传入的
   contract 版本号；已追加 `tf::supportsBorderlessCapture()`（OS build ≥ 22000）
   与之一致才放行，防止 contract shim 谎报。

## 遗留风险（静态审计无法闭环的点）

- `AppNotificationManager` 打包后端仍是 TODO（`AppMain.cpp:1581` 注释）：打包身份下
  气泡目前也走托盘后端，功能正确但非系统通知中心样式。非 Win10 专属问题。
- Win10 21H2 真机/虚拟机实机回归未做（任务边界：静态审计）。上线前建议在
  Win10 21H2 VM 跑一遍六冒烟 + `.appinstaller` 安装链路。
