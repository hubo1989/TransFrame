# windows/UI/EnginesPage — FR-35 引擎页签（自包含交付说明）

> 本分支不改任何现有工程文件（集成由主线做）。本目录 5 个文件全部自包含。

## 文件清单

| 文件 | 作用 | 可在 macOS 编译 |
|---|---|---|
| `EnginesPageViewModel.hpp/.cpp` | 纯 C++ ViewModel，包装 Core 配置模型（`tf::core::EngineConfig/VlmConfig/PaddleOcrConfig/EngineChainEntry` + `tf::EngineConfig/EngineHealth`），字段名对齐 settings-config-schema.md | ✅ |
| `EnginesPageTests.cpp` | VM 单元测试（Catch2，catch_amalgamated，同 windows/Tests 约定），覆盖链 1..5 / 对比集 2..4 / 兼容实例 0..5 上限、候选过滤、持久化显式保存协议、Sheet draft 语义、健康四态 | ✅（需挂进 Tests 工程） |
| `EnginesPage.xaml` | WinUI 3 UserControl 骨架（无 .idl，不进 XamlTypeInfo） | ❌（仅 Windows） |
| `EnginesPage.xaml.h/.cpp` | C++/WinRT code-behind（纯头投影，不用 /ZW）；链行/引擎卡/添加菜单/两个 Sheet 命令式构建 | ❌（仅 Windows） |

## 集成合同（主线 shell 接入点）

1. **挂载**：NavigationView 的 engines 页签 → `contentFrame.Content(winrt::make<tf::ui::EnginesPage>())`（UserControl 直接实例化，无 .idl）。
2. **App.xaml 必须合并**：`<XamlControlsResources xmlns="using:Microsoft.UI.Xaml.Controls"/>`；`CardStyle` 需 shell 设计系统提供（或删 Style 用内联属性）。
3. **XAML 编译**：把 `EnginesPage.xaml` 作为 `<Page>` 项加入 shell vcxproj（xamlcodegen 已在 Probe 验证，见 docs/winui3-migration.md §5）；`EnginesPage.xaml.g.h` 由生成器产出，`EnginesPage.xaml.h` 已 `__has_include` 防御。
4. **持久化注入**：构造后调 `SetPersistence({loadEnginesJson, saveEnginesJson})`（engines.json 读写；Core `parseEngineConfig`/`serializeEngineConfigPatch` + `writeEngineConfigFileAtomic`）。未注入 = 纯内存模式。
5. **凭证**：VLM per-provider API Key / PaddleOCR 云端 Key / 兼容实例 Key / Baidu AppID+Secret 全部走 CredMan（`credentialAccount(engine)` / `credentialAccountForCompatible(uuid)`，NativeShell CredManKeyStore）—— code-behind 注释标了全部注入点。
6. **健康探测**：`EngineHealthChecker` 异步回调 → 投递 UI 线程 → `vm.SetHealthOf(key, green|red|orange|gray)` → `RefreshHealthDots()`。
7. **测试**：`EnginesPageTests.cpp` 加进 `TransFrame.Core.Tests.vcxproj`（ClCompile + include 路径 `windows/UI/EnginesPage`）。

## 自查清单（已执行）

- [x] x:Name 全局唯一（XAML 内：VlmProviderCombo/VlmBaseURLBox/VlmModelNameBox/VlmOpenAIModeCombo/PaddleStatusDot/PaddleStatusText/PaddleModeCombo/PaddleUseCloudToggle/PaddleCloudBaseURLBox/PaddleCloudAPIKeyBox/PaddleCloudModelIdBox/EngineModeCombo/OrderedChainSection/ChainCountText/ChainListPanel/AddChainButton/AddChainMenu/CompareEnginesSection/CompareCountText/CompareListPanel/AddCompareButton/AddCompareMenu/EngineCardsPanel）
- [x] code-behind 访问器与 x:Name 一一对应（xaml.h 声明 / xaml.cpp 使用一致）
- [x] 事件处理器签名正确（SelectionChanged / RoutedEventArgs / Click 均为 C++/WinRT 标准签名）
- [x] spec §2 控件全覆盖：VLM provider/模式/BaseURL/模型（含 provider 联动与 paddleocr→PaddleOCR 状态区）、PaddleOCR 状态/模式/云端三件套、策略 ordered/compare、链 n/5 增删移换、对比集 n/4、9 内置引擎卡（启用开关+四态健康点）、兼容实例 0..5 + 上限文案、Sheet 1（OpenAI 预设/启用/APIKey/Baidu/AppID/MTran URL/BaseURL/模型/测试连接）、Sheet 2（显示名/BaseURL/模型/APIKey/新建标题）
- [x] 持久化 = 显式 Save*（无"改内存即落盘"）
- [x] 未改任何现有工程文件（`git status` 仅 `windows/UI/` 新增）
