# Brief D — macOS 取色历史独立窗口（FR-41 macOS 侧，与 Windows 同规格）

你是 TransFrame macOS 侧的实现工程师。4 个并行子任务共用同一 PRD；你负责 D：独立取色历史窗口（macOS 实现）。

## 必读上下文（按序）
1. `docs/specs/color-history-window.md` — **唯一权威交互规格**（§3 数据模型、§4 交互、§5.1 macOS 技术设计、§6 验收）
2. `tasks/prd-windows-transframe.md` FR-41/US-W26（Windows 侧对位，规格同源）
3. 现有实现：`TransFrame/Services/HistoryStore.swift`（addColor 条目约定、entries 发布、收藏/删除）、`TransFrame/Features/History/HistoryView.swift` + `HistoryWindowController.swift`（窗口管理惯例）、`TransFrame/Services/ClipboardService.swift`、`TransFrame/Features/Preview/PreviewToolBar.swift`（eyedropperButton 取色入口）、`TransFrame/Features/MenuBar/MenuBarController.swift`（托盘菜单项惯例）

## 交付范围（只做这些，不越界）
1. `TransFrame/Features/History/ColorHistoryView.swift`：
   - 网格色块（每格 ≥48×48pt，自适应列）+ 等宽 HEX 文本
   - 悬浮 tooltip（HEX/RGB/时间）+ 悬浮操作（复制/收藏/删除）；收藏右上角星标常显
   - 空态视图（插图 + 提示 + 取色快捷键）
2. `TransFrame/Features/History/ColorHistoryWindowController.swift`：
   - 独立窗口（默认 560×420，记住位置尺寸，深浅色跟随，NSVisualEffectView 侧栏材质）
   - 单开复用（重复打开聚焦已有窗口）
3. `HistoryStore` 扩展：`colorEntries(matching: String?, favoritesOnly: Bool) -> [TranslationHistory]`（HEX 片段大小写不敏感过滤）；新取色实时置顶插入（窗口打开时高亮动画）
4. 顶栏：搜索框、仅收藏过滤、复制格式切换（HEX 默认/RGB，记住选择）、清空（二次确认，仅 color 类目）
5. 键盘：Tab 网格导航、Enter=复制、Space=收藏、Delete=删除、Esc=清空搜索（spec §4.4）
6. 入口 ×2：MenuBarController 菜单"取色历史"（置于"翻译历史"之后）；取色完成悬浮提示"查看历史"链接（若现有取色完成无悬浮提示，则加一个轻量 toast，1.5s 消失）
7. 测试（XCTest，风格对齐现有 TransFrameTests）：HEX 格式化（小写→大写、3 位→6 位）、搜索过滤、仅收藏叠加、清空仅影响 color 类目、容量上限清理最旧
8. 本地化：所有新字符串进 `Localizable.strings`（10 语言至少 en/zh-Hans 全量，其余语言用英文占位 + 注释待翻译）

## 明确约束
- 不改 Windows 侧任何文件；不动 history 存储结构（零迁移）
- 复制格式切换与搜索状态持久化用现有 UserDefaults 惯例（`TransFrame.` 前缀新键要列在报告里）
- 遵守仓库 CONTRIBUTING/现有代码风格；Swift 6 并发注解对齐

## 完成标准
- `swift build` 或 Xbuild 工程构建通过；XCTest 新增用例全绿（无法在本机跑则标注 NOT RUN + 命令）
- spec §6 US-CH1…CH6 逐条自验收并在报告中给出结果
- 报告：文件清单、新增 UserDefaults 键、字符串键清单、与 spec 的偏差（如有必须为 0 或列为待裁决）
