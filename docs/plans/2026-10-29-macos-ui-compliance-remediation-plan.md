# Plan：macOS 26 HIG / Liquid Glass 合规修复

- 日期：2026-10-29
- 依据：`docs/specs/2026-10-29-macos-ui-compliance-audit-spec.md` 的扫描结果（grep 实测数据，非估计）
- 基准：deployment target 26.0，Liquid Glass 代 HIG
- 原则：每批独立可交付、可回滚；先用户可见 bug，再平台规范，再统一性。批次间无强依赖，但建议按序做——P0 批次最便宜且收益直接。

## 审计发现摘要（实测）

| 类别 | 数据 |
|---|---|
| glassEffect | 全项目 0 处；DesignSystem.swift 自称 "Liquid Glass" 实为 opacity 填充+细描边 |
| DesignSystem 采用率 | Features/ 内仅 1 处引用（基本死代码） |
| 圆角值 | 9 种（2/3/4/5/6/8/9/10/12）共 58 处，无 token 化 |
| 固定字号 | 50 处（8~60pt），不用语义字体，无 Dynamic Type |
| buttonStyle | .plain ×53 泛滥；role: .destructive 仅 3 处 |
| foregroundColor | 41 处弃用 API，与 186 处 foregroundStyle 混用 |
| 本地化机制 | NSLocalizedString 327 + String(localized:) 199 + 自定义 localized() 179，三套并存 |
| 硬编码文案 | EngineSettingsTab ×6、PinnedWindow ×6（"编辑"+5 条中文 tooltip）、TranslationPopoverView.swift:270 "Copy"、BilingualResultView.swift:54 "Close (Esc)" |
| ForEach(id: \.offset) | ×1：MultiEngineSettingsSection.swift:228 可变长引擎列表，增删/换行时状态错位（P0） |
| 手绘控件 | segmented 胶囊 ×3（VLM 提供商 / API 模式 / 引擎选择模式）、目标语言手绘 Menu+手绘 checkmark ×2（LanguageSettingsTab）、Toggle `.switch`/`.checkbox` 混用 ×2 |
| frameAutosaveName | 0 处；Settings/History 每次 center() |
| 窗口级别 | Settings、History 均 .floating（应为 .normal） |
| 菜单快捷键 | `o`、`h` 裸字母无 ⌘ 修饰 |
| NSAlert | 9 处与 SwiftUI .alert 9 处混用 |
| 空标题 Picker/Text("") | ≥9 处，VoiceOver 读不到字段名 |

---

## 批次 1：P0 用户可见 bug（半天）

### 1.1 硬编码文案漏翻
- `Features/Settings/EngineSettingsTab.swift:76,127,154,218,244,249`（"API 模式"、"（可选，为空则免鉴权请求）"、"选择常用本地端点预设"、"保存配置"、"已保存"、"（有未保存更改）"）
- `Features/Pinned/PinnedWindow.swift:369,378,393,403,419,431`（"编辑"按钮 + 5 条中文 tooltip）
- `Features/Overlay/TranslationPopoverView.swift:270`（硬编码英文 "Copy"）
- `Features/BilingualResult/BilingualResultView.swift:54`（硬编码英文 "Close (Esc)"）
- 做法：迁入现有 localized() 机制 + 10 个 .lproj 的 Localizable.strings 补 key
- 验证：`grep -rnE 'Text\("[^"]*[一-鿿]'` 与 tooltip 硬编码 grep 归零；切英文语言冒烟设置页、钉图、翻译气泡

### 1.3 ForEach(id: \.offset) 可变列表（数据-视图错位）
- `Features/Settings/MultiEngineSettingsSection.swift:228`：quickSwitch 引擎排序列表用 `id: \.offset`，增删/换行时行状态错位
- 做法：改 `id: \.self`（TranslationEngineType: Hashable）或行内稳定 id
- 验证：快速切换模式下增删引擎、上下移位，行内容与数据一一对应

