# Brief C — Windows 翻译/OCR 服务层（US-W06/07/08/09 + 引擎注册表 + 划词/插入）

你是 TransFrame Windows 版的实现工程师之一。4 个并行子任务共用同一 PRD；你负责 C：OCR 引擎 + 10 个翻译引擎接入 + V2 链式/对比调度 + 划词与插入。

## 必读上下文（按序）
1. `tasks/prd-windows-transframe.md` §6.3（FR-15…17）、§6.4（FR-18…27）、§6.5（FR-28/29）、§3.2（UIA/Credential Manager 映射）
2. Swift 参考实现：`TransFrame/Services/OCREngine*.swift`、`Services/Translation/`（TranslationEngineRegistry/Providers/LLMTranslationProvider/CompatibleTranslationProvider）、`Services/EngineHealthChecker.swift`、`MTranServerEngine.swift`、`Features/TextTranslation/`（TextTranslationFlow/插入流程）
3. 任务 A 的 `windows/Core/` 接口与模型（未就绪则按接口契约先行）

## 交付范围（只做这些，不越界）
在 `windows/NativeShell/`（服务部分）+ `windows/Core/`（纯逻辑）实现：

1. **OCR**：`WindowsOcrEngine`（Windows.Media.Ocr；语言包可用性检测 + TextSegment[] 映射：text+归一化 bbox+confidence）；`PaddleOcrEngine`（外部进程协议，fast/precise，健康检查缓存）；`VlmOcrEngine` 骨架（五 Provider 接口位，HTTP 细节同翻译层复用）
2. **翻译引擎 ×9**（**无 apple**）：MTranServer、OpenAI、Claude、Gemini、Ollama、Google、DeepL、Baidu、Custom 兼容（含多实例 UUID）。统一 `ITranslationProvider`；WinHTTP 封装（超时默认 10s、取消 token、错误分类=网络/配额/鉴权/内容）
3. **调度**：`EngineRegistry` + ordered 链（≤5 逐个回落，聚合错误）+ compare 并行（2-4，结果缓存免重译）；场景绑定路由（screenshot/text_selection/translate_and_insert）
4. **可用性探测**：`EngineHealthChecker`（绿/红/橙/灰；仅轻量端点不烧配额；缓存 TTL 对齐 macOS）
5. **密钥**：`KeyStore`（Credential Manager CredWrite/CredRead，`TransFrame/<engine>/<instance>`；UI 侧显示 `sk-****last4`）
6. **划词**：`TextExtractor`（UIA TextPattern GetSelection 优先 → 模拟 Ctrl+C 剪贴板回退（保存/恢复原剪贴板））；`InsertionService`（UIA 目标重验证 + SendInput Ctrl+V 粘贴回退，重验证失败绝不粘贴）
7. 单元测试：ordered 链回退矩阵（mock provider 注入延迟/失败）、compare 结果缓存、LLM 批量/解析（复用 A 的 fixtures）、UIA 几何（mock）、错误分类映射

## 明确约束
- 不做 UI（设置页/弹窗归后续任务）；提供控制台测试入口 `tests/manual/smoke_translation.cpp` 手工验证
- 已裁决：不内置任何模型 —— Ollama/兼容端点只是 HTTP 接入
- HTTP 层不依赖 UI 框架，可被 Core 测试直接驱动

## 完成标准
- Catch2 全绿（链调度/探测缓存/KeyStore mock/批量解析）
- 每引擎一个请求/响应样例文档 `docs/windows/engines/<engine>.md`（端点、参数、错误码映射表）
- 报告：FR 映射、与 macOS 行为差异点清单（如有，需对齐 PRD §6.12 白名单，超出即报缺陷）
