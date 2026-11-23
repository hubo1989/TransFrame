# TransFrame Windows — Win10 21H2 降级矩阵（PRD §6.13 / 附录 C）

静态审计基线：`45a474f`。目标为 Windows 10 21H2（build 19044）及 Win11。
审计 `windows/**` 的运行时 API 防护；**未在 Win10 虚拟机运行**。本机 Win11 构建、
Catch2 和冒烟验证不能代替 Win10 实测，下表列出后续验证方法。

版本说明：原注释把 WGC 和去黄框 API 的 contract 混在一起。微软文档中
`CreateFreeThreaded` 是 **Win10 1809 / contract 7**，`IsBorderRequired` 是
**SDK 20348 / contract 12**，并非 contract 15。Windows 10 21H2 的桌面基线仍为
19041 系列（contract 10），不可按产品年份推导 contract。产品策略仍按 PRD：
Win10 保留黄框，Win11 才尝试去黄框；实际属性存在性和系统授权另行判断。

## 降级矩阵

| API / 能力 | Win10 21H2 行为 | 防护 / 回退位置（file:line） | 验证方式 |
|---|---|---|---|
| `GraphicsCaptureSession.IsBorderRequired`（contract 12） | 不调用去框属性，保留系统黄框 | `windows/NativeShell/src/WGCCaptureEngineFull.cpp:105`：Win11 策略 + `IsPropertyPresent` + try/catch；setter 失败不影响捕获 | `CaptureServiceTests.cpp:81` 注入 Win10/Win11 与 contract 10/11/12/14/15/16；Win10 VM 确认黄框 |
| WGC / `Direct3D11CaptureFramePool.CreateFreeThreaded`（contract 7） | WGC 可用；策略禁用或创建失败才降级 | `windows/NativeShell/src/WGCCaptureEngineFull.cpp:144`、`WGCCaptureEngine.cpp:37`：`IsMethodPresent(CreateFreeThreaded)` + `GraphicsCaptureSession::IsSupported()` + try/catch；捕获异常经引擎链落到 DXGI/GDI | Catch2 引擎链用例、本机 capture 冒烟；Win10 VM 分别验证 WGC 可用与被禁用 |
| WinUI 3 `SystemBackdrop` / `MicaBackdrop` | 明确使用不透明纯色，随明暗主题变化 | `windows/UI/SettingsShell/SettingsShell.cpp:287`：build ≥ 22000 + `MicaController::IsSupported()` + try/catch；`:295` 为纯色画刷及 `ActualThemeChanged` 回退 | `PlatformFeaturesTests.cpp:24` 等版本边界；settings_tabs 冒烟；Win10 VM 切换明暗主题 |
| 原生 DWM `DWMSBT_*` / `DWMWA_SYSTEMBACKDROP_TYPE` | 没有直接调用 | 全仓 Windows C++ 搜索无调用；Mica 统一经过上述 WinUI 路径 | 搜索这些符号，确认没有绕过运行时防护的新调用 |
| Snap Layouts | Win10 没有 Win11 Snap 布局弹层；普通窗口仍可用 | `windows/UI/SettingsShell/SettingsShell.cpp:98`：标准 WinUI Window 激活；无 Snap 专用 API、`HTMAXBUTTON` 或自定义 `WM_NCHITTEST` 调用 | 搜索专用 API；Win10 VM 打开、移动、最大化/恢复设置窗口，不应依赖 Snap 弹层 |
| App Notifications | 当前实现统一使用托盘气球，无 Win11 通知 API 激活 | `windows/App/AppMain.cpp:1583`：注入 `appnotify::setBackend` 托盘后端；`windows/App/UpdateService.cpp:38`：通知失败回退 `MessageBoxW` | locale_onboard 的 N1/N2；免装检查更新应提示安装 MSIX。打包通知中心后端尚未实现 |
| WinUI 3 / Windows App SDK 1.6 | WASDK 支持 Win10 1809+；项目清单下限 19041 | `windows/App/Package.appxmanifest:25`；`windows/scripts/build_msix.py` 检查 EXE、bootstrap DLL 的 PE 架构及入包结果 | x64/ARM64 构建与打包自检；Win10 VM 另验 WASDK 运行库安装和启动 |
| `DwmGetWindowAttribute`：窗口边界、cloaked 状态 | Win10 支持 | `windows/NativeShell/src/WindowDetectorWin32.cpp:55`、`:61` 检查 HRESULT，失败回退 `GetWindowRect` | `WindowDetectorTests.cpp`；Win10 VM 枚举窗口 |
| DXGI Desktop Duplication、GDI 捕获、剪贴板、置顶窗口 | Win10 支持 | `windows/NativeShell/src/CaptureService.cpp` 引擎回退链；Win32 路径未发现 Win11 专用调用 | CaptureService/WindowDetector Catch2，capture/overlay/pinned_history 冒烟 |

## 本轮修复

1. **WGC 可用性门槛过高**：原有两处使用 contract 12 判断整个 WGC，误排除 Win10。
   改为探测实际使用的 `CreateFreeThreaded` 方法及 `IsSupported()`；去黄框条件不再参与
   WGC 整体可用性判断。
2. **Mica 未显式降级**：加入版本与控制器支持检查；不支持或异常时设置不透明画刷，
   并响应明暗主题变化。`RequestedTheme(Default)` 本身不等价于设置纯色背景。
3. **版本读取受 manifest 影响**：通过动态解析 `RtlGetVersion` 获取真实 build，失败安全
   返回未知；避免 `GetVersionExW` 在未声明兼容性的测试宿主中报告旧系统。
4. **策略测试跳过断言**：用注入的 19044/21999/22000/22621 与 contract 边界测试，
   不再按运行测试的主机版本决定是否执行断言。去黄框的实际 setter 同时保留属性检查。

## 静态审计的限制

- 未运行 Win10 VM；黄框、主题、Snap、MSIX 安装及更新链路仍需在那里验收。
- `AppNotificationManager` 打包后端在 `AppMain.cpp:1581` 仍为 TODO；本轮未把托盘气球
  描述为已接入 Windows 通知中心。WASDK 的通知支持也不能简单等同于必须有 MSIX 身份。
- `IsBorderRequired(false)` 只是一项请求；系统授权、能力声明和其它捕获进程也会影响
  黄框是否消失。设置失败或被忽略时保留黄框，不承诺 Win11 上一定去框。
- 未修改用户指定的 Core/Tests 并行开发文件或 `TransFrame/` macOS 源。

## API 依据

- [CreateFreeThreaded：Win10 1809、contract 7](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.direct3d11captureframepool.createfreethreaded)
- [IsBorderRequired：SDK 20348、contract 12、授权要求](https://learn.microsoft.com/en-us/uwp/api/windows.graphics.capture.graphicscapturesession.isborderrequired)
