# PRD: TransFrame for Windows（屏幕翻译工具 · Win11 原生版）

- **Status**: Draft v1.0
- **Date**: 2026-11-02
- **Owner**: TransFrame Team
- **Baseline**: macOS 版 v1.0.4（功能完全对齐，见 §2 功能基线）
- **Reference PRDs**: `tasks/prd-macos-transframe.md`, `tasks/prd-engine-mode-convergence.md`, `docs/specs/window-detection-auto-select.md`, `docs/specs/color-history-window.md`
- **已裁决决策（2026-11-02，适用两平台）**：
  1. 本地翻译只做**引擎接入**（MTranServer/Ollama/兼容端点），不内置、不打包任何模型（LocalTranslate/ONNX 路线取消）。
  2. 历史检索**不启用 FTS5**，维持 LIKE 查询。
  3. **不考虑便携版**，仅 MSIX 单一分发通道。
  4. 取色历史**扩展为独立取色历史窗口**，Windows 与 macOS 同步交付（见 `docs/specs/color-history-window.md`）。

---

## 1. 概述（Overview）

TransFrame for Windows 是 macOS 屏幕翻译工具 TransFrame 的 Windows 原生版本。用户通过全局快捷键截取屏幕任意区域（或全屏/窗口），自动 OCR 识别文字，调用本地或云端翻译引擎翻译，以覆盖层、双语对照抽屉或就地替换的形式展示译文；同时提供划词翻译、翻译并插入、标注、置顶贴图、翻译历史、图床上传等完整能力。

**三大硬性要求**（本 PRD 的存在理由）：

1. **功能完全一致**：与 macOS v1.0.4 逐特性对齐（§2 基线表 + §6 FR 清单）；差异仅在平台能力本身导致的等价替换（§6.12 差异对照表）。
2. **Win11 标准 UI**：Fluent 2 设计语言 —— Mica 材质、圆角、深浅色模式、Segoe Fluent Icons、Snap Layouts、系统通知、任务栏托盘常驻（§8）。
3. **原生语言 + 尽可能高性能**：不使用 Electron/Web 壳；渲染走 GPU（Direct2D/DirectComposition），截图走系统级捕获 API，冷启动与常驻内存对标原生工具（§3、§9）。

### 1.1 Goals

- 与 macOS 版**特性集 1:1**（同一份 FR 清单驱动两个平台）。
- 截图→OCR→翻译→渲染 全链路 P95 延迟预算：热路径本地引擎 ≤ 1.2s，其中捕获 ≤ 50ms、本地 OCR ≤ 300ms（§9）。
- 常驻内存 ≤ 80MB（托盘态），空闲 CPU 0%，无后台轮询（事件驱动）。
- 原生 Win11 体验：开机自启动、任务栏托盘、系统通知、深色模式、多显示器/Per-Monitor DPI 感知。
- 同一份设置/历史/引擎数据模型（schema 与 macOS 对齐），为将来跨平台同步留接口。

### 1.2 Non-Goals

- 不做 Win32 老式 UI 风格的兼容模式。
- 不做 Windows 10 以下（Win7/8）适配；Win10 21H2 走降级路径（§6.13），非一等公民。
- 不做移动端（与 macOS 版一致）。
- 不内置翻译模型训练/微调。
- V1 不做云同步（历史/设置本地存储；schema 预留）。

---

## 2. 功能基线（macOS v1.0.4 特性全景 → Windows 对齐表）

