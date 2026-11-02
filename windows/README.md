# TransFrame for Windows — 工程骨架（任务 A / US-W01）

Windows 原生版的解决方案骨架 + Core 可移植层。本目录由任务 A 交付：
解决方案四工程、Core 数据模型与算法（与 macOS Swift schema 一一对齐）、
纯虚接口、Catch2 单测、CI。

任务 B（捕获/托盘/UIA，`win-capture-shell` worktree）填充 NativeShell；
任务 C（翻译/OCR 服务，`win-translation-services` worktree）实现服务层。

## 目录

```
windows/
├── TransFrame.sln                 # 解决方案（4 工程 × Debug/Release × x64/ARM64）
├── Core/                          # 可移植层：模型 + 算法（无 Win32 依赖）
│   ├── TransFrame.Core.vcxproj
│   ├── *.h / *.cpp                # 模型与算法（见下方清单）
├── NativeShell/                   # Win32 壳层（任务 A：空骨架 + pch）
│   └── TransFrame.NativeShell.vcxproj
├── App/                           # WinUI 3 桌面应用（任务 A：空白窗口 + MSIX 打包）
│   ├── TransFrame.App.vcxproj
│   ├── TransFrame.Package.wapproj
│   └── Package.appxmanifest
├── Tests/                         # Catch2 v3 单测
│   ├── TransFrame.Core.Tests.vcxproj
│   ├── fixtures/visual-line-grouping.json   # 与 macOS 测试同源的向量
│   └── *Tests.cpp
└── external/                      # vendored 依赖（见 external/README.md）
    ├── nlohmann/json.hpp          # v3.11.3
    └── catch2/catch_amalgamated.* # v3.7.1
```

## 构建（Windows / MSVC）

前置：Visual Studio 2022 (v143 工具集)、Windows SDK 10.0.26100。

```powershell
# 双架构 Release
msbuild windows\TransFrame.sln /m /p:Configuration=Release /p:Platform=x64
msbuild windows\TransFrame.sln /m /p:Configuration=Release /p:Platform=ARM64

# 运行 Catch2 测试
windows\build\x64\Release\TransFrame.Core.Tests\TransFrame.Core.Tests.exe
```

## 构建 + 测试（任意平台，含 macOS/Linux）

Core 层平台无关（无 Win32 include），单命令编译全部源 + 测试：

```bash
clang++ -std=c++20 -Wall -Wextra -Werror \
  -I windows -I windows/external \
  windows/Core/*.cpp \
  windows/external/catch2/catch_amalgamated.cpp \
  windows/Tests/*.cpp \
  -o build/tests_runner

# 在仓库根运行（fixture 相对路径解析）
./build/tests_runner
```

或 GCC 13+：把 `clang++` 换成 `g++`，参数相同（CI 的 ubuntu job 即此路径）。

## 依赖

全部 vendored，无 vcpkg/网络依赖。版本与校验说明：`external/README.md`。

## Core 模型清单（schema 对齐 macOS v1.0.4）

| 头文件 | 对应 Swift | 说明 |
| --- | --- | --- |
| `SettingsKeys.h` | AppSettings.Keys | 全部 UserDefaults 键，逐键对齐 |
| `KeyboardShortcut.h` | KeyboardShortcut.swift | Carbon 键码 + 修饰位，5 个默认快捷键 |
| `Annotation.h/.cpp` | Annotation.swift | 11 种标注（rectangle…calloutBubble），tagged-JSON 编码 |
| `CaptureDocument.h/.cpp` | Screenshot/CaptureDocument/DisplayInfo/OCRResult/ScreenAnalysisResult | ImageRef 句柄代替 CGImage |
| `TranslationResultBundle.h/.cpp` | TranslationResult/BilingualSegment/EngineResult/TranslationResultBundle | 错误以 category+message 携带 |
| `TranslationHistory.h/.cpp` | TranslationHistory.swift + HistoryStore 约定 | 4 类条目（translation/screenshot/color/upload），color 条目约定 sourceText="Color Picked" 等 |
| `VLMProviderType.h/.cpp` | VLMProviderType.swift + CompatibleConfig | 5 provider 默认 baseURL/model、OpenAIAPIMode、PaddleOCRMode、CompatibleConfig |
| `Geometry.h` | CGPoint/CGSize/CGRect | CGRect 语义（minX/maxX/union/insetBy/null） |
| `Uuid.h` | Foundation.UUID | 大写字符串 wire 格式、v4、nil |
| `Json.h/.cpp` | Swift Codable wire 形状 | CGRect=[[x,y],[w,h]]、Date=2001 纪元秒、Base64、enum-with-payload、enum-keyed dict |
| `TranslationEngineType.h` | TranslationEngineType.swift | 9 引擎（**无 apple**），raw 值与 macOS 一致 |
| `EngineSelectionV2.h` | EngineSelectionV2.swift | EngineChainItem/EngineModeV2/limits(5/4/2) |
| `TranslationEngineConfig.h` | TranslationEngineConfig.swift | EngineOptions 可选字段 |
| `SceneEngineBinding.h` | SceneEngineBinding.swift | 迁移源类型；apple 读入降级 |
| `TranslationScene.h` | TranslationScene.swift | 3 场景 |
| `TranslationPromptConfig.h` | TranslationPromptConfig.swift | 优先级 + 占位符替换（顺序与 Swift 相同） |
| `VisualLineGrouping.h/.cpp` | VisualLineGrouping.swift | mergeLines/joinText，阈值 0.5/3.0 |
| `LLMTranslationCore.h/.cpp` | LLMTranslationBatching/ResponseParser.swift | 分批 + OpenAI/Claude JSON/SSE 解析 |
| `AppLanguage.h` | AppLanguage.swift | system + 10 语言 |
| `OCREngineType.h` | OCREngineType.swift | vision→windows 迁移映射 |
| `ExportFormat.h` | ExportFormat.swift | png/jpeg/heic |
| `Styles.h` | Styles.swift | CodableColor/StrokeStyle/TextStyle/FontWeight |
| `Interfaces.h` | （新增） | IOcrEngine/ITranslationProvider/IHotkeyListener/IClipboard/IHistoryStore/ICredentialStore 纯虚接口 |

## 测试

```bash
./build/tests_runner                    # 全部（68 用例 / 261 断言，macOS clang 20 验证）
./build/tests_runner "[visual-line-grouping]"
./build/tests_runner "[llm]"
./build/tests_runner "[schema]"
```

向量来源：`Tests/fixtures/visual-line-grouping.json` 从
`TransFrameTests/VisualLineGroupingTests.swift` 逐条提取（文件内注明出处）。

## CI

`.github/workflows/windows-ci.yml`：
- `core-tests` job：windows-latest，msbuild Release x64 + ARM64，跑 Catch2；
- `portable-check` job：ubuntu + g++-13 编译运行同一套源，保证 Core 不泄漏 Win32 依赖。

## 未运行项（本 macOS 主机）

- MSVC x64/ARM64 编译：CI 覆盖（`windows-ci.yml`）；本机复现命令见上。
- WinUI 3 / Windows App SDK NuGet 还原：任务 B 接入真实 XAML 时启用
  （`App/TransFrame.App.vcxproj` 中已留注释开关）。
- MSIX 打包签名：`TransFrame.Package.wapproj` + `Package.appxmanifest` 骨架就绪，
  签名证书占位，发布时替换。
