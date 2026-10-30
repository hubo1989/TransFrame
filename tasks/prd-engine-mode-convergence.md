# PRD: 翻译引擎模式收敛（顺序退避 + 对比）

日期：2026-10-29
状态：待评审
关联计划：`docs/plans/2026-10-29-engine-mode-convergence-plan.md`

## Overview

将现有四种引擎选择模式（主备 / 并行 / 快切 / 场景绑定）收敛为两种：

1. **顺序模式（ordered）**：配置一条有序引擎链（最多 5 个），按顺序逐级退避——第一个引擎失败自动尝试下一个，直到成功或链尽。
2. **对比模式（compare）**：配置一组引擎（2–4 个）同时翻译，结果并列展示供用户比对选用。

同时完成三项配套收敛：

- **引擎清单收敛**：大模型组的 OpenAI 与 Ollama 合并为「OpenAI 兼容」单入口（内含 OpenAI 官方 / Ollama 本地 / 自定义三个预设模板）；Claude、Gemini 独立保留；云服务（Google / DeepL / 百度）各自保留；兼容接口实例成为一等公民，可直接加入引擎链或对比组。
- **状态点改真实可用性探测**：设置页引擎卡片的绿点 / 橙点不再只反映"是否填过 Key"，改为真实探测引擎端点（本地服务 ping、远端 models/usage 等免费端点），区分「可用 / 异常 / 未配置 / 未验证」。
- **移除半成品**：删除快切模式（服务端从未实现切换逻辑）与场景绑定模式（UI 缺 fallback 入口，三种场景实际配置趋同）。

## Goals

- 用户理解成本从"四种模式 + 共用一份数组"降为"两种模式、各自独立引擎列表"
- 主流程（顺序模式）行为确定：失败逐级退避，可预期
- 对比模式有明确上限（4），UI 与结果展示可控
- 兼容接口实例可多个同时参与翻译，消灭 `customName` 存 JSON 的 hack
- 状态点反映引擎真实可用性，减少"配了但用不了"的排障成本

## Non-goals

- 不改各 Provider 的翻译协议实现（请求格式、鉴权方式不变）
- 不做引擎结果自动择优（对比模式由用户人工比对选择）
- 不做按场景自动路由（场景字段仅保留用于历史记录与统计归属）
- 不改 OCR / 渲染管线

## Quality Gates

每条 User Story 完成时必须满足：

- `./run_tests.sh` 全量通过（`xcodebuild test`，见仓库根 `run_tests.sh`）
- Xcode 项目编译无错误（`xcodebuild build`）
- 旧版本升级场景：用户已配置的引擎凭据（Keychain）、兼容接口实例、主引擎选择在迁移后全部保留

## User Stories

### US-001 顺序模式：配置引擎链，逐级退避

As a 用户, I want 配置一条有序的引擎列表，前者失败时自动退到后者, so that 翻译主流程稳定不中断。

**Acceptance Criteria:**

- [ ] 顺序模式 UI 提供可拖拽排序的引擎列表（链），上限 5 个，至少 1 个
- [ ] 链成员可选自：8 个内置卡片中已配置的引擎 + 兼容接口实例
- [ ] 翻译时按链顺序尝试：第 N 个失败（网络错误 / API 错误 / 超时）自动尝试第 N+1 个
- [ ] 全部失败时抛出聚合错误（保留现有 `MultiEngineError.allEnginesFailed` 行为），错误信息列出每个引擎的失败原因
- [ ] 链中未配置（无凭据）的成员在运行时跳过并记录日志，不阻塞后续成员
- [ ] 成功结果记录实际使用的引擎与各级失败明细（`TranslationResultBundle.results`）

### US-002 对比模式：多引擎并行，结果并列对比

As a 用户, I want 让 2–4 个引擎同时翻译同一段文本并列展示, so that 我可以比对译文质量。

**Acceptance Criteria:**

- [ ] 对比模式 UI 提供引擎多选（chips 或卡片多选），最少 2 个、最多 4 个
- [ ] 已选引擎数 <2 时不可切换到对比模式（控件禁用并提示）
- [ ] 翻译时所有选中引擎并行执行（TaskGroup），互不阻塞
- [ ] 文本翻译弹窗复用现有 `showMultiEngine` 多结果渲染：按引擎分段展示，含延迟与失败原因
- [ ] 截图翻译预览窗在对比模式下提供引擎切换 chips，切换不触发重新翻译
- [ ] 至少一个引擎成功即返回成功（成功结果 + 其余引擎的失败/成功明细）；全部失败抛聚合错误

### US-003 引擎清单收敛：OpenAI 兼容合一

