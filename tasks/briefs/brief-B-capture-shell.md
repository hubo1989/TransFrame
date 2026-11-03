# Brief B — Windows 捕获与热路径 UI 层（US-W02/03/04/05 + 覆盖层/选区）

你是 TransFrame Windows 版的实现工程师之一。4 个并行子任务共用同一 PRD；你负责 B：屏幕捕获 + 全屏选区覆盖层 + 窗口检测 + 热键 + 托盘。

## 必读上下文（按序）
1. `tasks/prd-windows-transframe.md` §3.2 API 映射表、§6.1（FR-01…06）、§6.7（FR-33/34）、§8 性能预算
2. `docs/specs/window-detection-auto-select.md` — 窗口自动识别的完整交互规格（Windows 版语义一致，API 换用 EnumWindows/DWM）
3. `TransFrame/Features/Capture/`（SwiftUI 参考：SelectionOverlayWindow/SelectionInPlaceToolbar/InPlaceResultsPanel 行为）
4. 任务 A 产出的 `windows/Core/`（模型与接口；若未就绪，按 PRD §4 接口契约先行开发，后续对齐）

## 交付范围（只做这些，不越界）
在 `windows/NativeShell/`（+ 必要的 App 侧接入点）实现：

1. **WGC 捕获服务**：`CaptureService`（Windows.Graphics.Capture，per-monitor GraphicsCaptureItem；`IsBorderRequired=false`（contract 15，失败降级）；DXGI Duplication 与 GDI BitBlt 回退链）；输出统一为 Core 的 `Screenshot`（物理像素 + DPI 元数据）
2. **显示器枚举**：`DisplayMonitor`（EnumDisplayMonitors + GetDpiForMonitor，Per-Monitor V2；带 2s 缓存刷新策略，对齐 macOS DisplayFreshnessPolicy）
3. **窗口检测**：`WindowDetector`（EnumWindows + DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS/CLOAKED)；`windowUnderPoint()` 悬停查询 ≤1ms）
4. **全屏选区覆盖层**：`SelectionOverlay`（WS_POPUP+WS_EX_LAYERED+WS_EX_TOPMOST，跨全部显示器；Direct2D 1.1 + DirectComposition 渲染：40% 变暗、选区挖亮、1px 高亮描边、尺寸徽标、十字准线、窗口高亮（单击=整窗）、拖拽=手动圈选、Esc 取消；120Hz 下绘制路径 ≤8ms）
5. **全局热键**：`HotkeyManager`（RegisterHotKey/WM_HOTKEY ×5；注册失败=冲突上报；运行时重注册生效）
6. **托盘**：`TrayIcon`（Shell_NotifyIcon；左键 MenuFlyout：全屏/区域/翻译/划词/插入/历史/设置/退出占位 + 最近 5 张截图缩略图位）
7. 单元测试（Catch2）：窗口检测几何（DWM bounds vs WindowRect 差异用例）、DPI 换算、热键冲突矩阵、显示器缓存策略

## 明确约束
- 不做翻译/OCR/预览窗口/标注（C 与后续任务范围）；就地工具栏(FR-05)只做布局骨架不接功能
- UIA 划词不在本任务（归 C）
- 所有窗口 Per-Monitor V2 DPI 感知（manifest 声明）
- 性能预算硬指标：热键→覆盖层出现 ≤60ms；松开→截图产物 ≤50ms（含 GPU→CPU 一次拷贝）

## 完成标准
- 代码可编译（或 CI 绿；本机 macOS 无法编译 Windows 目标时，给出静态审查清单 + NOT RUN 标注）
- 每个服务一个 `docs/windows/*.md` 简要设计说明（API 选择理由 + 回退链）
- 报告：实现清单、与 PRD FR 映射表、已知限制（如 Win10 黄边框降级路径）