| # | 特性 | macOS 实现 | Windows 对应（详见 FR） | 优先级 |
|---|------|-----------|--------------------------|--------|
| F-01 | 区域截图（拖拽选区） | ScreenCaptureKit + SelectionOverlayWindow | Windows.Graphics.Capture + 全屏层窗口 + D2D 选区 | P0 |
| F-02 | 窗口自动识别高亮（悬停检测 + 单击选窗口） | CGWindowListCopyWindowInfo | EnumWindows + DWMWA_EXTENDED_FRAME_BOUNDS | P0 |
| F-03 | 全屏截图（多显示器菜单） | SCK + DisplaySelector | WGC/DXGI Duplication + 显示器选择菜单 | P0 |
| F-04 | 翻译模式（截图即翻译） | CapturePipeline + AnalysisPipeline | 同管线移植 | P0 |
| F-05 | 就地选区工具栏 | SelectionInPlaceToolbar | D2D 绘制的层窗口工具栏 | P1 |
| F-06 | 就地结果面板 | InPlaceResultsPanel | 同上 | P1 |
| F-07 | 预览窗口（保存/复制/关闭） | PreviewWindow(SwiftUI) | WinUI 3 窗口 | P0 |
| F-08 | 标注：矩形/椭圆/直线/箭头/自由手绘/文字 | AnnotationCanvas | Direct2D 画布 | P0 |
| F-09 | 标注：马赛克/高亮/数字标签/隐私遮挡/气泡 | Annotation.swift 11 种 | Direct2D 画布（11 种全量） | P1 |
| F-10 | 智能打码（VLM 敏感区检测） | performSmartRedaction | VLM Provider 移植 | P2 |
| F-11 | 裁剪 + 撤销/重做（50 步） | CropDimOverlay + undo 栈 | 同 | P0 |
| F-12 | 保存 PNG/JPEG/HEIC + 质量设置 | ImageExporter | WIC 编码器（PNG/JPEG；HEIC 需扩展，见 §6.12） | P0 |
| F-13 | 复制到剪贴板（含标注） | ClipboardService | Win32 剪贴板 CF_DIB/PNG | P0 |
| F-14 | 置顶贴图（多窗口管理、点击穿透切换） | PinnedWindowsManager | WS_EX_TOPMOST + WS_EX_NOACTIVATE 独立 HWND | P1 |
| F-15 | OCR：Apple Vision（本地默认） | Vision framework | **Windows.Media.Ocr**（本地，随语言包） | P0 |
| F-16 | OCR：PaddleOCR（外部，fast/precise） | PaddleOCREngine | 同一外部进程协议移植 | P1 |
| F-17 | VLM 视觉分析：OpenAI/Claude/Gemini/Ollama/PaddleOCR | VLM Providers | HTTP Provider 移植 | P1 |
| F-18 | 翻译引擎 ×10：Apple/MTranServer/OpenAI/Claude/Gemini/Ollama/Google/DeepL/Baidu/自定义兼容 | Providers | 9 个移植 + Apple 引擎的平台替代（§6.12） | P0 |
| F-19 | OpenAI 兼容引擎多实例 | CompatibleConfig(UUID) | 同数据模型 | P1 |
| F-20 | 引擎选择 V2：ordered 有序回退链（≤5） | EngineModeV2.ordered | 同 | P0 |
| F-21 | 引擎选择 V2：compare 并行对比（2-4，免重译切换） | EngineModeV2.compare | 同（并行 HTTP） | P1 |
| F-22 | 引擎真实可用性探测（状态点，不烧配额） | EngineHealthChecker/MTranServerChecker | 同语义 | P1 |
| F-23 | 场景绑定（截图/划词/翻译插入 → 引擎链） | SceneEngineBinding + TranslationScene | 同 | P1 |
| F-24 | 自定义 Prompt（per-engine / per-兼容实例 / per-scene，{source_language}/{target_language}/{text} 占位符） | TranslationPromptConfig | 同 | P1 |
| F-25 | 可视行分组双语对照 | VisualLineGrouping.mergeLines | 纯算法移植 | P0 |
| F-26 | 译文覆盖层渲染（原位替换/下方追加） | OverlayRenderer + TranslationRenderPipeline | D2D 离屏合成 | P0 |
| F-27 | LLM 批量翻译（分段批处理 + 响应解析） | LLMTranslationBatching | 纯算法移植 | P1 |
| F-28 | 划词翻译（弹窗显示结果） | TextTranslationPopup + AX API | **UI Automation TextPattern + 剪贴板回退** | P0 |
| F-29 | 翻译并插入（目标重验证 + 粘贴回退，独立目标语言） | TextInsertionProgressController | UIA 选区重验证 + SendInput 粘贴 | P1 |
| F-30 | 翻译历史（翻译/截图/取色/上传 4 类，搜索/收藏/导出/缩略图） | HistoryStore + HistoryView | SQLite 存储 + WinUI 列表 | P1 |
| F-31 | 图床上传：PicGo/阿里 OSS/腾讯 COS/七牛 Kodo + Markdown 复制 | ImageUploadService | HTTP 客户端移植 | P2 |
| F-32 | 屏幕取色器（吸管 + HEX 历史） | eyedropperButton + color 历史 | GDI/WGC 单像素采样 | P2 |
| F-41 | **独立取色历史窗口**（大色块网格 + HEX/RGB + 复制 + 收藏/删除 + 清空，跨平台同规格） | 新增（与 Windows 同步交付） | WinUI 3 窗口（数据同 history.color 条目） | P2 |
| F-33 | 全局热键 ×5（全屏/区域/翻译/划词/插入，可自定义） | Carbon RegisterEventHotKey | **RegisterHotKey** | P0 |
| F-34 | 托盘常驻 + 菜单（全部功能入口 + 最近 5 张截图） | MenuBarController | Shell_NotifyIcon + WinUI MenuFlyout | P0 |
| F-35 | 设置窗口（通用/快捷键/标注/引擎/多引擎/语言/高级 + prompt 编辑） | Settings 13 个视图 | WinUI 3 NavigationView | P0 |
| F-36 | 密钥安全存储 | Keychain | **Windows Credential Manager / DPAPI** | P0 |
| F-37 | UI 本地化 10 语言（跟随系统） | Localizable.strings ×10 | .resw ×10 | P1 |
| F-38 | 自动更新 | Sparkle appcast | **MSIX + App Installer（.appinstaller）** | P2 |
| F-39 | 引导（首次启动权限/引导页） | Onboarding | 同语义（权限模型不同，§7） | P1 |
| F-40 | 性能埋点管线（各阶段延迟记录） | PerformanceRecorder/Pipelines | ETW/自记录移植 | P1 |

> P0 = 1.0 发布必需；P1 = 1.0 目标（可 1.1 补）；P2 = 1.x 路线图。

---

## 3. 技术选型（原生语言决策）

### 3.1 语言与 UI 框架（决策）

| 层 | 选型 | 理由 |
|----|------|------|
| **核心管线（截图/OCR 调度/翻译调度/覆盖层/标注渲染/热键/托盘）** | **C++20, C++/WinRT** | "尽可能高性能 + 原生语言"：零 GC、直接访问 WGC/D2D/DComp/Win32，捕获与渲染热路径无托管开销 |
| **设置/历史/引擎配置 UI** | **WinUI 3（Windows App SDK 1.5+），C++/WinRT 绑定** | "UI 以 Win11 标准方式"：Fluent 2、Mica、圆角、深浅色、控件无障碍全部开箱即得；非热路径，XAML 开发效率可接受 |
| **热路径窗口（选区覆盖层/预览画布/置顶贴图/就地工具栏）** | **纯 Win32 HWND + Direct2D 1.1 + DirectComposition** | XAML 不适合每帧重绘的标注画布与全屏覆盖层；D2D/DComp 是 GPU 加速的标准答案 |
| 脚本/CI 辅助 | PowerShell / pwsh | 打包签名、appinstaller 生成 |

**备选方案与否决理由**（记录决策，不再复议）：

- ~~C# + WinUI 3 全栈~~：更快的开发速度，但违反"原生语言/极致性能"的硬性要求（托管启动 ~150-300ms、GC 停顿在 120fps 标注绘制中可感知）。若后续评估开发产能不足，可降级为"核心 C++ + 设置 C#"混合，数据层接口（§4.2）已按 ABI 稳定设计，允许该切换。
- ~~Win32 + 自绘 Fluent 控件全量~~：Mica/DWM 圆角可做，但 NavigationView/TextBox 无障碍/输入法/触控全套自绘成本不可接受。
- ~~Electron/Tauri/Web~~：直接否决（内存 200MB+、非原生）。

### 3.2 系统能力映射（macOS API → Windows API 总表）

