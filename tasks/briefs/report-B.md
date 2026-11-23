# Report B — Windows 捕获壳层（Win-Capture-Shell）

- 分支：`hubo/win-capture-shell`（基于 `hubo/feat-windows` @ 9e9f9b0）
- 交付目录：`windows/NativeShell/`（+ `windows/docs/windows/*.md`）
- 验证：**Catch2 v3.7.1 · 60 用例 / 235 断言 全绿**（macOS arm64 clang 21 实测运行）
- Windows 侧编译：**NOT RUN**（本机 macOS；复现命令见 §6）

---

## 1. 实现清单

### 1.1 头文件（`windows/NativeShell/include/tf/`）

| 文件 | 内容 |
|---|---|
| `Geo.hpp` | Point/Size/Rect（int64 物理像素，半开区间，纯头文件） |
| `CoreTypes.hpp` | 对任务 A 的最小依赖自声明：PixelBuffer（BGRA8+stride+DPI）、DisplayInfo（物理 frame+DPI+scale）、Screenshot（物理像素+DPI 元数据+CaptureTrigger）、HotkeyAction ×5 |
| `DisplayMonitor.hpp` | DisplayMonitorService + DisplayFreshness 三态（Cached/AlwaysRefresh/RefreshIfOlderThanMs）+ IDisplayEnumerator/IClock 注入口 |
| `WindowDetector.hpp` | WindowInfo（DWM frame/cloaked/alpha/zOrder）+ filterVisibleWindows/windowUnderPoint 纯函数 + 快照缓存服务 |
| `CaptureService.hpp` | CaptureEngine 枚举（WGC→DXGI→GDI）、CaptureRequest/Result、ICaptureEngine、回退链服务、`wgcSupportsBorderless(contract)` |
| `HotkeyManager.hpp` | HotkeySpec/Conflict/RegistrationReport、isHotkeySpecValid、findDuplicateSpec、applySpecs 生命周期、默认键位、格式化 |
| `OverlayRenderer.hpp` | OverlayMode 状态机（Idle/HoverWindow/Dragging/Confirmed）、PaintList、OverlayVisuals（40% 变暗/4px 阈值/accent）、layoutInPlaceToolbar |
| `SelectionOverlay.hpp` | Win32 控制器：precreate/beginSession/cancelSession + SelectionOverlayResult（wholeWindow 标记 FR-02） |
| `TrayIcon.hpp` | TrayCommand ×11、buildTrayMenu（最近 5 截图条目）、RecentShot、TrayIconService |

### 1.2 实现（`src/`，Win32 TU 与策略 TU 分离）

**策略 TU（可移植，无 Win32 包含，任何平台可编译/测试）**

| 文件 | 要点 |
|---|---|
| `DisplayMonitor.cpp` | 2s 缓存三态策略实现（互斥锁保护） |
| `WindowDetector.cpp` | cloaked/透明/零尺寸过滤 + topmost-first hit-test + mouse-down 快照缓存 |
| `HotkeyManager.cpp` | 注册生命周期（先全卸再注册=运行时即时生效）、冲突报告、MOD 位校验、Ctrl+Alt+F/A/T/D/I 默认 |
| `CaptureService.cpp` | 回退链遍历（available→capture→observer 上报）、区域与显示器求交裁剪、空交集快失败 |
| `OverlayRenderer.cpp` | 完整交互状态机：4px 点击/拖拽阈值、方向归一化、拖拽清除窗口高亮（FR-02）、Esc 取消、徽标/十字准线/工具栏骨架 paint list、双线程安全 |
| `TrayMenu.cpp` | 菜单模型纯函数（FR-34：5 动作+最近5+5占位+退出） |

**Win32 TU（`pch.h` 预编译，仅 Windows 编译）**

