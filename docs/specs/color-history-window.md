# Spec: 独立取色历史窗口（macOS + Windows 同步交付）

- **Status**: Approved（决策 4，2026-11-02）
- **Parent PRD**: `tasks/prd-windows-transframe.md`（FR-41 / US-W26）；macOS 侧对应新增 US
- **Scope**: 取色器拾取的颜色以独立窗口的形式提供历史浏览、复制、收藏、删除、清空

## 1. 概述

当前取色器拾取的颜色只作为 `HistoryCategory.color` 条目混入翻译历史列表，色块小、入口深、缺少专属交互。本特性新增一个**独立的取色历史窗口**：以大色块网格为主体的专属视图，单击复制 HEX，支持收藏、删除、搜索与清空。macOS 与 Windows **同一交互规格、同步交付**。

## 2. 动机

- 设计/开发用户高频取色，需要在旧颜色间快速回看与复制。
- 通用历史列表对 color 条目的呈现（小缩略图 + 文本行）无法体现"颜色"这一主体信息。
- 两平台规格统一，避免后续维护两套交互心智。

## 3. 数据模型（两平台一致，零迁移成本）

复用现有 `TranslationHistory` / history `color` 类目条目，**不新增存储表**：

| 字段 | 取值约定 |
|------|----------|
| `category` | `.color` / `HistoryCategory.color` |
| `translatedText` | HEX，如 `#1A2B3C`（大写，6 位） |
| `sourceText` | 固定文案 "Color Picked"（沿用现状，见 `HistoryStore.swift` addColor） |
| `sourceLanguage` | `"RGB Hex"`（沿用现状） |
| `thumbnailData` | 不使用（色块由 HEX 直接渲染，无需缩略图） |

- 条目容量上限、自动清理策略沿用历史设置（`maxHistoryEntries` / Windows 侧 SQLite 同语义）。
- Windows 侧为 SQLite 存储（PRD 决策 2：LIKE 检索），`meta_json` 预留 RGB/取色位置扩展位。

## 4. 交互规格（唯一权威定义，两平台 1:1）

### 4.1 窗口

- 独立窗口（非弹层），标题"取色历史 / Color History"。
- 默认尺寸 560×420，可缩放，记住位置与尺寸。
- 入口 ×2：托盘菜单（"取色历史"项，置于"翻译历史"之后）；取色器完成一次取色后的悬浮提示中"查看历史"链接。
- 深浅色跟随系统；macOS 用 NSVisualEffectView 侧边栏材质，Windows 用 Mica（PRD FR-50.1）。

### 4.2 主体网格

- 色块网格：自适应列（每格 ≥ 48×48 逻辑像素），色块下方显示 HEX（等宽字体）。
- 悬浮：显示完整 HEX/RGB/取色时间 tooltip；右上角出现操作按钮（复制 / 收藏 / 删除）。
- 收藏条目：色块右上角常显星标。
- 空态：居中插图 + "还没有取色记录，使用取色器开始" + 取色快捷键提示。

### 4.3 顶栏操作

| 操作 | 行为 |
|------|------|
| 搜索框 | 按 HEX 片段过滤（大小写不敏感，如 `1A2`）；空串 = 全部 |
| 仅收藏过滤 | 切换按钮，与搜索叠加生效 |
| 复制格式切换 | HEX（默认）/ RGB，记住选择 |
| 清空全部 | 二次确认对话框；仅清空 color 类目，不影响其他历史 |

### 4.4 单条操作

| 操作 | 行为 |
|------|------|
| 单击色块 | 按当前复制格式写入剪贴板，色块上出现"已复制"浮标（1s 消失） |
| 右键 / 悬浮菜单 | 复制 HEX / 复制 RGB / 收藏（取消收藏）/ 删除 |
| 键盘 | Tab 网格导航；Enter = 复制；Space = 收藏切换；Delete = 删除；Esc = 清空搜索 |

### 4.5 与取色器的联动

- 取色器完成取色后，窗口若打开则实时插入新条目（置顶，带高亮动画）。
- 取色器悬浮提示保留"加入历史"现状逻辑不变。

## 5. 技术设计

### 5.1 macOS

- 新增 `Features/History/ColorHistoryWindow.swift` + `ColorHistoryView.swift`（SwiftUI）。
- `HistoryStore` 新增过滤 API：`colorEntries(matching:favoritesOnly:) -> [TranslationHistory]`；`@Published` 驱动实时插入。
- 复制：`ClipboardService` 现有能力；收藏/删除复用 `HistoryStore.toggleFavorite / removeEntry`。

### 5.2 Windows

- `App/Views/ColorHistoryPage`（WinUI 3）：`GridView`（`ItemsRepeater` 或 `GridView`）+ DataTemplate（色块 + HEX TextBlock）。
- 数据：`HistoryStore`（SQLite, LIKE 检索）同 API 形状 `colorEntries(matching, favoritesOnly)`。
- 复制反馈：`TeachingTip` 或就地浮层；清空确认用 `ContentDialog`。
- 无障碍：色块 AutomationPeer 名称 = HEX；键盘路径按 §4.4。

### 5.3 共享验收测试向量

- HEX 格式化（小写输入 → 大写展示、3 位扩展为 6 位）。
- 搜索过滤（`1a2b` → `#1A2B3C` 命中）。
- 容量上限：51 条取色 → 最旧 1 条被清理。

## 6. 用户故事与验收

**US-CH1** 取色后我在独立窗口看到新条目置顶出现。（打开窗口 → 取色 → 1s 内插入并高亮）
**US-CH2** 单击色块复制 HEX 并有即时反馈。（粘贴到记事本 == 色块 HEX）
**US-CH3** 我能只看收藏的颜色。（过滤后全部带星标）
**US-CH4** 我能按片段搜索颜色。（`1A2` 命中 `#1A2B3C`）
**US-CH5** 清空只清颜色，不动翻译/截图历史。（清空后其他类目条数不变）
**US-CH6** 键盘完整操作路径可达所有功能。（不碰鼠标完成复制/收藏/删除/搜索清空）
**US-CH7** 两平台同一测试脚本逐项通过（共享验收清单）。

## 7. 非目标

- 不做取色屏幕坐标回放/定位。
- 不做颜色格式转换器（HSL/CMYK）编辑器。
- 不做跨设备同步。

## 8. 交付与排期

- 归属 P2；纳入 PRD US-W26（Windows）与 macOS 侧对位 US，同版本发布（M5 或 macOS 下一 minor 版本）。
- 两平台共用本 spec 的 §4 交互规格作为验收基线。
