# DisplayMonitor 设计说明（EnumDisplayMonitors + GetDpiForMonitor）

## API 选择理由

- **`EnumDisplayMonitors`**：一次遍历得到全部 HMONITOR；回调内
  `GetMonitorInfoW(MONITORINFOEXW)` 拿 `rcMonitor`（物理像素、虚拟桌面坐标）
  与 `szDevice`（显示器设备名，作为稳定 id）。进程声明 Per-Monitor V2
  （`app.manifest`）后，`rcMonitor` 已经是物理像素坐标——与捕获管线
  “选区以物理像素记录”（PRD FR-01）严格一致，无需换算。
- **`GetDpiForMonitor(MDT_EFFECTIVE_DPI)`**：PMv2 进程拿到每显示器有效 DPI，
  直接写入 `DisplayInfo.dpiX/dpiY`；`scale() = dpi/96` 与 macOS
  `DisplayInfo.scaleFactor` 语义对齐（100%/150%/200% 混合 DPI 验收）。
  API 失败（Win10 1607 前）回退假设 96。

## 2s 缓存刷新策略（对齐 macOS DisplayFreshnessPolicy）

macOS `CapturePipeline.lookupDisplay(freshness:.refreshIfOlderThan(2))` 的
语义一比一移植为 `DisplayFreshness` 三态：

| 策略 | 行为 | 测试用例 |
|---|---|---|
| `Cached` | 首次枚举后永不再枚举 | `Cached never re-enumerates` |
| `AlwaysRefresh` | 每次查询都枚举 | `AlwaysRefresh enumerates every query` |
| `RefreshIfOlderThanMs(2000)`（默认） | 缓存年龄 > 2s 才重新枚举 | `1999ms 内命中缓存`、`2001ms 过期重枚举一次` |

- 时钟经 `IClock` 注入（`FakeClock` 可推进），枚举经 `IDisplayEnumerator`
  注入（`CountingEnumerator` 计数）——策略完全可测（8 个用例）。
- 生产装配：`makeWin32DisplayEnumerator()` + `makeSteadyClock()`（src/DisplayMonitorWin32.cpp）。
- `WM_DISPLAYCHANGE` 到达时调用 `invalidate()`，下一次查询强制枚举
  （用户热插显示器后 ≤1 次查询内感知）。
- 排序稳定：主显示器在前，其次按 x 坐标——捕获目标选择不因枚举顺序抖动。

## 为什么热路径需要这个缓存

热键→覆盖层 ≤60ms 的预算里，`EnumDisplayMonitors + GetDpiForMonitor`
每次 1-3ms（多显示器更贵）。托盘常驻 + 每次热键都全量枚举是纯浪费；
2s 缓存让连续截图会话共享一次枚举，同时保证显示配置变更在 2s 内可见
（与 macOS 行为完全一致，两平台互测无差异）。
