# 子任务 A 完成报告（US-W01：解决方案骨架 + Core 可移植层）

**状态：完成**（macOS 本机 clang++ 20 编译 + Catch2 全绿：68 test cases / 261 assertions）。
MSVC/WinUI/MSIX 未在本机运行（无 Windows），CI 与复现命令见下。

## 一、文件清单

### Core 模型（windows/Core/）

| 文件 | 内容 | 新增/修订 |
| --- | --- | --- |
| `SettingsKeys.h` | AppSettings.swift `Keys` 枚举全部键（含 per-provider `vlmBaseURL.<raw>` 函数式键、history key），逐键同名 | 新增 |
| `KeyboardShortcut.h` | 键码+修饰位、5 个默认快捷键（Cmd+Shift+3/4/T/Y/I 的 Carbon 码）、`{"keyCode","modifiers"}` JSON | 新增 |
| `Annotation.h/.cpp` | 11 种标注完整移植：rectangle/freehand/arrow/text/ellipse/line/mosaic/highlight/numberLabel/privacyRedaction/calloutBubble。std::variant 载荷 + tagged-JSON（`{"type","value"}`）；各类 isValid/bounds/clamp 逻辑与 Swift 相同（含 NumberLabel 12–256 clamp、Mosaic 4–128、Highlight 0.1–0.8、箭头 bounds 的 lineWidth*3 padding） | 新增 |
| `CaptureDocument.h/.cpp` | DisplayInfo（resolution/isRetina/pixelSize）、OCRText/OCRResult（fullText 按 minY 排序、filteredBy、observationsIn）、TextSegment、ScreenAnalysisResult（filter/recoveringFilteredSegments/updatingSegment/fromOcr）、Screenshot（estimatedFileSize/formattedFileSize/aspectRatio；ImageRef=uint64 句柄代替 CGImage）、QuartzGlobalRect、CaptureDocument（全部 revision 字段） | 新增 |
| `TranslationResultBundle.h/.cpp` | TranslationResult（combine/emptyFor）、BilingualSegment（fromResult/fromSegment/pixelBoundingBox）、EngineResult（isSuccess/failed）、TranslationErrorCategory 6 类、EngineSelectionMode（legacy 4 模式 raw 值）、TranslationResultBundle（primaryResult 回退逻辑/hasErrors/allFailed/successfulEngines/failedEngines/single/failed） | 新增 |
| `TranslationHistory.h/.cpp` | HistoryCategory 4 类（raw/displayName/SF Symbol 名）、TranslationHistory 全字段 + Swift decodeIfPresent 默认（category→translation、isFavorite→false）、4 类工厂：`fromTranslationResult`/`colorPicked`（Color Picked/RGB Hex/hex 同值约定）/`uploaded`（markdown/URL/provider/Public URL）/`screenshotSaved`；matches 大小写不敏感子串、sourcePreview 500 截断 | 新增 |
| `VLMProviderType.h/.cpp` | 5 provider（raw 值、默认 baseURL/model、requiresAPIKey）、OpenAIAPIMode、PaddleOCRMode、CompatibleConfig（nil-UUID 默认实例、keychainId `custom:<uuid>`） | 新增 |
| `VisualLineGrouping.h/.cpp` | mergeLines/belongsToSameLine/mergedSegment/joinText 完整移植；**修复了本移植过程中的一个真实 bug**：lineBox 必须只由已有行碎片构成（与 Swift 一致），首版误把新碎片并入 union 导致全部合一行——测试向量当场抓住，已修复 | 新增 |
| `LLMTranslationCore.h/.cpp` | LLMTranslationBatching（separator `\n---\n`、UTF-16 计数、splitLongText 按码点切、prompt {text} 成本扣除）+ LLMTranslationResponseParser（JSON 与 SSE 双方言、仅首行 `data:` 判流、error.message 透传、Claude stop_reason/content[].text、OpenAI choices/finish_reason/delta.content、截断→incomplete） | 新增 |
| `Interfaces.h` | 纯虚接口（全部带文档注释）：`IOcrEngine`、`ITranslationProvider`(+`TranslateRequest`)、`IHotkeyListener`、`IClipboard`、`IHistoryStore`、`ICredentialStore`、`OcrImage`（BGRA8 跨平台图像） | 新增 |
| `TranslationPromptConfig.h` | 占位符解析（既有草稿，本任务补测） | 审查通过 |
| `Geometry.h`/`Uuid.h`/`Json.h/.cpp`/`TranslationEngineType.h`/`TranslationScene.h`/`EngineSelectionV2.h`/`TranslationEngineConfig.h`/`SceneEngineBinding.h`/`AppLanguage.h`/`OCREngineType.h`/`ExportFormat.h`/`Styles.h` | 既有草稿修订：修复 `boundingBox.height()` 误当函数调用；`from_json` 统一 `const Json&`（nlohmann ADL 要求）；`SceneEngineBinding` 补 `operator==`；`Json.h` 补 `nlohmann::adl_serializer<time_point>` 特化（std::chrono 命名空间 ADL 不可达） | 修订 |

### 工程文件