### 1.2 空标题控件（可访问性）
- `Picker("")` ×6：GeneralSettingsTab:225、EngineSettingsTab:348、MultiEngineSettingsSection:114,166,831 等
- `Text("")` ×3：EngineSettingsTab:412、EngineConfigSheet:287、CompatibleEngineConfigSheet:92
- ColorPicker("") AdvancedSettingsTab:77
- 做法：给 label 或 `.accessibilityLabel`，视觉上保留 labelsHidden 布局
- 验证：VoiceOver 逐控件朗读字段名（或 Accessibility Inspector）

## 批次 2：P1 平台规范（1.5 天）

### 2.1 窗口行为
- Settings/History `window.level = .floating` → `.normal`：SettingsWindowController.swift:85、HistoryWindowController.swift:61
- 加 `frameAutosaveName`（全项目 0 处），去掉每次 `center()`：SettingsWindowController.swift:80、HistoryWindowController.swift:60 及其余 5 处 center()
- 验证：移动窗口→关闭→重开，位置保持；Settings 不再压在其他 app 上

### 2.2 菜单快捷键
- `keyEquivalent: "o"`（:121）、`"h"`（:133）补 `modifierMask: [.command]`——MenuBarController.swift
- 注意：`⌘H` 与系统"隐藏"冲突，History 建议改 `⌘⇧H`（.command + .shift）或换键
- 验证：菜单栏显示 ⌘O / ⌘⇧H；按 ⌘H 仍是隐藏当前 app

### 2.3 设置窗口去 iOS 化
- `SettingsView.swift:26`：`.system(size: 24, weight: .bold, design: .rounded)` 大标题 → 删掉自定义标题栏，macOS 设置无大标题模式；`.rounded` design 不做界面标题
- 侧栏图标 `tab.color` 多彩着色 → 单色（SettingsView.swift:16-18 的 `.foregroundStyle(tab.color)` 移除）
- 分组容器逐步迁 `Form` + `.formStyle(.grouped)`（先做 General tab 一个，验证手感再铺开，避免一次性大改）

### 2.6 手绘控件换系统控件
- segmented 胶囊 ×3 → `Picker().pickerStyle(.segmented)`：VLM 提供商（EngineSettingsTab.swift:47-66）、OpenAI API 模式（:81-100）、引擎选择模式（MultiEngineSettingsSection.swift:57-80）；手绘版无 hover/焦点环/键盘态/disabled 态
- 目标语言手绘 Menu+手绘 checkmark ×2（LanguageSettingsTab.swift:78-117,155-194）→ 标准 `Picker(.menu)`，系统 Menu 自带勾选当前项
- Toggle 风格统一：EngineConfigSheet.swift:61 `.switch` vs EngineSettingsTab.swift:372-373 `.checkbox`，设置窗口内统一 `.switch`（或按 macOS 26 规范统一 checkbox，二选一批次内一致）
- 危险按钮：AdvancedSettingsTab.swift:18-29 手绘红底按钮 → `Button(role: .destructive).buttonStyle(.borderedProminent)`
- History 分类过滤胶囊（HistoryView.swift:155-175）→ 系统控件或保留手绘但与 3.1 token 对齐

### 2.4 玻璃系统二选一（决策点）
- 现状：DesignSystem.swift(116 行) 自称 Liquid Glass，实为 `Color.white.opacity(0.18)` 假玻璃，且几乎无人用
- 方案 A（推荐）：删除 DesignSystem.swift 死代码，浮层用真 `glassEffect()` / NSVisualEffectView 材质重写关键 3 处（SelectionInPlaceToolbar、BilingualResult 浮层、PreviewToolBar）
- 方案 B：保留手绘体系，仅改名去 "Liquid Glass" 字样并接材质 API
- 验证：真机看浮层透blur效果；DesignSystem.swift 删除后 `xcodebuild` 编译过

### 2.5 NSAlert 收敛
- 9 处 NSAlert.runModal 与 9 处 SwiftUI .alert 并存：列出清单，确认类（阻塞错误）保留 AppKit，简单确认迁 SwiftUI 统一
- 验证：触发路径冒烟，行为不变