| 能力 | macOS | Windows 选型 | 备注 |
|------|-------|--------------|------|
| 屏幕捕获 | ScreenCaptureKit | **Windows.Graphics.Capture**（GraphicsCaptureItem per monitor） | GPU 纹理直达 D2D，零拷贝；Win11 支持关闭黄边框（`IsBorderRequired=false`，contract 15+）；GDI `BitBlt` 作为不支持时回退 |
| 全屏连续帧 | —（按需） | DXGI Desktop Duplication（备选） | 仅当 WGC 不可用 |
| 窗口枚举/检测 | CGWindowListCopyWindowInfo | **EnumWindows + DwmGetWindowAttribute(DWMWA_EXTENDED_FRAME_BOUNDS)** + `WindowFromPoint` | 真实可见边界（排除不可见阴影），DWM 层级过滤 cloaked/UWP 挂起窗口（`DwmGetWindowAttribute(DWMWA_CLOAKED)`） |
| 全局热键 | Carbon RegisterEventHotKey | **RegisterHotKey / WM_HOTKEY** | 冲突检测：RegisterHotKey 失败即冲突提示 |
| 划词取文本 | Accessibility AX API | **UI Automation（UIA TextPattern: GetSelection/RangeFromPoint）**；剪贴板回退：发送 Ctrl+C 模拟（可配置，默认开） | UIA 只读不需要管理员；TextPattern 覆盖 Win32/WinUI/WPF/浏览器 |
| 翻译并插入 | AX 设值 + 粘贴回退 | **UIA ValuePattern 设值** + `SendInput` Ctrl+V 粘贴回退 + **目标重验证**（读回选区比对） | 与 macOS 同一套安全语义：先重验证后粘贴 |
| 剪贴板 | NSPasteboard | Win32 Clipboard（CF_DIB + PNG 垃圾格式 + delayed render） | |
| 置顶窗口 | NSWindow level | **WS_EX_TOPMOST / WS_EX_NOACTIVATE / WS_EX_TOOLWINDOW** | 独立 HWND per pin |
| 全屏覆盖层 | NSWindow overlay | **WS_POPUP + WS_EX_LAYERED + WS_EX_TOPMOST + Per-Monitor V2 DPI**，DirectComposition 视觉树 | 选区 UI 载体 |
| 屏幕取色 | CGDisplayCreateImage 像素 | WGC 单帧或 `GetDC+GetPixel`（回退） | |
| OCR | Vision | **Windows.Media.Ocr.OcrEngine**（本地，Bing 离线语言包） | 中文识别质量弱于 Vision/Paddle → 默认引擎策略见 §6.4 |
| 本地翻译（Apple Translation 对位） | Translation framework | **无系统公开 API** → 从默认链移除该槽位，MTranServer 接棒默认。本地能力一律通过外部引擎接入（MTranServer / Ollama / 兼容端点），**不内置任何模型**（决策 1） | 平台无系统翻译 API；已裁决不引入模型运行时 |
| 密钥存储 | Keychain | **Windows Credential Manager（CredWrite/CredRead，TARGET=TransFrame/<engine>）**；备选 DPAPI CryptProtectData 加密后落 JSON | |
| 设置存储 | UserDefaults | **JSON 文件**：`%LOCALAPPDATA%\TransFrame\settings.json`（schema 与 macOS Keys 一一对应，前缀 TransFrame.） | 便于跨平台 schema 对齐与迁移 |
| 历史存储 | HistoryStore(JSON) | **SQLite（WAL）**：`%LOCALAPPDATA%\TransFrame\history.db`；含 schema v1 迁移表 | 搜索用 LIKE（决策 2：不启用 FTS5） |
| HTTP 客户端 | URLSession | **WinHTTP**（或 `Windows.Web.Http`）；重试/超时/取消语义对齐 URLSession 封装 | |
| 自动更新 | Sparkle appcast | **MSIX + App Installer**：.appinstaller 指向 HTTPS 更新源，检查周期/强制更新策略可配 | 仅 MSIX 单一分发通道（决策 3：不做便携版） |
| 图标/符号 | SF Symbols | **Segoe Fluent Icons** + 自绘 OCR/翻译字形 | |
| 材质 | NSVisualEffectView | **Mica（DWMWA_SYSTEMBACKDROP_TYPE=DWMSBT_MAINWINDOW）**；覆盖层用 Acrylic | |
| 深浅色 | NSAppearance | Registry `AppsUseLightTheme` + `UISettings.ColorValuesChanged` 事件 | |
| 通知 | UserNotifications | **Windows App Notifications（appnotification:）** | |
| 多显示器/DPI | NSScreen + backingScaleFactor | **EnumDisplayMonitors + GetDpiForMonitor（Per-Monitor V2）** | 坐标统一用物理像素，输出时换算 |
| 无障碍 | Accessibility | **UIA AutomationPeer（XAML 自动）+ 自绘控件的 UIA provider** | P1 验收项 |
| 开机自启 | SMLoginItemSetEnabled | **StartupTask（MSIX）** / HKCU Run 键 | |
| 性能埋点 | os_signpost + PerformanceRecorder | **ETW EventTracingLog + 同名 PerformanceRecorder 移植** | 导出 JSON 报告 |

### 3.3 依赖清单（全部原生/开源，无运行时付费）

| 组件 | 选择 | 许可 |
|------|------|------|
| Windows SDK | 10.0.26100+（Win11 24H2） | — |
| C++/WinRT | 2.0.230706+ | MIT |
| Windows App SDK | 1.5+ | MIT |
| JSON | nlohmann/json v3.11 | MIT |
| SQLite | 3.45+（仅 history.db，**LIKE 检索，不启用 FTS5**，决策 2） | Public Domain |
| libheif（HEIC 导出） | 1.17+ | LGPL-3（动态链接，或改用 WIC HEIF 扩展探测） | 见 §6.12 |
| 单元测试 | Catch2 v3 / MSVC 自带 | BSL-1.0 |
| 打包 | MSIX + signtool（EV 证书或 Azure Trusted Signing） | — |

---

## 4. 系统架构

### 4.1 分层与模块（对齐 macOS feature-based 结构）

```
┌────────────────────────────────────────────────────────────────────┐
│ App 层                                                             │
│  App.cpp（入口）  AppController（生命周期/热键分发/权限）           │
│  Coordinators: CaptureCoordinator / TextTranslationCoordinator /    │
│                HotkeyCoordinator / TrayCoordinator                  │
├────────────────────────────────────────────────────────────────────┤
│ Features（UI 层）                                                   │
│  TrayMenu/          托盘图标 + MenuFlyout（最近截图 5 张）          │
│  SelectionOverlay/  全屏选区层窗口（D2D：变暗、选框、尺寸提示、     │
│                     窗口高亮、十字准线、就地工具栏、就地结果面板）   │
│  Preview/           预览窗口（WinUI 3 外壳 + D2D 标注画布子 HWND）  │
│  Pinned/            置顶贴图窗口管理器（多窗口/穿透切换/回收）      │
│  TextTranslation/   划词弹窗 + 插入进度控制器                       │
│  Settings/          NavigationView 七页签设置                      │
│  History/           历史窗口（搜索/收藏/导出/缩略图）               │
│  Onboarding/        首启引导                                       │
├────────────────────────────────────────────────────────────────────┤
│ Services（业务层，可测、无 UI 依赖）                                │
│  Capture/           WGC 捕获 + 显示器枚举 + 窗口检测               │
│  OCR/               OcrEngine 接口：WindowsOcr / PaddleOcr / Vlm   │
│  Translation/       EngineRegistry + 10 Provider + 兼容实例 +       │
│                     ordered/compare 调度 + EngineHealthChecker      │
│  Pipelines/         CapturePipeline / AnalysisPipeline /            │
│                     TranslationRenderPipeline（含 PerformanceRecorder）│
│  VisualLineGrouping / LLMTranslationBatching（纯算法，全平台共享）  │
│  Overlay/           OverlayRenderer（D2D 离屏合成）                 │
│  Hotkeys/ TextInsertion/ Clipboard/ ImageExport(WIC)/ ImageUpload/  │
│  ColorPicker/ KeyStore(CredMan)/ SettingsStore(JSON)/ HistoryStore  │
├────────────────────────────────────────────────────────────────────┤
│ Models（纯数据，schema 与 macOS 一一对应）                          │
│  AppSettings / Annotation(11 种) / Screenshot / CaptureDocument /    │
│  OCREngineType / TranslationEngineType / EngineChainItem /          │
│  EngineModeV2 / TranslationScene / SceneEngineBinding /             │
│  TranslationPromptConfig / TranslationResultBundle / BilingualSegment │
│  / TranslationHistory / ExportFormat / AppLanguage / ...            │
└────────────────────────────────────────────────────────────────────┘
        TransFrameTests（Catch2：模型/算法/管线单测 + PRD 验收测试）
```

