# 子任务 D：macOS 取色历史（补工收尾版）

> 本版为补工交付：销掉上一版报告的全部"待裁决/已知偏差"项，含全量测试结果。
> 基线：`daed543 feat: add macOS color history window`。

## 文件（累计，含补工改动）

- `TransFrame/Features/History/ColorHistoryView.swift`：网格、筛选、操作、键盘与格式化；补工新增空态动态快捷键提示、右上角星标/操作合并簇。
- `TransFrame/Features/History/ColorHistoryWindowController.swift`：窗口管理（单实例、共享 store）。
- `TransFrame/Features/History/ColorPickToastController.swift`：取色提示（"查看历史"链接）。
- `TransFrame/Features/History/ColorPickFlow.swift`（新增）：全部取色面的共享入史+toast 管道。
- `TransFrame/Features/History/HistoryWindowController.swift`、`TransFrame/Services/HistoryStore.swift`：共享历史实例；补工抽出 `makeColorPickEntry` / `recordPickedColor` 统一入口。
- `TransFrame/App/AppDelegate.swift`：`pickScreenColor` 改走 `ColorPickFlow.complete`。
- `TransFrame/App/Coordinators/HotkeyCoordinator.swift`：新增 `.pickColor` 热键注册。
- `TransFrame/Features/MenuBar/MenuBarController.swift`：托盘"屏幕取色"项显示可配置热键。
- `TransFrame/Features/Preview/PreviewToolBar.swift`、`TransFrame/Features/Capture/SelectionInPlaceToolbar.swift`：两个吸管接入共享管道。
- `TransFrame/Models/KeyboardShortcut.swift`：`pickColorDefault`（Ctrl+Cmd+C）。
- `TransFrame/Models/AppSettings.swift`：`pickColorShortcut` 键/加载/重置。
- `TransFrame/Features/Settings/SettingsViewModel.swift`、`ShortcutSettingsTab.swift`：第 6 组热键录制 UI（含冲突检测）。
- `TransFrame/Resources/{en,zh-Hans,de,es,fr,it,ja,ko,pt,ru}.lproj/Localizable.strings`。
- `TransFrameTests/ColorHistoryStoreTests.swift`（+4 用例）、`TransFrameTests/ShortcutRecordingTypeTests.swift`（用例扩展到 6 案）。

## UserDefaults（累计）

- `TransFrame.colorHistoryCopyFormat`：复制格式，默认 HEX。
- `TransFrame.colorHistorySearch`：搜索文字。
- `TransFrame.pickColorShortcut`（新增）：屏幕取色热键，`{keyCode, modifiers}` 字典，默认 Ctrl+Cmd+C。
- AppKit 窗口 frame autosave name：`TransFrameColorHistoryWindow`。

## 新增字符串键（补工新增）

- `settings.shortcut.pick.color`（设置快捷键页第 6 项标签）
- `colorHistory.empty.shortcut`（空态热键提示，`%@` = displayString）
- `colorHistory.empty.trayHint`（热键无效时的托盘路径文案）

en/zh-Hans 全量翻译；其余 8 语言英文占位（置于既有 "TODO: translate" 块，`plutil -lint` 全通过）。

## 逐条销掉旧报告偏差清单

### 1. 空态缺"取色快捷键提示"（spec §4.2）→ 已销

- 新增全局热键"屏幕取色"，默认 **Ctrl+Cmd+C**（`KeyboardShortcut.pickColorDefault`；避开纯 Cmd+C 剪贴板绑定）。
- 完整接入既有 5 组热键的全部惯例：`KeyboardShortcut` 模型默认值 → `AppSettings.Keys.pickColorShortcut` + 加载/重置 → `HotkeyCoordinator.HotkeyType.pickColor` 注册（动作 `appDelegate?.pickScreenColor()`）→ `SettingsViewModel` 录制/重置/冲突检测（第 6 案）→ `ShortcutSettingsTab` 第 6 行 UI → 托盘菜单项 `menu.pick.color` 显示该热键（`shortcut:` 参数，快捷键变更通知触发 rebuildMenu 已有）。
- 空态文案**动态读取** `AppSettings.shared.pickColorShortcut.displayString`，并订阅 `AppSettings.shortcutDidChangeNotification` 实时刷新（`.id(shortcutRefreshID)` 强制重渲染）；`isValid == false` 时回退 `colorHistory.empty.trayHint` 托盘路径文案。

### 2. 吸管未接入历史 + toast（spec §4.5、report-D 偏差）→ 已销

- 新增共享入口 `ColorPickFlow.complete(_:writeToPasteboard:)`（`Features/History/ColorPickFlow.swift`）：hex 规范化 →（可选）剪贴板 → 确保共享 store 存在 → 广播 `.colorDidPick` → `ColorPickToastController.show`（含"查看历史"链接）。
- 三个取色面全部收敛到该入口，无复制粘贴实现：
  - 托盘 `AppDelegate.pickScreenColor`（`writeToPasteboard: true`，与原行为一致）
  - `PreviewToolBar.eyedropperButton`（`false`，注释标注"标注中途不动剪贴板"）
  - `SelectionInPlaceToolbar` 屏幕吸管（`false`，同上）
- 入史统一走 `HistoryStore.recordPickedColor(hex:)`（构建 canonical 条目 + `addEntry` 去重/置顶/收藏保留）；`.colorDidPick` 观察者也改走同一路径（原内联构造删除）。

### 3. 悬浮操作与星标位置（spec §4.2）→ 已销

