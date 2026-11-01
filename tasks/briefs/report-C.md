# Report C — Windows 翻译/OCR 服务层

**任务**: Brief C（US-W06/07/08/09 + 引擎注册表 + 划词/插入）
**分支**: `hubo/win-translation-services`（worktree `win-translation-services`）
**状态**: 完成 — Catch2 全绿（105 cases / 370 assertions，macOS arm64 Apple clang 21.0.0）

## 交付物清单

| 类别 | 文件 |
|---|---|
| Core 头（13） | `windows/Core/include/tf/{CoreModels,HttpTransport,TranslationProvider,LLMTranslationCore,Json,LLMChatProvider,MTranServerProvider,CloudProviders,CompatibleChatProvider,TranslationScheduler,EngineHealthChecker,OcrEngines,VisualLineGrouping,TextSelectionInsertion}.hpp` |
| Core 实现（14） | `windows/Core/src/*.cpp`（与头一一对应 + `CredentialAccounts.cpp` 凭据命名/掩码） |
| NativeShell（Win32, `#ifdef _WIN32`） | `WinHttpTransport.{h,cpp}`、`CredManKeyStore.{h,cpp}`、`Win32OcrUiaHosts.{h,cpp}` |
| 测试（6 文件） | `windows/Tests/{TranslationSchedulerTests,ProvidersTests,LLMTranslationCoreTests,EngineHealthCheckerTests,OcrAndTextSelectionTests}.cpp` + `TestSupport.hpp` |
| 手工冒烟 | `windows/Tests/manual/smoke_translation.cpp`（S1–S8 链路；本机 NOT RUN，非 Windows 分支编译验证通过） |
| 引擎文档 ×9 | `docs/windows/engines/{mtran,openai,claude,gemini,ollama,google,deepl,baidu,custom}.md` |
| 构建说明 | `windows/README.md` |

## FR 映射

