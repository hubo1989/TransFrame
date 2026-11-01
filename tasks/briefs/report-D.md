# 子任务 D：macOS 取色历史

## 文件

- `TransFrame/Features/History/ColorHistoryView.swift`：网格、筛选、操作、键盘与格式化。
- `TransFrame/Features/History/ColorHistoryWindowController.swift`：窗口管理。
- `TransFrame/Features/History/ColorPickToastController.swift`：取色提示。
- `TransFrame/Features/History/HistoryWindowController.swift`、`TransFrame/Services/HistoryStore.swift`：共享历史实例、颜色筛选和清理。
- `TransFrame/App/AppDelegate.swift`、`TransFrame/Features/MenuBar/MenuBarController.swift`：取色及菜单入口。
- `TransFrame/Resources/{en,zh-Hans,de,es,fr,it,ja,ko,pt,ru}.lproj/Localizable.strings`：本地化。
- `TransFrameTests/ColorHistoryStoreTests.swift`：11 项 XCTest。

## UserDefaults

- `TransFrame.colorHistoryCopyFormat`：复制格式，默认 HEX。
- `TransFrame.colorHistorySearch`：搜索文字。
- AppKit 窗口 frame autosave name：`TransFrameColorHistoryWindow`（系统保存键带 `NSWindow Frame` 前缀）。

## 新增字符串键

`menu.color.history`；`colorHistory.title`、`search.placeholder`、`filter.favoritesOnly`、`copy.format`、`format.hex`、`format.rgb`、`clear.all`、`clear.alert.title`、`clear.alert.message`、`copy.hex`、`copy.rgb`、`favorite`、`unfavorite`、`copied`、`empty.title`、`empty.message`、`no.results.title`、`no.results.message`、`toast.viewHistory`（上述省略前缀的键均以 `colorHistory.` 开头）。

## 验收状态

- US-CH1：新条目置顶由单测覆盖，高亮实现已编译；实际窗口交互未验证。
- US-CH2：复制及反馈已实现；实际剪贴板交互未验证。
- US-CH3：收藏与搜索叠加单测通过；窗口交互未验证。
- US-CH4：大小写不敏感片段搜索单测通过。
- US-CH5：清除仅颜色单测通过；截图类单独验证未运行。
- US-CH6：键盘处理已实现；完整 Tab/Enter/Space/Delete/Esc 路径未做 UI 自动化验证。

## 构建与测试

- `xcodebuild -project TransFrame.xcodeproj -scheme TransFrame -destination 'platform=macOS' build CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY=''`：BUILD SUCCEEDED。
- `xcodebuild test -project TransFrame.xcodeproj -scheme TransFrame -destination 'platform=macOS' -only-testing:TransFrameTests/ColorHistoryStoreTests CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY=''`：TEST SUCCEEDED，11 项通过。
- 10 个 strings 文件 `plutil -lint` 均通过。
- 最终完整测试套件：NOT RUN；命令为 `xcodebuild test -project TransFrame.xcodeproj -scheme TransFrame -destination 'platform=macOS' CODE_SIGNING_ALLOWED=NO CODE_SIGN_IDENTITY=''`。

## 待裁决 / 已知偏差

- 空态未显示取色快捷键；现有托盘取色入口未配置快捷键。
- 预览工具栏的吸管及截图工具栏吸管没有接入历史和 toast；当前仅托盘取色入口接入。
- 色块悬浮操作位置及收藏星标位置与规格右上角要求不同。
- 窗口、toast、键盘路径和新条目高亮尚未进行实际交互验证；焦点与窗口滚动行为需人工或 UI 测试确认。
- 多个存储实例间历史同步仍依赖共享控制器实例；其他独立创建的 `HistoryStore` 可能产生覆盖，需后续统一管理。