| 文件 | 说明 |
| --- | --- |
| `windows/TransFrame.sln` | 4 工程 × Debug/Release × x64/ARM64（Core/NativeShell/App/Tests） |
| `windows/Core/TransFrame.Core.vcxproj` | 静态库，C++20 `/W4 /WX` 双架构 |
| `windows/NativeShell/TransFrame.NativeShell.vcxproj` | 静态库空骨架 + pch（任务 B 填充）；依赖 Core |
| `windows/NativeShell/pch.h/.cpp`、`NativeShellSkeleton.cpp` | pch 与占位 TU |
| `windows/App/TransFrame.App.vcxproj` | 应用工程：Win32 空白窗口入口（WinUI 3 NuGet 注释开关就绪，任务 B 切换 XAML island）；依赖 Core+NativeShell |
| `windows/App/AppMain.cpp` | 空白窗口 + 消息泵 |
| `windows/App/TransFrame.Package.wapproj` + `Package.appxmanifest` | MSIX 打包骨架：startupTask 扩展、graphicsCapture 受限能力、占位 Identity |
| `windows/Tests/TransFrame.Core.Tests.vcxproj` | Catch2 测试工程，fixture 复制到输出目录 |
| `windows/Tests/TestMain.cpp` | Catch2 入口 |
| `windows/Tests/CoreModelsTests.cpp` | schema 对齐 30+ 用例（wire 形状断言逐字段） |
| `windows/Tests/VisualLineGroupingTests.cpp` | Swift 同向量 16 用例 + fixture 文件驱动复验 |
| `windows/Tests/LLMTranslationCoreTests.cpp` | 截断/JSON/SSE/分批 12 用例 |
| `windows/Tests/TranslationPromptConfigTests.cpp` | 优先级/占位符 7 用例 |
| `windows/Tests/fixtures/visual-line-grouping.json` | 从 `TransFrameTests/VisualLineGroupingTests.swift` 逐条提取（12 case + 3 joinText，文件头注明出处与坐标系） |
| `.github/workflows/windows-ci.yml` | windows-latest msbuild 双架构跑 Catch2 + ubuntu g++-13 portable-check（防 Win32 泄漏进 Core） |
| `windows/README.md` | 构建/测试/依赖/模型清单/未运行项 |
| `windows/external/README.md` + `nlohmann/LICENSE.MIT` + `catch2/LICENSE.txt` | vendored 依赖来源、版本、重下命令、许可证 |

## 二、接口清单（Core/Interfaces.h，全部纯虚 + 文档注释）

- `IOcrEngine`：`id()/name()/isAvailable()/recognize(OcrImage)->OCRResult`（归一化 bbox、置信度）
- `ITranslationProvider`：`id()/name()/engineType()/isAvailable()/translate(TranslateRequest, out)->optional<EngineError>/verifyConnection()->optional<EngineError>`（verifyConnection 禁打计费端点，注释明示）
- `IHotkeyListener`：`registerShortcut(actionId, KeyboardShortcut)/unregisterShortcut/nextFiredAction`
- `IClipboard`：`copyImage(PNG)/copyText/readText/hasImage`
- `IHistoryStore`：`entries/add/remove/toggleFavorite/save/reload`（≤50 条上限注释）
- `ICredentialStore`：`save/load/remove/has`（账户命名 `TransFrame/<engine>/<instance>`）
- 辅助值类型：`OcrImage`（BGRA8）、`TranslateRequest`

## 三、Schema 对齐差异表（Windows vs macOS）

| 项目 | macOS | Windows 处理 | 状态 |
| --- | --- | --- | --- |
| CGRect wire | `[[x,y],[w,h]]` | 相同（`Json.h` to_json/from_json + 测试断言 dump 字面量） | 已测 |
| UUID wire | 大写 8-4-4-4-12 | 相同；parse 容忍大小写/花括号，malformed 抛 `invalid_argument` | 已测 |
| Date wire | 2001-01-01Z 纪元秒（double） | 相同；`adl_serializer<time_point>` 特化使 initializer-list 可用 | 已测（0.0 ↔ 978307200 unix） |
| Data wire | base64 | `encodeBase64/decodeBase64`；PNG 头向量 `iVBORw0K` 对拍 | 已测 |
| enum 带载荷（EngineChainItem.Kind） | `{"builtin":{"_0":"openai"}}` | 相同字面量断言；compatible 变体同 | 已测 |
| enum-keyed Dictionary（promptConfig 等） | 键值交替数组 | 相同（既有草稿实现，本任务补 prompt 单测） | 已测 |
| TranslationEngineType.apple | 存在（10 案例第 1 位） | **不存在**（PRD §6.12）；chain item 读到 `apple` 抛异常→调用方丢条目（降级在设置层）；engineConfigs `id:"apple"` 同 | 已测（THROWS 断言） |
| SceneEngineBinding 默认 primary | `.apple` | `.mtran`；fallback=`custom`（**TODO(schema-align)** 原注释保留：与 C 任务最终裁决对齐——C 的 CoreModels 无此默认，按 PRD §6.12 mtran 为链首；若协调者裁决不同，改 `defaultFor` 一处即可） | 已定待复核 |
| OCREngineType.vision | 本地默认 | 读入映射为 `windows`；`windows`/`paddleocr` 原样 | 已测 |
| CompatibleConfig 默认 id | nil UUID `00000000-…` | 相同（keychainId 对拍） | 已测 |
| Annotation 编码 | Swift enum 未走 Codable 存档 | 显式跨平台 tagged JSON `{"type","value"}`（11 类 round-trip 全测）；macOS 文档存档走 CaptureDocument 几何原语，不冲突 | 设计决策 |
| Screenshot CGImage | 运行时对象不可编码 | `ImageRef`(uint64) 句柄；dimensions 查询留 NativeShell 钩子（Core 返回 0×0） | 骨架 |
| EngineError | Swift 任意 Error | `TranslationErrorCategory`(6) + message 字符串 | 等价收窄 |
| 历史条目 4 类约定 | HistoryStore 内联构造 | 工厂函数 `colorPicked/uploaded/screenshotSaved/fromTranslationResult`，字段逐一对齐 | 已测 |