| FR | 要求 | 交付位置 | 验证 |
|---|---|---|---|
| FR-15 | Windows.Media.Ocr → TextSegment[]（归一化 bbox + confidence）；语言包检测 | `OcrEngines.hpp` `WindowsOcrEngine`（LanguageProvider/Recognizer 缝 + `mapLines` + `normalizeLanguageTag`）；WinRT 胶水在 `Win32OcrUiaHosts.cpp` | mapLines 归一化/语言包门/空列表不可用/tag 归一 4 测试绿 |
| FR-16 | PaddleOCR fast/precise 外部进程 + 健康缓存 | `OcrEngines.hpp` `PaddleOcrEngine`（Mode/`buildArguments`/`parseFastMode`/`parsePreciseMode`/`pythonDictToJson`/`extractResultJson`）+ `PaddleOcrHealthCache` | fast/precise 解析、置信度过滤、Python 字面量转换、ANSI 剥离、argv 形状、TTL 缓存 9 测试绿 |
| FR-17 | VLM 五 Provider 位 + 分段解析 | `OcrEngines.hpp` `VlmOcrEngine`（五槽 + `extractionPrompt` + `parseSegments`，chat completion / fenced json / 像素 box 归一） | 5 槽位、fenced/像素 box 解析、Ollama keyless 可用性 4 测试绿 |
| FR-18 | 引擎 ×9（无 apple）、超时 10s、取消、错误四分类 | `LLMChatProvider`（openai/claude/gemini/ollama 四分支）、`CloudProviders`（google/deepl/baidu）、`MTranServerProvider`、`CompatibleChatProvider`、`HttpTransport`（CancellationToken/HttpRequest） | 各引擎请求形状/响应解析/错误码映射测试绿（详见各 engine md 的映射表） |
| FR-19 | 兼容多实例 UUID | `CompatibleChatProvider`（`custom:<uuid>` 凭据槽、`resolveConfigurationSnapshot` 已保存优先）、`EngineChainItem::makeCompatible` | 多实例身份/独立请求/悬空跳过/快照解析 4 测试绿 |
| FR-20 | ordered ≤5 逐个回落 + 聚合错误 | `TranslationScheduler::translateOrdered` | 回退矩阵/首个成功停/不可用跳过/全败聚合/链截断 5/取消即止 6 测试绿 |
| FR-21 | compare 2-4 并行 + 结果缓存 | `TranslationScheduler::translateCompare`（std::thread 并行 + FNV-1a 缓存键） | 并行/部分失败/全败/顺序保持/缓存免重译/截断 4 6 测试绿 |
| FR-22 | 四态探测 + 不烧配额 + TTL 对齐 | `EngineHealthChecker`（四态/60s TTL/注入时钟）+ `DefaultEngineProbe`（mtran /health 梯子、openai/ollama/兼容 models、claude/gemini/deepl/google/baidu 无免费探测 → 有凭据灰） | 四态映射/梯子容错/门控/缓存命中/过期/invalidateAll/refresh 15 测试绿 |
| FR-23 | 三场景绑定路由 | `TranslationScheduler::translateForScene` + `SceneEngineBinding` | 绑定命中/未绑定回退全局 2 测试绿 |
| FR-24 | 三层 Prompt 覆盖 + 占位符 + 插入场景默认 | `TranslationPromptConfig`（`resolve` scene>compatible>engine>默认；`substitute` 三占位符）——**注意层级顺序，见差异清单 #2** | 分层/占位符 2 测试绿 |
| FR-25 | VisualLineGrouping 移植（同测试向量） | `VisualLineGrouping`（Y 投影重叠 0.5 / 水平间距 3×行高 / CJK 与标识符无缝拼接） | 行内代码合并/两行分离/下划线/CJK/分栏边界/置信度均值+bbox 并集 6 测试绿 |
| FR-27 | LLM 批处理 + 截断检测 | `LLMTranslationBatching`（`\n---\n` 分隔、预算收缩、超长按字符切）+ `LLMTranslationResponseParser`（plain JSON + SSE、`finish_reason:length`/`max_tokens` 截断错误、正文含 `data:` 不误判） | 批量预算/字符切分/prompt 成本/截断 5 形态/SSE 拼接/错误对象 12 测试绿 |
| FR-28 | 划词：UIA 优先 → Ctrl+C 回退（保存/恢复剪贴板） | `TextExtractor`（Route 1 UIA GetSelection；Route 2 backup→clear→Ctrl+C→poll→restore，`allowClipboardFallback` 缝） | UIA 优先不碰剪贴板/回退后恢复原内容/禁用回退不出错/空选区报错 5 测试绿 |
| FR-29 | 插入：重验证 + 绝不粘贴 | `InsertionService::validateTarget`（UIA runtimeId + 选区文本比对 / 剪贴板模式 PID 锚定）+ 粘贴窗口（set→Ctrl+V→150ms→restore） | 正常粘贴并恢复/选区变更中止/焦点丢失中止/PID 变更中止/空译文拒绝 5 测试绿（paste 调用数 = 0 断言） |
| §5（brief） | KeyStore：CredMan + `TransFrame/<engine>/<instance>` + `sk-****last4` | `CredManKeyStore.{h,cpp}`（CredWrite/CredRead，JSON blob 保 appID/additional）+ `CredentialAccounts.cpp`（`credentialAccount`/`credentialAccountForCompatible`/`maskApiKey`） | 命名/掩码/mock 往返 2 测试绿 |

## 与 macOS 行为差异清单

对照 PRD §6.12 白名单，逐条核对：

1. **Apple Translation 移除**（白名单第 1 条，符合）：引擎集 9 个（无 apple），
   raw 值与 macOS 一致；`translationEngineFromRaw("apple")` 返回 nullopt。
   默认链首 MTranServer（已裁决）。
2. **Prompt 层级顺序**：移植初期 C++ 把 engine 层放在 scene 之前；Swift 原版
   （`TranslationPromptConfig.promptPreview`）是 **scene > compatible > engine**。
   已在实现中修正为 Swift 顺序（`CoreModels.cpp resolve`），测试覆盖三层层级。
   这不是与 macOS 的差异，是移植缺陷的修复记录。
3. **EngineRegistry 缓存弱化**：Swift `TranslationEngineRegistry` 缓存 provider
   实例（class 引用语义）；C++ `TranslationProviderPtr = unique_ptr` 不可共享，
   registry 改为**缓存配置切片**（`operator==` 命中时仍走工厂重建实例）。
   行为差异：多轮 translate 会重建 provider 对象（无网络差异；凭据/配置读取
   每次 fresh）。对功能无影响（provider 无内部状态需保活）；若后续任务 D/E
   需要真正的实例共享，可把 `TranslationProviderPtr` 换 `shared_ptr` — 接口不变。