- 实现方式（在报告说明）：单一 `topTrailingCluster` 覆盖在色块 `.topTrailing`：
  - 静止时只渲染常显星标 `favoriteBadge`（收藏=黄色 star.fill）；
  - 悬浮时星标**合并进操作组左侧**成为其首按钮（star/star.fill 切换），后接复制、删除，整组 `.ultraThinMaterial` 背景。
  - 一个 HStack、一个对齐锚点，星标与操作组**不可能重叠**；操作组原 `bottomTrailing`（旧报告偏离规格）改为右上角。

### 4. HistoryStore 多实例覆盖风险 → 已审计，无风险新增

全仓 `HistoryStore(` 创建点审计：
- 生产代码仅 2 处：`HistoryWindowController.init`（单例所有，唯一真实实例）与 `ColorHistoryWindowController.init` 复用 `HistoryWindowController.shared.store`（前任已改，本次保持）。`AppDelegate.pickScreenColor` 不再直接触碰 store（经 `ColorPickFlow`）。
- `HistoryView.swift` 的 `HistoryStore()` 在 `#if DEBUG #Preview` 内，不进生产二进制。
- 测试目标 2 处（`HistoryStoreTests` / `ColorHistoryStoreTests`）用独立 UserDefaults suite，符合测试隔离惯例。
- 结论：运行期 store 实例唯一，无多实例覆盖风险；无需进一步改动。

### 5. 键盘路径复查（代码级自查，ColorHistoryView）

| 键 | 实现位置 | 分支 |
|---|---|---|
| Tab | `ColorSwatchCell` `.focusable()` + grid `.focused($focusedCellID, equals:)` | 每个 cell 均可获焦，Tab 循环遍历 |
| ←→↑↓ | `.onMoveCommand` → `moveFocus(from:direction:)` | 按 `columnCount`（实测宽度/64+12）换算 stride；左右 clamp 0…count-1，上下 clamp 跨行；`@unknown default` 不动 |
| Enter | `.onKeyPress(.return)` | `onCopy()`（按当前格式）+ `flashCopiedBadge()`，返回 `.handled` |
| Space | `.onKeyPress(.space)` | `onToggleFavorite()`，`.handled` |
| Delete | `.onKeyPress(.delete, phases: .down)` | `onDelete()`，`.handled`；`phases: .down` 保证按下即删（含 Fn+Backspace 正向删除键位） |
| Esc | 搜索框 `onExitCommand`（清空或失焦）+ 窗口级 `onExitCommand`（任意位置清空搜索，已空则无操作） | 两级覆盖，空搜索时不吞事件焦点行为 |

全部 6 条键路径代码级确认完整；完整 UI 自动化仍未做（沿用旧报告口径，非本次范围）。

## 全量验证（本机 macOS 实测）

- **构建**：`xcodebuild -project TransFrame.xcodeproj -scheme TransFrame -destination 'platform=macOS' build CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY=''` → **BUILD SUCCEEDED**（改动文件零新增告警）。
- **全量测试套件**（不只 ColorHistoryStoreTests）：`xcodebuild test -project TransFrame.xcodeproj -scheme TransFrame -destination 'platform=macOS' CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY=''` → **TEST SUCCEEDED，325 个用例全部通过、0 失败、0 跳过**；无与本次改动无关的失败（旧报告担心的全量回归不存在）。xcresult：`/Volumes/external/Xcode/DerivedData/TransFrame-*/Logs/Test/Test-TransFrame-2026.11.02_07-15-54-+0800.xcresult`。
- **ColorHistoryStoreTests**：15/15 通过（11 旧 + 4 新：`testRecordPickedColorInsertsCanonicalEntryAtTop`、`testRecordPickedColorDeduplicatesAndKeepsFavorite`、`testRecordPickedColorBroadcastObserverMatches`、`testPickColorShortcutDefaultIsValidAndDistinct`）。
- **ShortcutRecordingTypeTests**：`testAllCasesExist` 升级为 6 案断言（含 `.pickColor`），`testSwitchExhaustiveness` 同步扩展，均通过。
- **plutil -lint**：10 个 `Localizable.strings` 全部 OK。
- 未验证项（沿用旧口径）：真实窗口内 UI 自动化键盘/点击路径（Key event → NSPanel 转发需真机交互）；Toast 视觉位置。二者不影响上述代码级结论。

## 改动文件清单（本次补工 commit）

```
TransFrame/Features/History/ColorPickFlow.swift          (新增, 52 行)
TransFrame/Features/History/ColorHistoryView.swift       (空态热键提示 + 右上角合并簇)
TransFrame/Services/HistoryStore.swift                   (recordPickedColor/makeColorPickEntry, 观察者改走共享入口)
TransFrame/App/AppDelegate.swift                         (pickScreenColor → ColorPickFlow)
TransFrame/App/Coordinators/HotkeyCoordinator.swift      (.pickColor 注册)
TransFrame/Features/MenuBar/MenuBarController.swift      (取色菜单项显示热键)
TransFrame/Features/Preview/PreviewToolBar.swift         (吸管 → ColorPickFlow)
TransFrame/Features/Capture/SelectionInPlaceToolbar.swift(吸管 → ColorPickFlow)
TransFrame/Models/KeyboardShortcut.swift                 (pickColorDefault)
TransFrame/Models/AppSettings.swift                      (pickColorShortcut 键/加载/重置)
TransFrame/Features/Settings/SettingsViewModel.swift     (第 6 组热键录制)
TransFrame/Features/Settings/ShortcutSettingsTab.swift   (第 6 行 UI)
TransFrame/Resources/*.lproj/Localizable.strings ×10     (3 个新键)
TransFrameTests/ColorHistoryStoreTests.swift             (+4 用例)
TransFrameTests/ShortcutRecordingTypeTests.swift         (6 案扩展)
```
