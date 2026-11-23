# NativeShell 捕获服务设计说明（CaptureService · WGC → DXGI → GDI 回退链）

## API 选择理由（PRD §3.2）

首选 **Windows.Graphics.Capture（WGC）**：

- GPU 纹理直达：`Direct3D11CaptureFramePool` 产出 D3D11 纹理，无需经过 GDI 的
  CPU 位图路径，4K 全屏捕获（含 GPU→CPU 一次拷贝）在 PRD §8 预算 50ms 内。
- per-monitor `GraphicsCaptureItem`（`CreateForMonitor` interop），与 Core 的
  `DisplayInfo` 一一对应，多显示器各自原生分辨率。
- `IsBorderRequired=false`（UniversalApiContract 15+，即 Win11 21H2）关闭系统
  黄边框；契约探测封装在 `wgcSupportsBorderless()`，见下表。
- 无需窗口可见即可捕获，托盘常驻进程随时可捕获（首次有系统级确认条一次）。

回退 1 **DXGI Desktop Duplication**：WGC 不可用（老系统契约缺失、组策略禁用、
捕获确认被拒）时使用。GPU 拷贝路径同样高效，但每输出一个 duplication、
会话独占，且不支持独立窗口捕获粒度——仅用于全屏/显示器捕获回退。

回退 2 **GDI BitBlt（SRCCOPY|CAPTUREBLT）**：任何会话类型都可用，是最兜底
路径。4K 约 30-80ms，超出热路径预算，只在链前两级都失败时兜底。

## 回退链实现

```
CaptureService::runChain()
  for engine in [WGC, DXGI, GDI]:
      engine.isAvailable() ? engine.capture(request) : continue
      成功 -> lastEngine_ = engine; 返回
      失败 -> observer_->onFallback(...); 下一级
  全失败 -> {ok:false, error:"All capture engines failed."}
```

- 链构造在 `NativeShellInit.cpp::makeCaptureService()`，顺序固定为 PRD §3.2。
- `ICaptureObserver` 上报引擎选择与回退事件（接 PerformanceRecorder/ETW）。
- 每个引擎是独立 `ICaptureEngine`，单测用 `StubEngine` 脚本化注入
  （`tests/CaptureServiceTests.cpp`，8 个链行为用例全绿）。

## 统一输出（Core Screenshot）

无论哪个引擎命中，输出统一为 `tf::Screenshot`：

| 字段 | 说明 |
|---|---|
| `pixels` | `PixelBuffer`：BGRA8、物理像素、top-down、`stride≥width*4`（D3D staging 256 对齐） |
| `pixels.dpiX/dpiY` | 源显示器 DPI（Per-Monitor V2，`GetDpiForMonitor(MDT_EFFECTIVE_DPI)`） |
| `sourceDisplay` | 捕获源 `DisplayInfo`（含物理像素 frame） |
| `trigger` | `CaptureTrigger`（热键/托盘/窗口单击/手动圈选） |

区域捕获统一走 `captureRegion(region, display, trigger)`：先与显示器 frame 求
交（跨屏拖拽裁剪到源显示器，测试覆盖），空交集直接失败不进引擎。

## 已知限制

| 限制 | 触发条件 | 降级行为 |
|---|---|---|
| 黄边框保留 | Win10 21H2（contract < 15） | `IsBorderRequired` 属性不存在，系统默认黄框；文档明示 Win11 体验最佳 |
| WGC 首次系统确认 | 进程第一次创建 capture item | 系统行为，应用内无法绕过；确认后静默 |
| DXGI 会话冲突 | 远程桌面/其他 duplication 会话 | 降级 GDI |
| `WGCCaptureEngineFull.cpp` | 设备接线 | D3D 设备单例在 App 层组装；本任务交付几何/链/契约与探针，设备注入点已留（见 `makeWgcCaptureEngine`） |

## 性能预算对照（PRD §8）

- 松开→截图产物 ≤50ms：WGC 单帧获取（frame pool 常驻、双缓冲）+ 一次
  `CopyResource` 到 staging + 一次 `Map`，像素直接进 `PixelBuffer`。
- 热键→覆盖层 ≤60ms：见 `SelectionOverlay.md`（预创建窗口路径）。