4. **WindowsOcr confidence**：Windows.Media.Ocr 不给置信度分数（macOS Vision 有），
   `mapLines` 一律填 1.0，保持 TextSegment schema 兼容（macOS 端同字段照旧）。
   §6.12 第 2 条（Apple Vision OCR → Windows.Media.Ocr 为默认）范围内。
5. **MTranServer 探测端点**：`DefaultEngineProbe` 内建 `Configuration::makeDefault()`
   （localhost:8989）探测。macOS 侧探测使用**当前配置**的端点。设置层接入后
   应把用户配置传入（接口已留：probe 构造可注入配置工厂）；当前测试按
   localhost 断言。**遗留 TODO（任务 D/E 接线时闭合）**。
6. **keyless OpenAI 兼容 localhost 判定**：`LLMChatProvider::isAvailable` 对
   `engine == OpenAI && baseURL 为 localhost` 放行无 key（Swift PR#92 语义，
   LM Studio/vLLM 预设）；与 macOS 行为一致，无差异。
7. **取消语义**：cancelled → `ProviderError::network("cancelled")`；调度器见
   network+cancelled 即止链（不磨完剩余引擎）。与 Swift PR#92 一致。
8. **错误分类第 5 类**：brief 要求网络/配额/鉴权/内容四类；实现加了
   `Configuration`（无效 URL/缺凭据，可不出网就判定的错误）。这是超集而非
   偏离：四类语义不变，Configuration 用于引擎卡灰/橙判定。若视为超白名单
   差异，可在 UI 层把 Configuration 并显示为内容错误 — 记录待裁决。

## 测试结果（本机实测）

```
$ clang++ -std=c++20 \
    -I windows/Core/include -I windows/Core/vendor \
    -I windows/Tests -I windows/Tests/vendor \
    windows/Core/src/*.cpp windows/Tests/*.cpp \
    windows/Tests/vendor/catch_amalgamated.cpp \
    -o tests_runner && ./tests_runner
All tests passed (370 assertions in 105 test cases)
```

按标签：`[scheduler]` 15 cases / 47 assertions；`[health]` 15 / 32；`[ocr]` 19 / 78；
`[selection]` 5 / 14；`[insertion]` 5 / 15；其余（providers/llm/grouping/keystore/
prompt/url/models）合计覆盖 9 引擎请求形状、MD5 已知向量（RFC 1321）、
DeepL 456→quota、Baidu 54003/54004、HTTPS key 守卫、URL 后缀去重等。

## NOT RUN（本机 macOS 限制）+ 复现命令

| 项 | 状态 | 复现 |
|---|---|---|
| `WinHttpTransport.cpp`（WinHTTP 5.1） | 编译未验证（需 Windows SDK） | Windows: `cl /std:c++20 /EHsc /I windows\Core\include /I windows\Core\vendor /I windows\NativeShell\include windows\NativeShell\src\WinHttpTransport.cpp /link winhttp.lib` |
| `CredManKeyStore.cpp`（CredWrite/CredRead） | 同上 | `cl ... CredManKeyStore.cpp /link advapi32.lib credui.lib` |
| `Win32OcrUiaHosts.cpp`（WinRT OCR / UIA / SendInput） | 同上 | `cl ... Win32OcrUiaHosts.cpp /link user32.lib ole32.lib oleaut32.lib windowsapp.lib` |
| `smoke_translation.cpp` S1–S8 | 非 Windows 分支已编译运行（打印 NOT RUN 退出 0）；Windows 全链路未跑 | `windows/README.md` 的 MSVC 命令；S1/S2/S7 可离线验证，S3–S6 需本地服务/key |

接口契约（`IHttpTransport`/`ICredentialStore`/`IOcrEngine`/`TextExtractorHost`/
`IEngineProbe`）全部以 mock 在 Catch2 里驱动过 — NativeShell 实现只需满足
同一接口签名即接入。

## 完成标准核对

- [x] Catch2 全绿（链调度 / 探测缓存 / KeyStore mock / 批量解析）
- [x] `docs/windows/engines/<engine>.md` ×9（端点、参数、错误码映射表）
- [x] 控制台测试入口 `tests/manual/smoke_translation.cpp`
- [x] FR 映射 + macOS 差异清单（上两节；差异 #3/#5/#8 为记录项，#5 带 TODO）