### 4.2 模块形态与 ABI

- `Core`（Models + 纯算法 Services）编译为**静态库 `TransFrame.Core.lib`**：无 WinRT UI 依赖，Catch2 直测；未来允许 C# 互操作切换（§3.1 备选）。
- `NativeShell`（Win32/D2D/WGC/热键/托盘）静态库，仅依赖 Core + Windows SDK。
- `TransFrame`（WinUI 3 app，C++/WinRT），依赖上两者。
- 输出：MSIX 包（x64 + arm64）。

### 4.3 关键数据流（与 macOS 管线同构）

```
热键 → AppController
  → CapturePipeline: 权限检查 → 显示器查找(带 2s 缓存策略) → WGC 捕获 → Screenshot
  → SelectionOverlay: 窗口检测高亮 / 拖拽选区 → captureRegion
  → AnalysisPipeline: OCR(WindowsOcr|Paddle|VLM) → 分段(TextSegment+归一化bbox+confidence)
  → TranslationRenderPipeline: VisualLineGrouping 合并视觉行 →
       ordered: 链式回退  |  compare: 并行(2-4)
     → TranslationResultBundle(per-engine EngineResult + BilingualSegment[])
  → 呈现: PreviewResultsPanel(双语抽屉) / OverlayRenderer(覆盖层) / Pinned / History
每阶段经 PerformanceRecorder 计时（operationID 串联），可导出延迟报告。
```

---

## 5. 用户与场景

（与 macOS 版一致，平台无差异）

- **U1 玩游戏/看外语文档的普通用户**：快捷键圈屏幕 → 原位看到译文。核心路径延迟敏感。
- **U2 开发者/研究者**：大量外文文档、报错、技术网页；划词翻译 + 翻译并插入写代码注释。
- **U3 内容创作者**：截图 + 标注 + 打码 + 图床上传 + 贴图置顶对照。
- **U4 隐私敏感用户**：只用本地链（Windows OCR + MTranServer/Ollama 本地端点），设置里显式禁用云引擎。

---

## 6. 功能需求（FR，含验收标准）

> 验收标准 = TransFrameTests 中可执行测试或可手工复现步骤。P0 缺一不可发布。

### 6.1 截图捕获（FR-01…06）

**FR-01 区域截图（P0）**
- 热键（默认 `Win+Shift+S` 冲突避让 → 默认 **Ctrl+Alt+A**，可自定义）进入全屏选区模式：所有显示器变暗（半透明黑 40%），显示十字准线与实时尺寸徽标。
- 拖拽绘制矩形，选区内保持原亮度并 1px 高亮描边；松开即捕获。
- Esc 取消；捕获期间覆盖层接收全部输入（鼠标+键盘）。
- 多显示器：覆盖层横跨整个虚拟桌面；坐标以物理像素记录，Per-Monitor V2 DPI。
- 验收：单屏/双屏混合 DPI(100%/150%/200%) 下选区像素与所见一致；松开→预览窗口出现 ≤ 100ms（本地实测，PerformanceRecorder 记录）。

**FR-02 窗口自动识别（P0，对齐 window-detection-auto-select spec）**
- 选区模式下鼠标悬停：实时检测光标下最顶层可见窗口，1px 强调色边框高亮 + 淡色填充；单击即整窗捕获。
- 按下并拖动 → 忽略高亮，进入手动圈选（行为与 FR-01 完全一致）。
- 高亮随移动平滑切换（≤ 1 帧）；桌面空白处只显示十字准线。
- Win32/UWP(含 cloaked 过滤)/浏览器子窗口均正确；最大化窗口排除阴影边界（DWMWA_EXTENDED_FRAME_BOUNDS）。
- 验收：Notepad/Chrome/WinUI 应用/UWP 各悬停单击，捕获边界与 DWM 可见边一致；拖拽路径不触发整窗捕获。

**FR-03 全屏截图（P0）**
- 热键（默认 Ctrl+Alt+F）全屏捕获；多显示器时弹出显示器选择菜单（含分辨率与"主显示器"标记），单显示器直接捕获。
- 验收：3 屏混合 DPI，每屏均按原生分辨率捕获。

**FR-04 翻译模式（P0）**
- 热键（默认 Ctrl+Alt+T）= 区域截图 + 自动 OCR + 翻译 + 结果呈现，无额外点击。
- 验收：从热键到译文出现计时（本地 OCR+MTranServer P95 ≤ 1.2s）。

**FR-05 就地工具栏（P1）**：选区确认后选区边缘出现工具条（复制/保存/翻译/置顶/取色/取消），D2D 绘制，可鼠标操作，Esc 关闭。
**FR-06 就地结果面板（P1）**：翻译结果停靠在选区侧边（空间自适应），显示原文/译文对照，可切换引擎结果（compare 模式），可"贴图置顶"。

### 6.2 预览与标注（FR-07…12）

**FR-07 预览窗口（P0）**
- 捕获后打开预览：标题栏（文件名/尺寸）、工具栏、画布、（翻译模式下）结果抽屉。
- 快捷键：`Enter`/`Ctrl+S` 保存；`Ctrl+C` 复制并关闭；`Esc` 关闭/退出裁剪；`R/1` 矩形、`D/2` 手绘、`A/3` 箭头、`T/4` 文字、`C` 裁剪、`Ctrl+Z` 撤销、`Ctrl+Y`/`Ctrl+Shift+Z` 重做、`Del` 删除选中标注、拖拽文字标注可移动。
- 窗口支持 Snap Layouts、记住尺寸位置、深浅色跟随。

