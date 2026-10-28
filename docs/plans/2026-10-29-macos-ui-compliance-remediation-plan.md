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
| 硬编码中文 | EngineSettingsTab ×6、PinnedWindow ×1（漏翻） |
| frameAutosaveName | 0 处；Settings/History 每次 center() |
| 窗口级别 | Settings、History 均 .floating（应为 .normal） |
| 菜单快捷键 | `o`、`h` 裸字母无 ⌘ 修饰 |
| NSAlert | 9 处与 SwiftUI .alert 9 处混用 |
| 空标题 Picker/Text("") | ≥9 处，VoiceOver 读不到字段名 |

---

## 批次 1：P0 用户可见 bug（半天）

### 1.1 硬编码中文漏翻
- `Features/Settings/EngineSettingsTab.swift:76,127,218,244,249`（"API 模式"、"（可选，为空则免鉴权请求）"、"保存配置"、"已保存"、"（有未保存更改）"）
- `Features/Pinned/PinnedWindow.swift:369`（"编辑"）
- 做法：迁入现有 localized() 机制 + 10 个 .lproj 的 Localizable.strings 补 key
- 验证：`grep -rnE 'Text\("[^"]*[一-鿿]'` 归零；切英文语言冒烟设置页

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

### 2.4 玻璃系统二选一（决策点）
- 现状：DesignSystem.swift(116 行) 自称 Liquid Glass，实为 `Color.white.opacity(0.18)` 假玻璃，且几乎无人用
- 方案 A（推荐）：删除 DesignSystem.swift 死代码，浮层用真 `glassEffect()` / NSVisualEffectView 材质重写关键 3 处（SelectionInPlaceToolbar、BilingualResult 浮层、PreviewToolBar）
- 方案 B：保留手绘体系，仅改名去 "Liquid Glass" 字样并接材质 API
- 验证：真机看浮层透blur效果；DesignSystem.swift 删除后 `xcodebuild` 编译过

### 2.5 NSAlert 收敛
- 9 处 NSAlert.runModal 与 9 处 SwiftUI .alert 并存：列出清单，确认类（阻塞错误）保留 AppKit，简单确认迁 SwiftUI 统一
- 验证：触发路径冒烟，行为不变

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
5. 批次 4 收尾

每批交付物：代码 + `npm run test`（TransFrameTests 对应批次补断言，如本地化 key 存在性、菜单快捷键修饰符）+ 冒烟清单。

## 未深读声明

SelectionOverlayWindow.swift(2143 行)、SettingsViewModel.swift(1333 行)、Annotation.swift(1214 行)、PaddleOCREngine.swift(1068 行) 及 OpenAIVLMProvider 等 Services 层仅 grep 覆盖未精读；纯逻辑/引擎文件与 UI 规范无关，深读价值低，未读不构成漏报。
