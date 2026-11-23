# WindowDetector 设计说明（EnumWindows + DWM）

## API 选择理由（对齐 spec: window-detection-auto-select.md）

spec 选择 `CGWindowListCopyWindowInfo` 的三条理由在 Windows 上的等价物：

| spec 理由（macOS） | Windows 等价 |
|---|---|
| 同步调用，适合 mouseMoved 高频查询 | `EnumWindows` + DWM 属性读取全部同步 |
| 返回 bounds/layer/ownerName 完整信息 | `DwmGetWindowAttribute` + `GetWindowTextW` + 进程映像名 |
| 无需额外权限 | `EnumWindows`/DWM 读取零权限 |

不用 `WindowFromPoint` 的原因：它只返回 HWND，拿不到“排除阴影的可见边界”，
且对子窗口/分层窗口语义与我们需要的“第一个包含点的可见顶层窗口”不同。

## DWM 两个关键属性

- **`DWMWA_EXTENDED_FRAME_BOUNDS`**：可见窗口边界，排除不可见的 7px 调整边框
  与投影阴影。GetWindowRect 在 Win10/11 上会多算最多 7px/边——FR-02 验收
  “捕获边界与 DWM 可见边一致”因此必须用 DWM bounds。测试
  `DWM extended frame bounds exclude the invisible resize border` 固化了
  这个差异用例。DWM 属性读取失败（无 DWM 会话）时回退 `GetWindowRect`。
- **`DWMWA_CLOAKED`**：过滤挂起的 UWP 窗口与虚拟桌面隐藏窗口——这些窗口
  `IsWindowVisible=TRUE` 但用户看不到，必须排除（spec 的 cloaked 过滤）。

## 过滤规则（filterVisibleWindows，纯函数）

1. `isCloaked` → 丢弃（UWP 挂起/隐藏虚拟桌面）
2. `alpha <= 0` → 丢弃（完全透明分层窗口，`GetLayeredWindowAttributes`）
3. `frame.empty()` → 丢弃（零尺寸）
4. WS_CHILD 顶层孤儿 / WS_EX_TOOLWINDOW / WS_EX_NOACTIVATE（自身覆盖层）→ 枚举期丢弃
5. **保留**浏览器子窗口类：EnumWindows 只走顶层 HWND；浏览器渲染子 HWND 属
   顶层窗口的 DWM bounds 内，FR-02 的“子窗口可选”由 DWM bounds 命中顶层达成。

## 悬停查询 ≤1ms：快照缓存

spec 2.1 的缓存策略一比一移植：

- `refreshSnapshot()` 在 **mouseDown** 时调用一次（窗口在覆盖层操作期间不会移动）
- `windowUnderPoint()` 在 mouseMoved 高频路径 **只做内存 hit-test**：
  过滤后的列表顺序遍历第一个包含点的窗口——纯内存比较，微秒级，远低于 1ms 预算
- 无快照时首次查询自动建立（惰性），`invalidate()` 在覆盖层销毁时清理

测试 `detector snapshot cache` 用共享向量模拟“mouse-down 后窗口关闭”，
验证 move 路径用缓存、refresh 路径感知变化。

## Z 顺序

`EnumWindows` 天然按 Z 序（顶层在前）返回，等价
`CGWindowListCopyWindowInfo(.optionOnScreenOnly)`；hit-test 取第一个命中即
最顶层窗口（测试 `topmost candidate even with overlap`）。