As a 用户, I want OpenAI 与 Ollama 合并为一个「OpenAI 兼容」入口并提侢单击预设, so that 清单更短、心智更简单。

**Acceptance Criteria:**

- [ ] 设置页引擎卡片收敛为 8 张：Apple 翻译、MTranServer、OpenAI 兼容、Claude、Gemini、Google 翻译、DeepL、百度翻译（兼容接口实例区另列）
- [ ] 「OpenAI 兼容」配置面板提供预设模板：OpenAI 官方 / Ollama 本地（自动填 `http://localhost:11434`）/ 自定义，选择后自动填 baseURL 与默认模型
- [ ] 旧 Ollama 配置（baseURL / 模型名）迁移后等价生效于「OpenAI 兼容」
- [ ] Ollama 本地预设不要求 API Key（沿用现有免 Key 逻辑）

### US-004 状态点改真实可用性探测

As a 用户, I want 引擎卡片的状态点反映引擎当前真实可用性, so that 我能立刻发现"本地服务没开 / Key 已失效"。

**Acceptance Criteria:**

- [ ] 状态点四态：`可用`（探测成功，绿）/ `异常`（已配置但探测失败，红）/ `未配置`（缺凭据或缺必填项，橙）/ `未验证`（已配置但无免费探测端点，灰）
- [ ] 探测策略：Apple 本地恒可用；MTranServer 复用 `MTranServerChecker`；OpenAI 兼容 / Claude / Gemini / Ollama 探测 models 列表端点；DeepL 探测 usage 端点；Google / 百度无免费端点，标记 `未验证`
- [ ] 探测结果缓存 60 秒；设置页出现时与配置保存后自动刷新；卡片提供手动刷新
- [ ] 探测请求超时 ≤5 秒，不阻塞设置页交互（异步更新）
- [ ] 每个引擎卡片提供「测试翻译」按钮：发送一条极短真实翻译请求验证端到端可用（消耗配额，按钮旁有提示）

### US-005 旧配置无损迁移

As a 老用户, I want 升级后我的引擎配置自动迁移到新结构, so that 我不需要重新配置。

**Acceptance Criteria:**

- [ ] 模式映射：主备 / 快切 / 场景绑定 → 顺序模式；并行 → 对比模式
- [ ] 顺序链映射：主备取 `parallelEngines[0..2]`；快切换取整个 `parallelEngines`（去重）；场景绑定取截图场景的 primary + fallback（未启用 fallback 则仅 primary）
- [ ] 对比组映射：原并行模式且引擎数 ≥2 时取原 `parallelEngines`（截取 ≤4）；否则由迁移生成的链前 2 个填充
- [ ] 旧 `translationFallbackEnabled == false` 时，迁移后的链只保留 primary 一个
- [ ] 兼容接口实例（最多 5 个）原样保留，成为可加入链 / 对比组的一等成员
- [ ] 迁移幂等：重复启动不产生重复条目；旧 UserDefaults 键保留不删（支持回滚到旧版本读旧值）
- [ ] 迁移单元测试覆盖上述全部映射分支（含空配置、单引擎、越界索引）

### US-006 移除场景绑定与快切

As a 用户, I want 设置页只保留两种模式, so that 界面不再出现实际不生效的选项。

**Acceptance Criteria:**

- [ ] 模式选择器只剩两个分段：顺序 / 对比
- [ ] 场景绑定 UI（`sceneBindingSection`）、快切 UI 及相关状态全部删除
- [ ] `TranslationScene` 枚举保留（历史记录 / 统计用途），不再参与引擎路由
- [ ] 服务端 `translateForQuickSwitch`、`translateByScene` 分支删除，`translate` 的 `selectionMode` 分派收敛为 2 分支
- [ ] `MultiEngineSettingsSection` 中通过 `customName` 存 JSON 引用兼容实例的代码（5 处 `JSONDecoder().decode(CompatibleConfig.self, ...)`）全部消灭

## 依赖与约束

- Keychain 凭据按 `TranslationEngineType` 存取的机制不变；Ollama 合并后其凭据（本就为空）无迁移负担
- `TranslationServicing` 协议签名变更会波及 `TranslationServiceMocks.swift`、`PipelineSmokeTests`、`TranslationServicePipelineTests`，属于计划内改动
- 旧版回滚：新版本写新键不删旧键；回滚安装旧版本后读到的是迁移前的旧值（可能丢失迁移期间的新改动，接受）

## 验收顺序建议

US-005（迁移先行，保数据）→ US-003 → US-001 → US-002 → US-004 → US-006（清理收尾）