### 2.7 翻译气泡（TranslationPopoverView）重写评估
- 现状：全文件 NSView `draw()` 手绘背景/描边/文字/按钮（TranslationPopoverView.swift:108-288）；文字不可选、无 Dynamic Type、无 VoiceOver、按钮无 hover 态只换指针、`sizeThatFits` 手算不换行（长文本撑爆 500px 上限，:99-101）
- 做法：重写为 SwiftUI + material + 标准按钮（文本可选、语义字体、accessibility）；属较大重构，先做不动布局的最小版（文字可选 + 换行修复 + Copy 按钮系统化），完整重写排后续版本
- 验证：长文本换行不溢出；Esc/复制行为不变；VoiceOver 可读

## 批次 3：P2 统一性（2 天，可与批次 2 并行）

### 3.1 圆角 token 化
- 9 种值收敛为 3 档：`radiusS=4 / radiusM=8 / radiusL=12`（沿用多数派 8 的现有手感）
- 58 处逐一替换；直方图复扫归 3 档
- 验证：`grep -rhoE 'cornerRadius:?\(?[0-9.]+' | sort | uniq -c` 仅剩 3 档 + 白名单（如圆形=height/2）

### 3.2 字体语义化
- 50 处固定字号 → `.caption/.body/.title3` 等语义字体；特例白名单（画布内绘制、水印等非 UI 文本可保留 pt）
- 验证：固定字号 grep 归零（白名单注释豁免）；系统字号调大后 UI 跟随

### 3.3 弃用 API 与 destructive
- 41 处 `foregroundColor` → `foregroundStyle`（机械替换 + 编译）
- 危险操作补 `role: .destructive`（清历史、删配置、重置类按钮，扫 `.plain` 53 处中的删除语义）
- 验证：编译零 warning（foregroundColor）；危险按钮红色系统样式

### 3.4 本地化机制收敛（最大单项，可单独排期）
- 三套并存：NSLocalizedString(327) / String(localized:)(199) / 自定义 localized()(179)
- 目标：统一到一套（建议 String(localized:)，代码量最少）；自定义 localized() 改为薄包装避免 327 处全重写
- 验证：10 语言 build 无 missing key warning；grep 三套计数只剩一套+包装

## 批次 4：P3 细节（0.5 天）

- 状态色：文字直接着色当状态色 → 图标着色文字保持 label 色（扫 `foregroundStyle(.green/.red/.yellow)` 于 Text）
- SF Symbol 语义复查：`power` 做 Quit（:198 用 `power`，语义尚可但系统惯例用 `power`；`photo.badge.plus` 等 20 处菜单图标按系统菜单极简原则酌情删）
- Toast/复制反馈：各窗口自造的统一为一个 helper

## 排序与交付

1. 批次 1 立即（用户可见，风险低）
2. 批次 2.1/2.2/2.3（平台规范，改动面小）
3. 批次 2.4 决策点需用户拍板（A/B）
4. 批次 3 逐项独立 PR，3.4 可拖后单独排期
5. 批次 4 收尾；2.6/2.7 视批次 2 落地手感排期

每批交付物：代码 + `npm run test`（TransFrameTests 对应批次补断言，如本地化 key 存在性、菜单快捷键修饰符）+ 冒烟清单。

## 附录：完整问题清单（源码基线 9a23725）

### P0 用户可见 bug
| # | 问题 | 位置 |
|---|---|---|
| 1 | 不可见的模型下拉按钮：`Text("").frame(width: 8, height: 12)` 空热区 | EngineSettingsTab.swift:412、EngineConfigSheet.swift:287、CompatibleEngineConfigSheet.swift:93 |
| 2 | 硬编码中文/英文 UI 文案（漏翻） | EngineSettingsTab.swift:76,127,154,218,244,249；PinnedWindow.swift:369,378,393,403,419,431；TranslationPopoverView.swift:270；BilingualResultView.swift:54 |
| 3 | `ForEach(..., id: \.offset)` 用于可变长引擎列表，状态错位 | MultiEngineSettingsSection.swift:228 |