**FR-08/09 标注（P0/P1）**
- 11 种全量：矩形、椭圆、直线、箭头、自由手绘、文字、马赛克、高亮、数字标签、隐私遮挡、气泡。各自支持颜色/线宽/填充/字号（对齐 macOS Annotation 模型的每类型属性集）。
- 选中/移动/删除单个标注；标注渲染在 D2D 硬件加速画布，笔迹延迟 ≤ 8ms（120Hz 显示器实测）。
- ⌘-drag 约束等价：Shift+拖拽 = 正方形/正圆/水平垂直线/45° 箭头。

**FR-10 智能打码（P2）**：VLM 检测敏感区域（人脸/证件/电话等）批量生成马赛克标注，单次撤销覆盖整批。
**FR-11 裁剪/撤销（P0）**：裁剪进入覆盖交互，Enter 应用/Esc 取消；裁剪清除标注（与 macOS 行为一致，提示确认）；撤销栈 50 步。
**FR-12 保存与导出（P0）**：格式 PNG/JPEG/HEIC + JPEG/HEIC 质量滑杆；默认位置可配（默认 `图片\屏幕截图`）；文件名模板（时间戳）；保存对话框/直接保存可配。HEIC：优先走系统 WIC HEIF 编码器（Win11 需装 HEVC 扩展，未装时 UI 明示并回退 PNG/JPEG）。

### 6.3 OCR（FR-15…17）

**FR-15 Windows OCR 引擎（P0）**
- `Windows.Media.Ocr`：本地、离线、按已安装语言包自动可用；结果映射为 `TextSegment[]`（text + 归一化 bbox + confidence）。
- 语言包缺失时给出安装引导（ms-settings:regionlanguage）；中文包未装时状态点为灰。
- 验收：中英混合截图段落级识别，bbox 归一化坐标与 macOS 版同 schema。

**FR-16 PaddleOCR 外部引擎（P1）**
- 与 macOS 相同的外部进程协议（可执行路径 + fast(~1s)/precise(~12s) 两档）；健康检查缓存语义一致。
**FR-17 VLM OCR/分析（P1）**
- OpenAI/Claude/Gemini/Ollama/PaddleOCR-VL 五个 Provider 移植：图像 base64/URL 上传、分段解析（bbox+confidence）、错误分类。

### 6.4 翻译引擎（FR-18…27）

**FR-18 引擎集（P0）**
- 移植 9 个：MTranServer、OpenAI、Claude、Gemini、Ollama、Google、DeepL、Baidu、自定义 OpenAI 兼容（含无密钥本地端点）。
- Apple Translation 无 Windows 对应：**默认有序链调整为 `MTranServer → WindowsOCR 场景下的次选`**；"本地翻译"能力全部通过外部引擎接入（MTranServer/Ollama/兼容端点），**不内置模型**（决策 1，详见 §6.12）。
- 每引擎配置卡片：开关、密钥（Credential Manager）、端点/模型、超时（默认 10s）、测试连接。

**FR-19 兼容引擎多实例（P1）**：OpenAI 兼容端点可建多实例（UUID 标识），链/对比/场景绑定中以实例身份出现。

**FR-20 ordered 有序回退链（P0）**：≤5 个引擎按序尝试，单个失败自动落入下一个；每个引擎结果带延迟；整链失败输出聚合错误（per-engine 摘要，对齐 TextTranslationError 聚合行为）。
**FR-21 compare 并行对比（P1）**：2-4 引擎并行请求；结果侧栏同时呈现，点击切换不重发请求（缓存 per-engine EngineResult）。
**FR-22 可用性探测（P1）**：设置页引擎状态点（绿=可用/红=不可达/橙=慢/灰=未配置）；探测仅用轻量端点（如 MTranServer ping、LLM models 列表），**绝不调用计费翻译接口**；结果缓存 TTL 与 macOS 一致。
**FR-23 场景绑定（P1）**：screenshot / text_selection / translate_and_insert 三场景可各自绑定 ordered 链或 compare 集；未绑定时用全局链。
**FR-24 自定义 Prompt（P1）**：per-engine / per-兼容实例 / per-scene 三层覆盖；默认模板含 `{source_language}` `{target_language}` `{text}` 占位符；插入场景有专用默认 prompt（简洁、无格式）。
**FR-25 视觉行分组（P0）**：`VisualLineGrouping.mergeLines` 纯算法移植：同一视觉行的碎片合并为一个翻译单元与一个覆盖层锚点。验收：移植 macOS 同套测试向量（跨平台共享测试数据 JSON）。
**FR-26 覆盖层渲染（P0）**：原位替换（按背景色自适应文字色）与下方追加两种模式；D2D 离屏渲染后合成回截图；导出/贴图/复制均带覆盖层。
**FR-27 LLM 批量翻译（P1）**：分段批处理（编号分隔）+ 响应解析器移植（处理截断/乱序/多行），失败自动降级为逐段。

### 6.5 划词与插入（FR-28/29）

**FR-28 划词翻译（P0）**
- 热键（默认 Ctrl+Alt+D）→ 优先 UIA TextPattern 取当前焦点控件选区文本；不支持 UIA 的应用回退"模拟 Ctrl+C 读剪贴板"（设置可关，且回退前保存/恢复原剪贴板内容）。
- 结果弹窗：无焦点抢占（WS_EX_NOACTIVATE），原文/译文/引擎延迟/重译按钮/copy/收藏；点击他处自动关闭。
- 验收：记事本/Word/Chrome/Edge/WinUI 应用/终端 各验证 UIA 路径；剪贴板回退路径验证原内容恢复。

**FR-29 翻译并插入（P1）**
- 热键（默认 Ctrl+Alt+I）→ 取选中原文 → 翻译（独立目标语言设置）→ **目标重验证**（UIA 读回当前选区仍为原文，未变则继续）→ 粘贴回退（SendInput Ctrl+V）；重验证失败则中止并提示，绝不覆盖意外内容。
- 插入进度浮层（进行中/完成/失败三态）；全程不与输入法冲突（对齐 macOS 语义：粘贴而非直接设值优先）。

### 6.6 历史 / 图床 / 取色（FR-30…32）

**FR-30 翻译历史（P1）**：4 类条目（translation/screenshot/color/upload）；字段与 macOS TranslationHistory 一致；搜索（原文/译文）、收藏、删除、导出（JSON/CSV/TXT）、缩略图（截图类）、最近 5 张截图入口在托盘菜单；容量上限与自动清理可配。
**FR-31 图床上传（P2）**：PicGo(本地 HTTP)/阿里 OSS/腾讯 COS/七牛 Kodo 四通道 + 配置卡片 + 上传后复制（URL 或 Markdown 可选）；上传记录入 history。
**FR-32 屏幕取色器（P2）**：放大镜取色（WGC 单帧采样），HEX 显示、加入历史（color 类条目）、点击复制。

