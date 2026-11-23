# HotkeyManager + TrayIcon 设计说明（FR-33 / FR-34）

## HotkeyManager（RegisterHotKey / WM_HOTKEY ×5）

### API 选择理由

`RegisterHotKey` 是 Windows 系统级全局热键唯一正解：不依赖焦点、不注入、
与 macOS Carbon `RegisterEventHotKey` 语义一一对应。**注册失败即冲突**——
RegisterHotKey 对已被占用的组合返回 FALSE，这正好是 FR-33 要求的冲突信号，
无需额外检测层。

### 结构

Win32 调用隔离在 `IHotkeyRegistrar`（src/HotkeyManagerWin32.cpp，
`MOD_NOREPEAT` 防按住重复触发）；策略层（注册生命周期、冲突上报、
去重、重注册）纯逻辑可测（12 个用例）。

- **5 组动作**：FullScreen/Region/Translate/TextSelection/TranslateInsert，
  稳定 id = `HotkeyAction` 枚举序（WM_HOTKEY wParam 直接映射）
- **默认键位**（PRD 附录 A）：Ctrl+Alt+F/A/T/D/I（Win+Shift+S 留给系统截图）
- **校验**：非零 VK + 至少一个修饰键（与 macOS 版规则一致，`isHotkeySpecValid`）
- **运行时重注册**：`applySpecs()` 先 `unregisterAll()` 再全量注册——
  设置页改键即时生效，无需重启；测试验证旧槽位完全释放
- **冲突上报**：失败组合进 `report.conflicts`（action+vk+mods+用户可读
  message），设置页标红 + 托盘气泡由 App 层消费该报告

### WM_HOTKEY 泵

热键绑定在主 UI 线程消息队列（`RegisterHotKey(nullptr, id, ...)`），
消息循环里 `pumpWmHotkey(&msg, manager)` 把 wParam 转发为动作回调。

## TrayIcon（Shell_NotifyIcon）

### API 选择理由

托盘常驻唯一原生路径：`Shell_NotifyIcon(NIM_ADD)`。MSIX 打包下任务栏图标
自动合规（不占任务栏按钮，PRD FR-34）。

### 菜单模型（可测，buildTrayMenu 纯函数）

```
全屏截图 | 区域截图 | 翻译模式 | 划词翻译 | 翻译并插入   <- 本任务接线
截图 1..n（最近 ≤5，缩略图位）                     <- 历史任务供给数据
置顶管理 | 历史记录 | 设置 | 检查更新（占位，disabled） <- 任务 C/D 接线
退出
```

- 最近截图条目带 `recentIndex`（0=最新）与 `thumbnailHint`（历史缩略图路径），
  供给方为任务 C 的 HistoryStore；当前 disabled 占位不阻断
- `TrackPopupMenuEx(TPM_RETURNCMD)` 同步取选择，`SetForegroundWindow`
  保证点击外部可关闭（Shell_NotifyIcon 契约要求）
- 命令枚举 `TrayCommand` 单一出口，App 层一个 handler 分发全菜单

### 已知限制

- 托盘图标当前用系统占位图标（`LoadIcon(IDI_APPLICATION)`），相机镜头
  图标资产属打包任务
- 缩略图渲染（HBITMAP 进菜单）等历史数据层就绪后接入；菜单模型已留位
