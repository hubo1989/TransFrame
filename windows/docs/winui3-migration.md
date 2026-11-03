# WinUI 3 接入评估（FR-35 设置窗口技术选型）

- 结论：**本周期不上 WinUI 3，采用 Win32 自绘过渡**；重评估触发信号见 §4。
- 范围：FR-35 设置窗口最小版（通用页 + 快捷键页）、FR-30 历史列表窗口。

## 1. 现状证据（Observed，仓库内）

- `windows/App/vcxproj 注释块`（`TransFrame.App.vcxproj` 3–7 行注释 + 85–93 行被注释的
  `PackageReference` 块）：WinUI 3 经 Windows App SDK NuGet 接入；库内记录的阻塞点：
  "PackageReference forces /ZW, which is incompatible with /std:c++20 and requires
  TargetPlatformMinVersion plumbing"（/ZW = C++/CX，无法与 /std:c++20 同编译单元）。
- 现有 UI 全部 Win32 自绘/公共控件：ResultWindow（GDI 弹窗）、SelectionOverlay（D2D）、
  托盘（Shell_NotifyIcon）。无 XAML 基础设施，CI 无 NuGet restore 步骤。
- `windows-ci.yml`：`msbuild TransFrame.sln` 直跑；MSVC 是唯一无法在本地（macOS 开发机）
  验证的 CI 腿，给这条腿加工具链维度会放大主线风险。
- Core 库 /W4 /WX 且必须平台无关（macOS clang++ 测试链路，windows/README.md）。

## 2. 上 WinUI 3 的成本清单（本周期）

| 维度 | 成本 |
|---|---|
| 工具链 | Windows App SDK NuGet + XAML 编译器 + MRT Core；sln 首次引入 restore 步骤 |
| 语言 | /ZW 与 /vcxproj 记录的 /std:c++20 冲突；XAML 生成码引入 C++/WinRT 头依赖 |
| 线程模型 | WinUI 3 内容需要 DispatcherQueueController，与现有消息循环桥接 |
| 部署 | WASDK runtime / 自包含 MSIX 体积 + bootstrap 初始化器（PRD 预算 ≤80MB RSS） |
| CI | MSVC 腿首次引入包管理，runner 需 WASDK 工作负载；失败面变大 |
| 收益面 | 本周期 UI 面 = 2 个设置页 + 1 个列表窗口；公共控件完全覆盖 |

## 3. 本周期方案（Win32 过渡）与迁移路径

- 窗口层全部 Win32 公共控件（tab / list-view / combo / edit / button），GDI 文本，
  PMv2 DPI（`GetDpiForWindow` 缩放，模式照抄 ResultWindow）。
- 设置窗口 = Tab 两页（通用 / 快捷键）；历史窗口 = ListView 报表 + 搜索框。
- 数据模型全部在 Core（EngineConfigStore / HotkeySettingsStore / HistoryStore，平台无关，
  Catch2 可测）——窗口层是薄壳，将来换 WinUI 3 时只替换这一层。

## 4. 何时重评估（触发信号）

满足任一条即重评估上 WinUI 3：
1. FR-35 完整版七页 NavigationView（通用/快捷键/标注/引擎/多引擎/语言/高级）立项；
2. FR-37 十语言 .resw 本地化立项（XAML 资源管线顺路解决）；
3. 引擎管理页（10 卡片 + 兼容实例管理）立项；
4. WASDK /ZW 冲突解除（库内记录的阻塞点消失）。

迁移路径：Core 数据模型不动，只替换窗口层；WASDK 走自包含 MSIX（PRD 决策 3 单通道）。