## 四、测试输出摘要（本机执行）

```
$ clang++ -std=c++20 -Wall -Wextra -Werror -I windows -I windows/external \
    windows/Core/*.cpp windows/external/catch2/catch_amalgamated.cpp windows/Tests/*.cpp \
    -o build/tests_runner          # 退出码 0，零警告（-Werror 下）

$ ./build/tests_runner
===============================================================================
All tests passed (261 assertions in 68 test cases)
```

用例分布：visual-line-grouping 16（含 fixture 文件驱动复验）、llm 12、prompt 7、schema/模型 33。
向量与 `TransFrameTests/VisualLineGroupingTests.swift`、`LLMResponseTruncationTests.swift` 同源；fixture 文件头注明出处。

关键回归（移植中实际抓住并修复）：`belongsToSameLine` 的 lineBox 首版把候选碎片并入 union，导致三行样例合为一行——Swift 源码复核后修正为 union 仅含已有行碎片，16 个向量全绿。

## 五、未运行项 + Windows 复现命令

| 未运行 | 复现命令（Windows） |
| --- | --- |
| MSVC x64/ARM64 编译 | `msbuild windows\TransFrame.sln /m /p:Configuration=Release /p:Platform=x64`（ARM64 同理换 Platform） |
| Catch2 于 Windows 运行 | `windows\build\x64\Release\TransFrame.Core.Tests\TransFrame.Core.Tests.exe` |
| WinUI 3 / WindowsAppSDK 还原 | vcxproj 中 PackageReference 注释块取消注释后 `msbuild /restore /t:Build`（任务 B 接管） |
| MSIX 打包签名 | `TransFrame.Package.wapproj` 构建；Identity/Publisher 为占位，发版前换真证书 |
| CI 首跑 | push 后 `.github/workflows/windows-ci.yml`（core-tests 双架构 + ubuntu portable-check） |

本机（macOS）等价验证已跑：上述 clang 单命令编译 + 全量测试；`Core/*.cpp` 以 `-fsyntax-only` 独立过检（证明无 Win32 泄漏）。

## 六、与任务 C（win-translation-services）模型对齐说明

C 的 `windows/Core/include/tf/CoreModels.hpp`（已测全绿）与本任务 `windows/Core/`（tf::core 命名空间）关系：

- **语义对齐、命名空间并存**：C 用 `tf::`（服务层），A 用 `tf::core::`（应用骨架层）。两者 schema 语义一致（UUID 大写 wire、CGRect 嵌套数组、引擎 raw 值、无 apple、enum-keyed dict 交替数组、UTF-16 计数 batching、parser 双方言）。
- **算法对齐**：`VisualLineGrouping`、`LLMTranslationBatching`、`LLMTranslationResponseParser` 三者与 C 的 `tf/VisualLineGrouping.hpp`、`tf/LLMTranslationCore.hpp` 逐函数同语义（阈值 0.5/3.0、`\n---\n`、UTF-16 计数、首行 data: 判流、截断→incomplete）；本任务独立重写并再次以 Swift 原向量验证（含 C 未覆盖的 belongsToSameLine lineBox 归属 bug 修复——C 的实现 union 输入正确，无此问题）。
- **接口对齐**：A 的 `IOcrEngine`/`ITranslationProvider` 签名与 C 的同名接口（`Outcome<OcrResult,ProviderError>` vs 本任务 `(result, out-error)` 双出参）有意不同：A 面向应用骨架（无 HttpTransport 依赖），C 面向服务层。**待协调者裁决**：B/C 合流时若采用 C 的服务层实现，App 侧用一层薄适配（`Outcome→optional<EngineError>`）即可，模型二选一落盘（建议保留 A 的 `tf::core` 作为 App 内唯一模型，C 层在其上做 transport）。
- **不重复造轮子的边界**：A 未实现 HTTP/credential/调度（C 已有），只定义接口；C 的 `Uuid`（mt19937+random_device）与 A 的 `Uuid`（BCrypt/random_device 回退）wire 格式一致，互通无障碍。
