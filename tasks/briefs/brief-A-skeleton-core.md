# Brief A — Windows 工程骨架 + Core 可移植层（US-W01 + Core 适配）

你是 TransFrame Windows 版的实现工程师之一。4 个并行子任务共用同一 PRD；你负责 A：解决方案骨架 + Core 层。

## 必读上下文（按序）
1. `tasks/prd-windows-transframe.md` — 总 PRD（§3 选型、§4 架构、§9 US-W01、附录 B 目录结构）
2. `TransFrame/Models/`（swift 模型，schema 源）+ `TransFrame/Services/Translation/VisualLineGrouping.swift`、`LLMTranslationBatching.swift`、`LLMTranslationResponseParser.swift`
3. `TransFrameTests/`（现有测试风格）

## 交付范围（只做这些，不越界）
在 `windows/` 目录下创建：

1. `TransFrame.sln` + 三个工程：
   - `Core/TransFrame.Core.vcxproj`（静态库, C++20, /W4 /WX, x64+arm64）：移植数据模型 —— Settings 模型（键名与 macOS UserDefaults `TransFrame.` 前缀一一对应）、Annotation 11 种、Screenshot/CaptureDocument、OCREngineType、TranslationEngineType（**无 apple 引擎**）、EngineChainItem/EngineModeV2、TranslationScene/SceneEngineBinding、TranslationPromptConfig、TranslationResultBundle/BilingualSegment、TranslationHistory(4 类)、ExportFormat(PNG/JPEG/HEIC)、AppLanguage(10 语言)
   - `NativeShell/TransFrame.NativeShell.vcxproj`（静态库，本任务仅建空骨架 + pch，捕获/UIA 留给任务 B）
   - `App/TransFrame.App.vcxproj`（WinUI 3 桌面应用：空白窗口 + 打包工程，具体 UI 留给 B/C 后续）
   - `Tests/TransFrame.Core.Tests.vcxproj`（Catch2 v3 单测）
2. Core 移植要求：
   - JSON 用 nlohmann（vendored 或 vcpkg.json，写明依赖清单与安装命令）
   - `VisualLineGrouping.mergeLines` 完整移植 + **与 macOS 测试向量同源**：从 `TransFrameTests` 提取现有向量数据为 `Tests/fixtures/visual-line-grouping.json`（若 swift 测试内嵌向量，复制数值，注明出处）
   - `LLMTranslationBatching`/`LLMTranslationResponseParser` 移植 + 单测
   - `TranslationPromptConfig` 占位符解析（{source_language}/{target_language}/{text}）+ 单测
3. CI：`.github/workflows/windows-ci.yml` — windows-latest, msbuild Release 双架构, 运行 Catch2 测试
4. `windows/README.md` — 构建/测试/依赖安装说明

## 明确约束
- 不实现捕获/OCR/翻译网络/托盘（B/C 的范围），但要在 Core 中定义好接口（纯虚 `IOcrEngine`、`ITranslationProvider`、`IHotkeyListener` 等，头文件带文档注释）
- 已裁决决策（PRD 文首）：不内置模型、无 FTS5、仅 MSIX、Apple 引擎不存在
- 所有模型字段与 macOS swift 版语义一致；不确定处标 `// TODO(schema-align)` 并在报告中列出

## 完成标准
- `msbuild Release x64` 与 `arm64` 双绿（本机无 Windows 则以 CI 配置正确 + CMake/MSBuild 工程文件静态审查为完成标准，报告注明 NOT RUN 的部分）
- Catch2 单测全绿（至少覆盖：prompt 解析、视觉行分组、LLM 批量解析、settings JSON 往返）
- 报告：文件清单、接口清单、schema 对齐差异表、未运行项与原因
