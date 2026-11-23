# SelectionOverlay 设计说明（全屏覆盖层 · D2D 1.1 + DComp）

## 窗口模型

一个跨全虚拟桌面（`SM_XVIRTUALSCREEN/CXVIRTUALSCREEN`）的窗口：

```
WS_POPUP | WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
```

- `WS_POPUP`：无边框全屏覆盖，鼠标消息直达（FR-01“捕获期间覆盖层接收全部输入”）
- `WS_EX_LAYERED`：与 DComp premultiplied 交换链组合，桌面内容透出
- `WS_EX_TOPMOST`：盖过所有普通窗口（含任务栏）
- `WS_EX_NOACTIVATE`/`WS_EX_TOOLWINDOW`：不抢焦点、不进 Alt-Tab（Esc 用显式
  SetFocus 接管键盘，选择会话结束后归还）

## 渲染栈：Direct2D 1.1 + DirectComposition

```
D3D11 device (BGRA)
  └─ ID2D1Factory2 -> ID2D1Device -> ID2D1DeviceContext (D2D 1.1)
       └─ CreateSwapChainForComposition (flip, 2 buffers, premultiplied)
            └─ IDCompositionDevice -> TargetForHwnd -> Visual -> SetContent(swapchain)
```

- 交换链直接挂 DComp visual，**零 GDI**；每帧 = `BeginDraw → 绘制 → EndDraw
  → Present1 → Commit`，全 GPU 合成。
- 目标位图 `D2D1_BITMAP_OPTIONS_TARGET` 绑定交换链 buffer，重设桌面尺寸时
  `ResizeBuffers` 重建。

## 60ms 热键路径（预算：热键→覆盖层出现 ≤60ms）

**窗口与设备在 App 启动时预创建（`precreate()`），热键路径只做**：

1. `resizeToVirtualDesktop()`（尺寸无变化时 no-op）
2. `renderFrame()`（首帧变暗）
3. `ShowWindow(SW_SHOWNA)` + `SetFocus`

无窗口创建、无设备创建、无工厂创建——1 个 DWM 帧内可见。

## 绘制内容（FR-01/02 语义）

| 元素 | 实现 |
|---|---|
| 40% 变暗 | 全桌面 `FillRectangle(black, α=0.40)` |
| 选区挖亮 | Punch-through：选区内清透明（桌面原亮度透出） |
| 1px 高亮描边 | `DrawRectangle(accent, 1px)`（accent 蓝 0,0.47,0.94） |
| 尺寸徽标 | 选区上方 36px 高度徽标底板（DWrite 文本由 FR-05 任务接） |
| 十字准线 | 全宽/全高 1px 白线（85% α），Idle + Dragging 显示 |
| 窗口高亮 | accent 填充 25% + 1px 描边（FR-02 悬停） |
| 就地工具栏 | `layoutInPlaceToolbar` 8 槽位布局骨架（36px 按钮/4px 间距/边缘翻转），仅矩形占位 |

## 交互状态机（OverlayState，MT-safe 纯逻辑）

```
Idle --mouseDown--> 跟踪开始（记录 anchor，4px 阈值判定点击 vs 拖拽）
Idle --hover--> HoverWindow（窗口高亮 + 十字准线）
拖动超 4px --> Dragging（清除窗口高亮，FR-02“拖拽忽略高亮”）
mouseUp: 拖拽→Confirmed(选区)；未拖拽→点击(高亮窗口=整窗捕获) / 空白=取消
Esc --> cancel()：回 Idle，清空选区
```

- 阈值 4px 与 macOS spec 2.2 一致（手抖容差）
- 所有边界矩形为**物理像素**（PMv2），选区即捕获区域，无 DPI 换算损耗

## 120Hz ≤8ms 绘制路径

- 每帧绘制 = 纯 `FillRectangle`/`DrawRectangle`/`DrawLine` GPU 操作，
  无文本布局（徽标文本在 FR-05 接入时预布局缓存）、无位图上传、无 GDI 互操作
- 状态与绘制解耦：`OverlayState::paintList()`（互斥锁快照，纯几何计算）
  → 渲染线程机械投影成 D2D 调用——`paintList` 无 Win32 依赖，
  MT-safety 由单测覆盖（双线程 5000 次并发无死锁/撕裂）
- 刷帧只由输入事件驱动（mouseMove），无轮询——空闲 CPU 0%（PRD §8）