### P1 违反平台规范（macOS 26 Liquid Glass）
| # | 问题 | 位置 |
|---|---|---|
| 4 | 自称 "Liquid Glass" 实为不透明填充+描边，全项目 glassEffect 0 处 | DesignSystem.swift:69-80；调用方 GeneralSettingsTab.swift:14,23 等 5 处 |
| 5 | 手绘卡片体系，未用 Form+formStyle(.grouped) | Settings 全部 6 个 tab |
| 6 | 设置窗口 iOS 式大标题（24pt bold rounded）、.floating 级别、无 frameAutosaveName 每次 center() | SettingsView.swift:26-33、SettingsWindowController.swift:80,85；HistoryWindowController.swift:60-61 |
| 7 | 菜单快捷键裸字母无 ⌘（o/h）；菜单项全部配 SF Symbol 图标过重 | MenuBarController.swift:121,133 及全部 imageName |
| 8 | 手绘 segmented/Menu/Toggle/危险按钮，无 hover/焦点环/键盘/disabled 态 | EngineSettingsTab.swift:47-66,81-100,372-373；MultiEngineSettingsSection.swift:57-80；LanguageSettingsTab.swift:78-117,155-194；EngineConfigSheet.swift:61；AdvancedSettingsTab.swift:18-29；HistoryView.swift:155-175 |
| 9 | NSView draw() 手绘翻译气泡：文字不可选、不换行、无 a11y | TranslationPopoverView.swift 全文件 |

### P2 不统一
| # | 问题 | 位置 |
|---|---|---|
| 10 | 圆角 9 种值（2/3/4/5/6/8/9/10/12）无 token；同类卡片跨 tab 圆角 24 vs 8 跳变 | 全 Features；DesignSystem.Radii 定义了几乎无人用 |
| 11 | 固定字号 15 种（8–60pt）不用语义字体，无 Dynamic Type | 全 Features（10/11/12pt 占多数） |
| 12 | 侧栏六彩图标（iOS 设置模式） | SettingsTab.swift:30-39、SettingsView.swift:16-18 |
| 13 | 文字当状态色着色（应图标着色）；自绘 Color.blue.opacity 高亮卡 | TextTranslationPopupView.swift:143-147,123；EngineSettingsTab.swift:258-262 |
| 14 | 复制反馈 4 种自造（整窗 alpha 闪/图标打勾/手绘 toast/flash） | TranslationPopoverView.swift:372-386；PinnedWindow.swift:406-415；BilingualResultView.swift:166-180；InPlaceResultsPanel.swift:72-78 |
| 15 | BilingualResult 系统交通灯+自绘关闭钮+Esc 三条关闭路径打架 | BilingualResultWindowController.swift:74-80 + BilingualResultView.swift:44-55 |
| 16 | .plain 按钮 53 处；foregroundColor(弃用)41 处与 foregroundStyle 186 处混用；`folder` 符号两义 | 全 Features；GeneralSettingsTab.swift:59,204 |
| 17 | 本地化机制四套并存（NSLocalizedString 327 / String(localized:) 199 / 自定义 localized() 179 / 硬编码） | 全项目 |

### P3 细节
| # | 问题 | 位置 |
|---|---|---|
| 18 | 空标题 Picker/Text + labelsHidden，VoiceOver 读不到字段名 | GeneralSettingsTab.swift:225 等 ≥9 处 |
| 19 | NSAlert.runModal() 阻塞确认与 SwiftUI .alert 混用 | HistoryView.swift:98-116 |
| 20 | 菜单栏图标 accessibilityDescription 固定 "TransFrame" 不本地化 | MenuBarController.swift:48 |
| 21 | DesignSystem 死代码（MeshGradientView/macOS26IconGlow/meshColors 0 引用） | DesignSystem.swift |
| 22 | Onboarding 进度点/特性行手绘蓝绿着色 | OnboardingView.swift:58-77、OnboardingComponents.swift:12,35 |

### 未深读声明

SelectionOverlayWindow.swift(2143 行)、SettingsViewModel.swift(1333 行)、Annotation.swift(1214 行)、PaddleOCREngine.swift(1068 行) 及 OpenAIVLMProvider 等 Services 层仅 grep 覆盖未精读；纯逻辑/引擎文件与 UI 规范无关，深读价值低，未读不构成漏报。
