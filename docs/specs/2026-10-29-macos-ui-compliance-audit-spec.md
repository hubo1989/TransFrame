# Spec：macOS 26 HIG / Liquid Glass UI 合规审计（TransFrame）

- 日期：2026-10-29
- 分支：hubo/ui-macos-spec-audit
- 性质：只读审计 → 产出修复计划。本 spec 不改产品行为。
- 基准：`MACOSX_DEPLOYMENT_TARGET = 26.0`（Liquid Glass 代），按当前代 HIG 判罪。
- 产物：`docs/plans/2026-10-29-macos-ui-compliance-remediation-plan.md`（见同批提交；附完整 P0–P3 问题清单附录）
- 实机验证：未做（CUA 运行时审批列表拒绝 TransFrame，`Computer Use was not approved to use TransFrame`）。纯源码审计，报告开头已声明。

## 1. 范围与输入

- 审计对象：`TransFrame/`（App+Features+Services+Models+Utilities），SwiftUI 为主、AppKit 桥接窗口。
- UI 面盘点：Features 17 个模块（About/Annotations/BilingualResult/Capture/History/MenuBar/Onboarding/Overlay/Pinned/Preview/Settings/TextTranslation/TranslationFlow），Swift 源码约 5 万行。
- 精读 Top 20 大文件，其余 grep 扫描覆盖；未深读文件在报告末尾声明。

## 2. 审计维度（判罪清单）

按 macos-ui-compliance-scan skill 的 P0–P3 分级：

- **P0 用户可见 bug**：不可见控件（`Text("")` 8×12 空热区 ×3）、硬编码文案漏翻 ×14 处、`ForEach(id: \.offset)` 可变列表 ×1、数据-视图错位。
- **P1 违反平台规范**：自称玻璃实为不透明填充（全项目 `glassEffect` 0 处）；手绘卡片/segmented/开关而不用 `Form`+`.formStyle(.grouped)` 与系统控件；设置窗口 iOS 式大标题、`.floating` 级别、不记窗口位置；菜单栏快捷键无 ⌘ 修饰；NSAlert 与 SwiftUI alert 混用。
- **P2 不统一**：同类元素圆角/字号/风格各写各的；`.plain` buttonStyle 泛滥、缺 `role: .destructive`；四套本地化机制并存；`foregroundColor`（弃用）与 `foregroundStyle` 混用；同一反馈（toast/复制）各窗口自造。
- **P3 细节**：文字直接着色当状态色；SF Symbol 语义混乱；空标题 Picker；可访问性缺失；DesignSystem 死代码。

## 3. 扫描面（批量 grep）

| 维度 | 模式 | 输出 |
|---|---|---|
| 圆角 | `cornerRadius\(([0-9]+)` / `RoundedRectangle(cornerRadius: ([0-9]+)` | 直方图 |
| 固定字号 | `font(.system(size:` | 列表 |
| 控件风格 | `buttonStyle(\.[a-z]+)` | 计数 |
| 材质 | `\.(thin|regular|thick|ultraThin|bar)Material` / `glassEffect` | 计数 |
| 弃用 API | `foregroundColor` vs `foregroundStyle` | 计数 |
| 本地化 | NSLocalizedString / localized( / String(localized: / 硬编码中文 | 四套机制对照 |
| 窗口行为 | `styleMask`、`level = `、`frameAutosaveName`、`center()` | 逐窗口 |
| 菜单 | `keyEquivalent` 是否带 modifierMask | 逐项 |
| 假玻璃 | 名字含 glass/Liquid 的 helper | 读实现验证 |

## 4. 验收标准

- 四级清单每条带 `file:line`，可定位、可复现（grep 命令附在条目里）。
- P0/P1 条目附修复方向（不实现），对应 remediation plan 的任务项。
- 报告末尾声明：未深读文件、未实机验证、判罪基准（HIG 26 代）。

## 5. 不做

- 不改任何代码、不重构、不提交样式修复——那是 remediation plan 的活。
- 不跑 Xcode 构建/测试（审计只读；build 属于修复阶段验证）。