**FR-41 独立取色历史窗口（P2，两平台同步交付，见 `docs/specs/color-history-window.md`）**
- 独立窗口（非弹层），从托盘菜单与取色器入口打开；网格色块（≥48px）+ HEX/RGB 文本 + 取色时间。
- 操作：单击复制 HEX；右键/悬浮菜单收藏、删除；顶栏搜索（HEX 片段）+ 仅收藏过滤 + 清空全部（带确认）；复制成功即时反馈。
- 数据：复用 history `color` 类目条目（不另建存储）；条目上限/自动清理沿用历史设置。
- 验收：取色 → 打开窗口看到条目；收藏过滤、搜索、删除、清空各路径；两平台同一交互规格。

### 6.7 壳与系统（FR-33…40）

**FR-33 全局热键（P0）**：5 组热键（全屏/区域/翻译/划词/插入）RegisterHotKey 注册；冲突时注册失败→设置页标红 + 托盘气泡提示；改后即时生效无需重启；快捷键录制控件（键+修饰键，必须含至少一个修饰键，校验规则与 macOS 版一致）。
**FR-34 托盘（P0）**：常驻托盘图标（截图镜头），左键弹 MenuFlyout：全屏/区域/翻译/划词/插入/置顶管理/历史/设置/检查更新/退出 + 最近截图（5 张缩略图）；托盘图标不占任务栏。
**FR-35 设置窗口（P0）**：NavigationView 七页：通用（语言/保存/格式/质量/开机自启/更新检查）、快捷键、标注默认样式、引擎（10 卡片+兼容实例管理）、多引擎（ordered 链编辑器/compare 编辑器/场景绑定/prompt 编辑）、语言（源/目标/插入独立目标/界面语言）、高级（日志/性能报告/数据目录/重置）。
**FR-36 密钥安全（P0）**：所有 API Key 存 Credential Manager（`TransFrame/<engine>/<instance>`），明文不落 settings.json；UI 显示为 `sk-****last4`；导出配置不含密钥。
**FR-37 本地化（P1）**：10 语言 .resw（en/zh-Hans/de/es/fr/it/ja/ko/pt/ru）+ 跟随系统 + 手动覆盖；日期数字格式随区域。
**FR-38 自动更新（P2）**：MSIX + .appinstaller（HTTPS 源、版本比较、可选强制）；仅 MSIX 单一分发通道（决策 3，无便携版）。
**FR-39 首启引导（P1）**：3 步引导（热键介绍 → 隐私声明：哪些引擎联网 → 默认链选择）；无 macOS 权限弹窗（Windows 无屏幕录制权限概念，UIA 只读无需授权）。
**FR-40 性能埋点（P1）**：PerformanceRecorder 移植，阶段=permission/displayLookup/capture/ocr/translation/render；operationID 串联全链路；设置页可导出 JSON 报告。

### 6.8 Win11 UI/UX 规范（FR-50 组）

- **FR-50.1（P0）Mica 材质**：主设置/历史/预览窗口 `DWMWA_SYSTEMBACKDROP_TYPE=DWMSBT_MAINWINDOW`；选区覆盖层/弹窗用 Acrylic（`DWMSBT_TRANSIENTWINDOW`）。
- **FR-50.2（P0）圆角**：窗口 DWM 自动圆角；自绘弹层 8px 圆角。
- **FR-50.3（P0）深浅色**：跟随系统（含"自动/浅/深"三态覆盖）；所有自绘内容双主题色板。
- **FR-50.4（P0）Snap Layouts**：预览/历史/设置窗口支持 Win11 贴靠（标准 Win32 即得，验收确认）。
- **FR-50.5（P1）Segoe Fluent Icons**：全部图标走 Segoe Fluent Icons / Segoe MDL2 fallback；小图标对齐 16/20/24px 网格。
- **FR-50.6（P1）无障碍**：XAML 控件 UIA 自动暴露；自绘工具栏/画布实现 UIA Provider（名称/角色/可用性）；完整键盘操作路径；对比度 ≥ 4.5:1。
- **FR-50.7（P1）系统通知**：保存/上传/更新完成用 Windows 通知，失败用对话框。
- **FR-50.8（P1）高刷新率**：标注画布 DComp 垂直同步，120Hz+ 无撕裂。
- **FR-50.9（P2）动画**：面板展开、弹窗淡入等用 Composition 动画（Implicit Animations），关闭可在设置禁用（减少动效）。

### 6.9 数据模型与迁移（FR-60）

- settings.json schema 与 macOS UserDefaults 键一一对应（`TransFrame.` 前缀保留）；Windows 专属键加 `.win` 后缀命名空间。
- 历史库 schema v1：`entries(id, category, source_text, translated_text, source_lang, target_lang, timestamp, thumbnail_path, is_favorite, meta_json)` + `schema_version` 表。
- 验收：两平台同一截图输入 → 输出的 BilingualSegment/EngineResult JSON 逐字段等值（跨平台快照测试）。

### 6.10 安全与隐私（FR-61）

- 明确的网络出口白名单（按引擎配置动态）；设置页"隐私"面板列出：本地处理 vs 联网引擎清单。
- 密钥仅 Credential Manager；崩溃日志（可选开启）不含密钥/剪贴板内容。
- MSIX 签名（Azure Trusted Signing 或 EV 证书）；SmartScreen 信誉积累。

### 6.11 可靠性（FR-62）