| 文件 | 要点 |
|---|---|
| `DisplayMonitorWin32.cpp` | EnumDisplayMonitors + GetMonitorInfoW(物理 rcMonitor) + GetDpiForMonitor(PMv2)；主显示器优先排序；工厂 makeWin32DisplayEnumerator/makeSteadyClock |
| `WindowDetectorWin32.cpp` | EnumWindows（Z 序天然 topmost-first）+ DWMWA_CLOAKED（UWP 挂起过滤）+ DWMWA_EXTENDED_FRAME_BOUNDS（可见边界，失败回退 GetWindowRect）+ 分层窗口 alpha + 进程映像名；排除自身（NOACTIVATE/TOOLWINDOW） |
| `HotkeyManagerWin32.cpp` | RegisterHotKey(MOD_NOREPEAT)/UnregisterHotKey + WM_HOTKEY 泵函数 |
| `WGCCaptureEngine.cpp` + `WGCCaptureEngineFull.cpp` | WGC 引擎：契约探测（UniversalApiContract 12）、GraphicsCaptureSession::IsSupported、IsBorderRequired=false 契约门（contract 15）；设备注入点留给 App 装配（见已知限制 §4.1） |
| `DxgiDuplicationEngine.cpp` | DXGI 回退：CreateDXGIFactory1 可用性探针 |
| `GdiCaptureEngine.cpp` | GDI 兜底：GetDC 可用性探针 |
| `SelectionOverlay.cpp` | WS_POPUP+WS_EX_LAYERED+WS_EX_TOPMOST+WS_EX_NOACTIVATE+WS_EX_TOOLWINDOW 跨虚拟桌面窗口；D3D11→D2D1.1(Factory2/Device/DeviceContext)→CreateSwapChainForComposition(flip,2 缓冲,premultiplied)→DComp(TargetForHwnd/Visual)；完整消息处理（MOUSEMOVE/LBUTTONDOWN/UP、Esc、KILLFOCUS），mouse-up 区分拖拽选区 vs 窗口单击（wholeWindow） |
| `TrayIcon.cpp` | Shell_NotifyIcon(NIM_ADD/NIM_DELETE) + TrackPopupMenuEx(TPM_RETURNCMD) + SetForegroundWindow 契约 |
| `NativeShellInit.cpp` | 组装根：makeCaptureService（链序 WGC→DXGI→GDI）、显示器/窗口/热键工厂 |
| `pch.h`/`pch.cpp` | Win32/WinRT/D2D/DComp/DXGI/D3D11 基线 + lib 链接注释 |

### 1.3 工程文件

- `CMakeLists.txt`：策略 TU 全平台 + Win32 TU `if(WIN32)` 门控 + Catch2 测试目标
- `TransFrame.NativeShell.vcxproj`：静态库（x64+ARM64，/W4 /WX Release，pch，策略 TU 显式 NotUsing pch）
- `TransFrame.NativeShell.Tests.vcxproj`：测试工程（引用 NativeShell）
- `app.manifest`：**PerMonitorV2** dpiAwareness + UTF-8 codepage + Win10/11 supportedOS
- `tests/`：Catch2 v3.7.1 amalgamated vendored + 8 个测试文件

### 1.4 文档（`windows/docs/windows/`）

每服务一份设计说明（API 选择理由+回退链）：`CaptureService.md`、`DisplayMonitor.md`、`WindowDetector.md`、`SelectionOverlay.md`、`HotkeyTray.md`、`NativeShell-Tests.md`。

---

## 2. FR 映射表

| FR | 要求 | 实现 | 状态 |
|---|---|---|---|
| **FR-01 区域截图** | 40% 变暗、拖拽、尺寸徽标、Esc、多显示器物理像素 | OverlayState（dimAlpha 0.40、4px 阈值、归一化拖拽、cancel）+ SelectionOverlay（跨虚拟桌面、D2D 绘制变暗/挖亮/1px 描边/徽标/十字准线）+ CaptureService.captureRegion（物理像素、显示器求交） | ✅ 壳层完成（预览窗口=任务 C 后续） |
| **FR-02 窗口自动识别** | 悬停检测、单击整窗、拖拽忽略、≤1 帧切换、cloaked 过滤、DWM 可见边界 | WindowDetector（EnumWindows+DWMWA_CLOAKED+EXTENDED_FRAME_BOUNDS，快照缓存 hit-test）+ OverlayState（hover 高亮 25% 填充+1px 描边，拖拽阈值后清除）+ SelectionOverlay mouse-up wholeWindow 分支 | ✅ 测试覆盖 DWM bounds vs GetWindowRect 差异 |
| **FR-03 全屏截图** | 多显示器选择、原生分辨率 | CaptureService.captureDisplay（per-display）+ DisplayMonitorService.isMultiMonitor（选择菜单判定） | ✅ 捕获壳层（菜单 UI 属 App 层） |
| **FR-04 翻译模式** | 热键=截图+OCR+翻译 | HotkeyAction::Translate 注册 + overlay 完成回调链（OCR/翻译=任务 C） | ✅ 热键与捕获路径 |
| **FR-05 就地工具栏** | 选区边缘工具条 | layoutInPlaceToolbar：8 槽位（36px 按钮/4px 间距/下缘翻转/桌面钳制）+ Confirmed 态 paint list | ✅ 布局骨架（按要求不接功能） |
| **FR-33 全局热键** | ×5、冲突上报、即时生效 | HotkeyManager：RegisterHotKey×5、失败→conflicts 报告（含用户可读 message）、applySpecs 先卸后注（即时生效）、MOD 校验 | ✅ 12 测试用例 |
| **FR-34 托盘** | 常驻图标、左键菜单、最近 5 截图 | TrayIconService（Shell_NotifyIcon）+ buildTrayMenu（10 命令+最近 5 条 recentIndex/thumbnailHint） | ✅ 缩略图数据待历史任务 |

