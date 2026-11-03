# NativeShell 测试与构建说明

## 测试矩阵（Catch2 v3.7.1 amalgamated，60 用例 / 235 断言全绿）

| 文件 | 覆盖 | 用例数 |
|---|---|---|
| GeoTests.cpp | Rect 半开区间/交集/膨胀 | 3 |
| DisplayMonitorTests.cpp | 2s 缓存策略三态、1999ms/2001ms 边界、invalidate、displayAt、多显示器 | 8 |
| DpiScalingTests.cpp | 96/144/192 DPI scale、混合 DPI 跨屏裁剪、4K 步长 | 4 |
| WindowDetectorTests.cpp | cloaked/透明/零尺寸过滤、DWM bounds vs GetWindowRect 差异、Z 序 hit-test、快照缓存 | 8 |
| HotkeyManagerTests.cpp | 默认键位、注册、冲突矩阵（OS 拒绝/组内重复/全拒）、运行时重注册、禁用跳过、卸载、格式化 | 12 |
| CaptureServiceTests.cpp | WGC 契约门（15）、链优先级/两级回退/全失败、区域裁剪、空交集快失败、观察者 | 10 |
| OverlayRendererTests.cpp | Idle 十字准线、悬停高亮、4px 阈值、拖拽绘制、方向归一、确认+工具栏骨架、Esc、布局翻转、双线程 MT-safe | 11 |
| TrayMenuTests.cpp | 十命令全集、最近 5 条上限/顺序/索引、无记录路径、占位禁用 | 6 |

## 构建方式

### 任一平台（macOS/Linux 开发机，策略层）

```bash
cd windows/NativeShell
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

只编译可移植策略 TU（DisplayMonitor/WindowDetector/HotkeyManager/
CaptureService/OverlayRenderer/TrayMenu），Win32 TU 由 `if (WIN32)` 门控跳过。

### Windows（完整层，含 Win32/D2D/WGC）

- MSBuild：`windows/TransFrame.sln` 内
  `NativeShell/TransFrame.NativeShell.vcxproj`（静态库）+
  `NativeShell/TransFrame.NativeShell.Tests.vcxproj`（测试）
  ```
  msbuild TransFrame.sln /p:Configuration=Release /p:Platform=x64
  msbuild TransFrame.sln /p:Configuration=Release /p:Platform=ARM64
  ```
- 或 CMake（MSVC generator 同样编译 Win32 TU 集合）

## 依赖

- Catch2 v3.7.1 amalgamated 已 vendored（`tests/catch_amalgamated.{hpp,cpp}`），
  零包管理依赖
- nlohmann/json 单头 vendored 于 `vendor/nlohmann_json.hpp`（任务 A Core 依赖，
  此处随附避免 sln 级联）
- Windows SDK 10.0.26100（Win11 24H2）；工具集 v143

## 分层纪律（对任务 A/C 的接口）

- `include/tf/*.hpp` 是对 App 层唯一公开面；`src/*Win32.cpp` 全部内部符号
- Core 模型依赖按 PRD §4 契约自声明（`CoreTypes.hpp` 最小集合：PixelBuffer/
  DisplayInfo/Screenshot/HotkeyAction）；任务 A 的 `windows/Core` 就绪后
  以别名/迁移合并，字段名已对齐 macOS Models