- 引擎请求全部可取消（WinHTTP + cancel token）；全局"取消"Esc 传播。
- 单引擎崩溃/超时不拖垮链（ordered 语义）；OCR/翻译异常都有用户可读错误 + 重试。
- 崩溃时未保存的截图/标注在下次启动可恢复（session 文件 `%LOCALAPPDATA%\TransFrame\session\`）。

### 6.12 平台差异对照表（唯一允许的行为差异）

| 差异点 | macOS | Windows 处理 | 理由 |
|--------|-------|--------------|------|
| Apple Translation | 系统内置本地翻译 | 移除该引擎，默认链首改为 MTranServer（若不可达按链回落）；本地能力一律外部引擎接入（MTranServer/Ollama/兼容端点），不内置模型（决策 1） | Windows 无公开系统翻译 API |
| Apple Vision OCR | 默认引擎 | Windows.Media.Ocr 为默认；中文质量不足场景由 PaddleOCR/VLM 补 | 平台 OCR 能力差异 |
| HEIC 导出 | 系统原生 | WIC HEIF（需 HEVC 扩展）探测，未装时 UI 提示并回退 | 编解码器分发差异 |
| Sparkle 更新 | appcast | MSIX/App Installer | 平台标准 |
| 屏幕录制权限 | 系统授权弹窗 | 无权限概念（WGC 首次出现单次系统确认条） | 平台安全模型 |
| 辅助功能权限 | Accessibility 授权 | UIA 只读无需授权 | 平台安全模型 |
| 默认快捷键 | Cmd 系 | Ctrl+Alt 系（Win+Shift+S 留给系统截图） | 键位布局差异 |
| 触控栏/焦点行为 | NSWindow | WS_EX_NOACTIVATE 等价实现 | 窗口模型差异 |
| 全局快捷键实现 | Carbon | RegisterHotKey（系统级，不吃焦点） | — |

### 6.13 支持矩阵

| 场景 | Win11 22H2+ (22621) | Win10 21H2 (19044) |
|------|---------------------|--------------------|
| WGC 捕获（无边框） | ✅ 完整 | ⚠️ 黄边框保留（系统限制） |
| Mica/Acrylic | ✅ | ⚠️ 回退纯色 |
| Snap Layouts | ✅ | ❌（标准贴靠） |
| UIA 划词 | ✅ | ✅ |
| 支持级别 | 一等公民 | 降级支持，不阻塞发布验收 |

最低：Windows 10 21H2；目标：Windows 11 24H2。x64 + arm64 双架构。

---

## 7. 权限与首启流程

1. 首次触发截图：WGC 在系统级出现一次"TransFrame 开始捕获"确认（系统行为，非应用弹窗）；之后静默。
2. 划词：UIA 只读零权限；"剪贴板回退"模式首次使用时提示一次。
3. 首启引导三步（FR-39）后直接可用，**无需重启**（对比 macOS 的屏幕录制重启）。

---

## 8. 性能预算（NFR）

| 指标 | 预算 | 测量方式（PerformanceRecorder/ETW） |
|------|------|--------------------------------------|
| 热键→选区覆盖层出现 | ≤ 60ms | ETW 事件对 |
| 松开选区→预览窗口首帧 | ≤ 100ms | recorder |
| WGC 捕获 4K 全屏 | ≤ 50ms（GPU→CPU 一次拷贝） | recorder |
| 本地 OCR（1080p 选区） | ≤ 300ms P95 | recorder |
| 本地链翻译（MTranServer） | ≤ 500ms P95 | recorder |
| **端到端（热键→译文覆盖层，本地链）** | **≤ 1.2s P95** | 全链 operationID |
| 标注笔迹输入延迟 | ≤ 8ms（120Hz） | 帧时间戳 |
| 常驻内存（托盘态） | ≤ 80MB 工作集 | 性能监视器 |
| 冷启动（托盘就绪） | ≤ 800ms | 事件对 |
| 空闲 CPU | 0%（无轮询，事件驱动） | 性能监视器 |
| 安装包体积 | ≤ 25MB（arm64+x64 合包） | 构建产物 |

验收：以上每项写入 `TransFrameTests/perf/` 基准，CI 记录趋势，超预算 20% 报警。

---

## 9. 用户故事（US，映射 FR；格式对齐 macOS PRD）

> Quality Gates（每个 US 完成的定义）：`msbuild /p:Configuration=Release` 零警告（/W4 /WX 于 Core）、Catch2 全绿、swiftlint 对应物 = clang-format 检查通过、MSIX 打包成功、x64+arm64 双架构构建。

### US-W01: 工程初始化与托盘骨架
As a developer, I want the WinUI 3 + C++20 solution skeleton so features build on a solid base.
- [ ] 解决方案三分层（Core/NativeShell/App）+ Catch2 测试工程
- [ ] 托盘图标 + MenuFlyout（全屏/区域/翻译/划词/插入/历史/设置/退出占位项）
- [ ] MSIX 打包 + 签名脚本；x64/arm64 CI
- [ ] 设置/历史目录 `%LOCALAPPDATA%\TransFrame\` 初始化

### US-W02: 全局热键
（FR-33）RegisterHotKey ×5，可配置、冲突提示、即时生效、Esc 取消传播。

### US-W03: 区域截图 + 全屏覆盖层
（FR-01）全屏变暗、拖拽、尺寸徽标、Esc 取消、多显示器、DPI、WGC 捕获 → Screenshot 模型。

### US-W04: 窗口自动识别
（FR-02）EnumWindows/DWM 检测、悬停高亮、单击整窗、拖拽忽略，性能 ≤1 帧切换。

### US-W05: 全屏截图与显示器选择
（FR-03）

### US-W06: Windows OCR 引擎
（FR-15）Windows.Media.Ocr → TextSegment[]、语言包检测与引导、异步不阻塞 UI。

### US-W07: MTranServer 翻译引擎
（FR-18）HTTP 客户端（超时 10s、取消、错误分类）、自动语言检测、目标语言配置。

### US-W08: 翻译引擎全家（LLM/云）
（FR-18）OpenAI/Claude/Gemini/Ollama/Google/DeepL/Baidu/自定义 ×9 Provider + 密钥卡 + 测试连接。

### US-W09: ordered 有序回退链
（FR-20）≤5 链、逐个回落、聚合错误、per-engine 延迟显示。

### US-W10: 预览窗口与保存/复制
（FR-07/12）WinUI 外壳 + 画布、Enter 保存、Ctrl+C 复制、格式与质量、位置记忆。

### US-W11: 标注工具集（11 种）
（FR-08/09）D2D 画布、选中/移动/删除、Shift 约束、撤销重做 50 步、深浅色。

### US-W12: 裁剪
（FR-11）

### US-W13: 视觉行分组与双语抽屉
（FR-25 + PreviewResultsPanel 对位）合并行、原文/译文对照、可编辑原文重译。

### US-W14: 覆盖层渲染
（FR-26）原位替换/下方追加、自适应文字色、合成导出。

### US-W15: compare 并行对比
（FR-21）2-4 并行、免重译切换、延迟徽标。

### US-W16: 划词翻译
（FR-28）UIA TextPattern + 剪贴板回退 + 无焦点弹窗 + 重译/复制/收藏。

### US-W17: 翻译并插入
（FR-29）目标重验证 + 粘贴回退 + 独立目标语言 + 进度浮层。

### US-W18: PaddleOCR 外部引擎
（FR-16）路径配置、fast/precise、健康检查。

### US-W19: VLM 视觉分析
（FR-17）五 Provider、bbox 解析、智能打码（FR-10）。

### US-W20: 兼容引擎多实例
（FR-19）UUID 实例、链/对比/绑定引用。

### US-W21: 引擎健康探测 + 场景绑定 + Prompt
（FR-22/23/24）

### US-W22: 设置窗口七页签
（FR-35）含 ordered/compare 拖拽编辑器、快捷键录制、语言页。

### US-W23: 翻译历史
（FR-30）SQLite、4 类、搜索/收藏/导出、托盘最近截图。

### US-W24: 置顶贴图
（FR-14）多窗口、穿透切换（对齐 macOS"禁用点击穿透/恢复交互"语义）、聚拢到前台。

### US-W25: 图床上传
（FR-31）四通道 + Markdown 复制。

### US-W26: 屏幕取色器 + 独立取色历史窗口
（FR-32/FR-41）取色器落地；取色历史独立窗口（两平台同规格，与 macOS 侧 `ColorHistoryWindow` US 同步验收）。

### US-W27: 本地化 10 语言
（FR-37）

### US-W28: 自动更新
（FR-38）.appinstaller 通道（仅 MSIX，无便携版，决策 3）。

### US-W29: 性能埋点与基准
（FR-40 + §8 全部指标进 CI）。

### US-W30: 首启引导 + 无障碍 + 打磨
（FR-39/50.6）三步引导、UIA Provider、键盘路径。

---

## 10. 里程碑

| 里程碑 | 范围（US） | 出口条件 |
|--------|-----------|----------|
| **M1 骨架**（2 周） | W01, W02 | 托盘+热键可用；MSIX CI 双架构绿 |
| **M2 捕获闭环**（3 周） | W03, W04, W05, W10 | 截图→预览→保存/复制全链路（无翻译）；性能预算前 4 项达标 |
| **M3 翻译闭环**（3 周） | W06, W07, W08, W09, W13, W14 | 端到端 ≤1.2s P95（本地链）；双语抽屉+覆盖层 |
| **M4 完整功能**（4 周） | W11, W12, W15–W21, W22, W24 | 特性集 = macOS P0+P1 全勾 |
| **M5 打磨发布**（2 周） | W23, W25–W30 | P2 项按取舍；无障碍+本地化+更新通道；公开 Beta |

总计 ~14 周一名资深 C++ 工程师 + 0.5 名设计/测试支持。

---

## 11. 测试与质量门禁

- **单元**（Catch2, Core 层）：模型、VisualLineGrouping、LLMTranslationBatching、Prompt 解析、链调度（mock provider）、设置 schema 往返。
- **跨平台快照**：共享测试向量 JSON（与 macOS TransFrameTests 同源）保证两平台同输入同输出。
- **集成**：WGC 捕获（多 DPI）、UIA 划词（目标应用矩阵：Notepad/Word/Chrome/Edge/WinUI/终端）、热键冲突、Credential Manager 读写。
- **性能**：§8 全指标基准化，CI 趋势门禁（超 20% fail）。
- **UI**：WinUI 控件手测清单 + 截图对比（深浅色 ×10 语言抽样 3 语言）。
- **打包**：干净 Win11 VM 安装→热键全流程冒烟；Win10 21H2 降级矩阵冒烟。

---

## 12. 风险与缓解

| 风险 | 等级 | 缓解 |
|------|------|------|
| C++/WinRT + WinUI 3 开发效率低于预期 | 高 | §3.1 备选：设置层切 C#（ABI 已隔离）；里程碑 M2 末做 Go/No-Go 评审 |
| Windows OCR 中文质量不达用户预期 | 中 | 默认链文档化推荐 PaddleOCR；VLM 补充；设置页引导安装中文语言包 |
| 无 Apple Translation 对位造成"无默认可用引擎" | 中 | 首启引导第 3 步强制选默认链（MTranServer/云引擎/Ollama 外部接入，不内置模型，决策 1） |
| UIA 在旧 Win32 应用上 TextPattern 缺失 | 中 | 剪贴板回退 + 回退开关；目标应用矩阵持续维护 |
| WGC 黄边框（Win10）观感差 | 低 | 文档明示 Win11 体验最佳；Win10 保留边框并提示 |
| MSIX 签名与 SmartScreen 信誉 | 中 | Azure Trusted Signing 起步；发布说明引导 |
| 120Hz 笔迹延迟不达标 | 低 | DComp 独立层 + 预渲染笔迹缓存；预算 8ms 有 50% 余量 |
| arm64 设备驱动级 API 差异 | 低 | 双架构 CI 全量测试；WGC/D2D 均有 arm64 原生支持 |

---

## 13. 开放问题（Open Questions）

1. ~~LocalTranslate（ONNX+DirectML）进 1.x 的验收标准~~ → **已裁决：不内置任何模型**，本地能力仅做外部引擎接入（决策 1）。
2. ~~历史库是否启用 FTS5~~ → **已裁决：不启用，维持 LIKE**（决策 2）。
3. ~~便携版（portable zip）是否进 1.0~~ → **已裁决：仅 MSIX**（决策 3）。
4. ~~取色器是否扩展为独立取色历史窗口~~ → **已裁决：扩展为独立窗口，两平台同步交付**（决策 4，规格见 `docs/specs/color-history-window.md`）。

---

## 附录 A：默认快捷键（Windows）

| 动作 | 默认 | macOS 对照 |
|------|------|-----------|
| 区域截图 | Ctrl+Alt+A | Cmd+Shift+4 |
| 全屏截图 | Ctrl+Alt+F | Cmd+Shift+3 |
| 翻译模式 | Ctrl+Alt+T | Cmd+Shift+T |
| 划词翻译 | Ctrl+Alt+D | Cmd+Shift+Y |
| 翻译并插入 | Ctrl+Alt+I | Cmd+Shift+I |

## 附录 B：目录结构（目标态）

```
windows/
├── TransFrame.sln
├── Core/            # TransFrame.Core（模型+纯算法，平台无关）
├── NativeShell/     # Win32/D2D/WGC/热键/托盘/UIA 静态库
├── App/             # WinUI 3 应用（C++/WinRT）
├── Tests/           # Catch2 单测 + 跨平台快照 + perf 基准
├── Installer/       # MSIX 打包 + .appinstaller + 签名
└── docs/            # 平台补充文档
```

## 附录 C：验收快照（发布前勾选）

- [ ] §2 基线表 P0/P1 全绿（差异仅 §6.12 列出项）
- [ ] §8 性能预算全达标（CI 报告链接）
- [ ] 双架构 + 双 OS（Win11/Win10 降级）冒烟通过
- [ ] 10 语言截图审校（en/zh-Hans/ja/de 抽查全量，其余 3 页关键屏）
- [ ] 无障碍清单（键盘全路径 + 对比度 + UIA 检查器验证）
- [ ] 隐私面板内容与实际网络行为一致（抓包审计）