**PRD §8 性能预算对策**：热键→覆盖层 ≤60ms（窗口+设备启动期预创建，热路径只 ShowWindow）｜松开→截图 ≤50ms（WGC frame pool 常驻+一次 staging 拷贝，GDI 30-80ms 仅兜底）｜120Hz 绘制 ≤8ms（纯 GPU fill/stroke，无文本无 GDI，paintList 纯内存）｜悬停查询 ≤1ms（快照内存 hit-test）｜空闲 CPU 0%（事件驱动无轮询）。

---

## 3. 测试证据（macOS arm64 · clang 21 · 实测）

```
$ clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude -c src/*.cpp   # 策略 TU 零警告
$ # (编译 tests/*.cpp + 链接)
$ ./native_shell_tests
All tests passed (235 assertions in 60 test cases)
```

Windows 复现（等价 CMake 路径）：`cmake -S windows/NativeShell -B build && cmake --build build && ctest --test-dir build`（MSVC 时 Win32 TU 一并编译）。

---

## 4. 已知限制

1. **Win10 黄边框降级**：`IsBorderRequired=false` 需 UniversalApiContract 15（Win11 21H2）；Win10 21H2 属性不存在，系统黄边框保留（PRD §6.13 既定降级，`wgcSupportsBorderless()` 已封装门控并有测试）。
2. **WGC 设备接线**：`WGCCaptureEngineFull.cpp` 的 captureOnce 留设备注入点（App 层 D3D 设备单例组装后接通）；契约探测、链序、几何、回退、观察者全部完成并有测试。DXGI/GDI 引擎 TU 当前为可用性探针 + 骨架（链行为由 StubEngine 全覆盖测试）。
3. **Esc 键盘接管**：WS_EX_NOACTIVATE 窗口不收键盘，选择会话内显式 SetFocus/SetForegroundWindow（会话结束隐藏归还）；焦点策略需 Windows 实机验证。
4. **托盘图标资产**：暂用系统占位图标；相机镜头图标属打包任务。缩略图渲染待历史数据层。
5. **尺寸徽标文本 / FR-06 结果面板**：徽标底板绘制就绪，DWrite 文本与结果面板属后续任务（本任务边界外）。
6. **Windows 编译未运行**：本机 macOS；vcxproj/CMake 为静态正确交付，MSVC 编译验证 NOT RUN（命令见 §3/§6）。

---

## 5. 与并行任务的接口

- **任务 A（Core）**：`CoreTypes.hpp` 按 PRD §4 自声明最小模型（PixelBuffer/DisplayInfo/Screenshot/HotkeyAction），字段名与 macOS Models 对齐；A 的 `windows/Core` 就绪后合并（删除自声明改 include 即可，无逻辑耦合）。
- **任务 C（翻译/OCR/预览）**：overlay 完成回调 `SelectionOverlayResult{regionPhysical, wholeWindow, windowHwnd}` + `CaptureService.captureRegion/captureDisplay` 是其输入；FR-05 工具栏 8 槽位布局已留。
- **任务 D（取色历史）**：托盘 `RecentShot.thumbnailPath` 供给位已留。

## 6. Windows 复现命令

```powershell
# 完整层（MSVC，x64 与 ARM64 各一次）
msbuild windows\TransFrame.sln /p:Configuration=Release /p:Platform=x64 /t:TransFrame_NativeShell;TransFrame_NativeShell_Tests
build\x64\Release\TransFrame.NativeShell.Tests\native_shell_tests.exe   # (路径以 OutDir 为准)

# 或 CMake
cmake -S windows\NativeShell -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
ctest --test-dir build -C Release
```
