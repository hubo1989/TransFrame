# 翻译引擎模式收敛 - 实现计划

**基于 PRD**: `tasks/prd-engine-mode-convergence.md`
**创建日期**: 2026-10-29

---

## Phase 0: 基线确认（已完成）

### 现有代码盘点（重构波及面）

| 组件 | 文件 | 改动量 |
|------|------|--------|
| 模式枚举 | `TransFrame/Models/EngineSelectionMode.swift` | 重写（4 case → 2 case） |
| 引擎类型 | `TransFrame/Models/TranslationEngineType.swift` | 中改（合并 openai/ollama 预设、删 gemini？否，仅合并入口不删 case） |
| 场景绑定 | `TransFrame/Models/SceneEngineBinding.swift` | 删除 |
| 设置持久化 | `TransFrame/Models/AppSettings.swift`（7 处 parallelEngines 引用 + sceneBindings + engineSelectionMode） | 大改（新键 + 迁移） |
| 服务分派 | `TransFrame/Services/TranslationService.swift`（4 分支 → 2 分支） | 大改 |
| 服务协议 | `TransFrame/Services/Protocols/TranslationServicing.swift` | 中改（签名收敛） |
| 管线配置 | `TransFrame/Features/TranslationFlow/ImageTranslationPipeline.swift` | 中改 |
| 文本流程 | `TransFrame/Features/TextTranslation/TextTranslationFlow.swift` | 中改 |
| 渲染管线 | `TransFrame/Services/Pipelines/TranslationRenderPipeline.swift` | 小改 |
| 设置 UI | `TransFrame/Features/Settings/MultiEngineSettingsSection.swift`（972 行，含 5 处 customName JSON hack） | 重写主体 |
| 配置面板 | `TransFrame/Features/Settings/EngineConfigSheet.swift` | 中改（OpenAI 兼容预设模板） |
| 兼容面板 | `TransFrame/Features/Settings/CompatibleEngineConfigSheet.swift` | 小改 |
| Registry | `TransFrame/Services/Translation/TranslationEngineRegistry.swift` | 小改（兼容实例一等公民化辅助） |
| 可用性探测 | 无（新建） | 新建 `EngineHealthChecker` |
| 测试 | `TransFrameTests/`（TranslationServicePipelineTests 24.5K、TranslationEngineRegistryTests 16.2K、TranslationConfigurationTests 7.6K、PRDAcceptanceTests 36.2K 等） | 大改 |

### 决策记录

- **不删 `TranslationEngineType.openai` / `.ollama` case**：Keychain 凭据、UserDefaults、EngineConfigSheet 的 `switch engine` 分支都依赖 rawValue；收敛是 UI 层面（一张卡片、三个预设模板），数据层两个 case 保留，`.ollama` 卡片从设置页隐藏、由「OpenAI 兼容」卡片代理配置。
- **`EngineRef` 不新建枚举**：兼容实例已有稳定 UUID（`CompatibleConfig.id`），链/对比组直接存 `[EngineChainItem]`，其中 compatible 用 UUID 引用、内置用 `TranslationEngineType`，Codable 单结构体即可，避免 enum-with-associated-value 的 Codable 样板。
- **旧键不删**（PRD US-005 回滚约束），新键以 `engineChainV2` / `compareEnginesV2` 命名，读取时若 V2 缺失且旧键存在则执行一次性迁移并落盘 V2。
- **快切/场景绑定的删除放在最后一个 Phase**：先让新结构并行可用，测试全绿后再拆旧分支，避免中途长时间红。

---

## Phase 1: 数据模型与迁移（US-005）

### 1.1 新模型

```swift
// TransFrame/Models/EngineSelectionV2.swift（新文件）
struct EngineChainItem: Codable, Equatable, Sendable, Identifiable {
    var id: UUID = UUID()                    // 条目自身 ID（同引擎可重复入链）
    var kind: Kind                           // .builtin(TranslationEngineType) | .compatible(UUID)
    enum Kind: Codable, Equatable, Sendable {
        case builtin(TranslationEngineType)
        case compatible(UUID)
    }
}

enum EngineModeV2: String, Codable, CaseIterable, Sendable {
    case ordered   // 顺序退避
    case compare   // 并行对比
}
```

- 上限常量：`orderedChainLimit = 5`，`compareEnginesLimit = 4`（含下限 `compareEnginesMinimum = 2`）。
- 同一引擎允许重复入链（沿用现快切 UI 注释里"主/备同引擎"的用例）。

### 1.2 AppSettings 新增（保留全部旧属性不删）

```swift
// Keys 增加
static let engineModeV2 = prefix + "engineModeV2"
static let engineChainV2 = prefix + "engineChainV2"          // [EngineChainItem]
static let compareEnginesV2 = prefix + "compareEnginesV2"    // [EngineChainItem]

var engineModeV2: EngineModeV2 { get set }        // didSet 持久化
var engineChainV2: [EngineChainItem] { get set }
var compareEnginesV2: [EngineChainItem] { get set }
```

### 1.3 迁移函数（一次性、幂等）

`AppSettings.migrateLegacyEngineSettingsIfNeeded()`，在 `loadSettings()` 尾部调用：

```
若 engineChainV2 已存在（UserDefaults 有值）→ 直接返回（幂等）

mode 映射:
  旧 .primaryWithFallback / .quickSwitch / .sceneBinding → .ordered
  旧 .parallel → .compare

ordered 链构建:
  .primaryWithFallback → [parallelEngines[0], parallelEngines[1]]（越界即止）
  .quickSwitch         → parallelEngines 去重保序（≤5）
  .sceneBinding        → sceneBindings[.screenshot] 的 primaryEngine
                         + fallbackEngine（fallbackEnabled == true 时）
  translationFallbackEnabled == false → 只保留第一个
  空链兜底 → [translationEngine（旧单引擎设置）或 .apple]

compare 组构建:
  旧 .parallel 且 parallelEngines.count ≥ 2 → 取前 4
  否则 → ordered 链前 2 个（不足 2 用 .apple 补齐）

compatible 实例: 旧 .custom 在链/组中的位置，解析 customName JSON 取
CompatibleConfig.id → 替换为 .compatible(UUID)；解析失败则丢弃该条目并记日志
```

### 1.4 测试（新文件 `TransFrameTests/EngineSettingsMigrationTests.swift`）

- 每个 mode 映射分支一测（4 例）
- fallbackEnabled=false 截断一测
- 越界 / 空配置 / 单引擎 / custom-JSON 条目解析成功与失败各一测
- 幂等：连续调用两次，第二次不改动 V2 值
- 旧键在迁移后保持原值未被删除

**完成标志**: `xcodebuild test -only-testing:TransFrameTests/EngineSettingsMigrationTests` 通过

---

## Phase 2: 引擎清单收敛（US-003）

### 2.1 设置页卡片布局（MultiEngineSettingsSection.enginesSection）

```
内置:        Apple 翻译 | MTranServer
大模型:      OpenAI 兼容 | Claude | Gemini
云服务:      Google 翻译 | DeepL | 百度翻译
兼容接口:    （动态实例卡片，≤5，不变）
```

- `TranslationEngineType.allCases` 过滤出 UI 卡片集合：`uiCards: [TranslationEngineType]` 静态列表 = `[.apple, .mtranServer, .openai, .claude, .gemini, .google, .deepl, .baidu]`（`.ollama`、`.custom` 不出现）。

### 2.2 EngineConfigSheet：OpenAI 兼容预设模板

`case .openai` 配置面板顶部加 `Picker` 预设：

| 预设 | 写入 baseURL | 写入默认模型 | Key 要求 |
|------|-------------|-------------|----------|
| OpenAI 官方 | `https://api.openai.com/v1` | gpt-4o-mini | 必填 |
| Ollama 本地 | `http://localhost:11434` | llama3 | 免 |
| 自定义 | 用户填 | 用户填 | 视 baseURL |

实现方式：预设选择写入 `config.options.baseURL`；当 baseURL == Ollama 端口时，保存时把配置落到 `engineConfigs[.ollama]`（代理写入），OpenAI 官方 / 自定义落 `.openai`。`isConfigured` 判断对 `.openai` 卡片 = openai 或 ollama 任一已配置。

### 2.3 旧 Ollama 数据迁移（并入 Phase 1 迁移函数）

链/组中出现 `.ollama` → 条目原样保留（`.builtin(.ollama)` 仍可执行，Provider 未删）；仅设置页不再单独展示 Ollama 卡片。

### 2.4 测试

- `uiCards` 断言 8 张且不含 ollama/custom
- OpenAI 兼容预设写入 → ollama 代理写入路径的单元测试（模拟 Keychain）
- 旧 `.ollama` 条目在新结构下仍能创建 Provider（Registry 冒烟）

**完成标志**: 相关测试通过；设置页渲染 8 张卡片

---

## Phase 3: 顺序模式（US-001）

### 3.1 UI（MultiEngineSettingsSection 重写 orderedSection）

- 引擎链列表：行 = 序号 + 引擎名 + 替换菜单 + 删除按钮（复用现 quickSwitchSection 的行交互模式，含"同引擎可重复"的组合 ID 处理）
- 上限 5：满员后隐藏添加按钮，显示上限提示
- 添加候选 = 8 张已配置卡片 + 全部兼容实例
- 拖拽排序（`.onMove`）或上/下移按钮（二选一，拖拽优先）

### 3.2 服务端（TranslationService）

```swift
// 分派收敛后:
case .ordered:  translateOrdered(segments:…, chain: [EngineChainItem], …)
case .compare:  translateCompare(segments:…, engines: [EngineChainItem], …)
```

`translateOrdered` 语义（替换现 translateWithFallback + translateForQuickSwitch + translateByScene 三个函数）：

```
for item in chain:
    resolve provider（builtin → Registry.provider(for:)；
                     compatible → Registry.createCompatibleProvider(id)）
    guard provider.isAvailable（未配置/不可用 → 记录 skipped，continue）
    do  → 返回成功 bundle（results = [成功] + [已尝试失败明细]）
    catch → 记录 EngineResult.failed，continue
全部失败 → throw MultiEngineError.allEnginesFailed(errors)
```

- 现有 `translateWithFallback` 的 "Skip fallback if the engine is not explicitly enabled"（TranslationService.swift:179）逻辑由链成员资格天然取代。

### 3.3 协议与调用点

`TranslationServicing.translate` 签名改为 `(segments, to, from, scene, modeV2, chain/engines)`；同步更新：

- `ImageTranslationPipelineConfig.fromAppSettings`（preferredEngine 解析逻辑简化为链首位）
- `TextTranslationFlow` 两处 config 构造 + `resolvePreferredEngine`
- `TranslationRenderPipeline` 两处调用
- `SelectionOverlayWindow` / `TranslationFlowController` / `PreviewViewModel+Translation`（间接经 config，预计仅类型适配）

### 3.4 测试

- 链 [A,B,C]：A 失败 → B 成功，bundle.results 含 A 失败 + B 成功
- 首个成功后不再调用后续（mock 计数断言）
- 链中不可用成员被跳过不阻塞
- 全部失败抛 `allEnginesFailed` 且错误数 = 链长
- compatible UUID 条目正确解析到对应实例 Provider
- 空链防御：回退 `.apple`

**完成标志**: `TranslationServicePipelineTests` 新增用例通过；文本/截图两条主流程在新模式下端到端可用（PipelineSmokeTests 适配后通过）

---

## Phase 4: 对比模式（US-002）

### 4.1 UI

- 引擎多选 chips（FlowLayout 复用）：已配置卡片 + 兼容实例，2–4 个
- 选中数 <2：模式切换控件禁用 + 提示文案；已处于 compare 时删到 1 个 → 自动弹提示并阻止
- 每行结果 chip 显示引擎名 + 延迟 + 成败图标

### 4.2 服务端 translateCompare

- 替换现 `translateParallel`：TaskGroup 并发执行全部条目；条目不可用 → 生成 `EngineResult.failed(error: EngineUnavailableError)` 并入组，不中断其他
- ≥1 成功 → 成功 bundle；全失败 → `allEnginesFailed`

### 4.3 结果展示

- `TextTranslationPopupView`：`showMultiEngine` 已按 `engineResults` 分段渲染（现状可复用），补延迟徽标
- 截图预览（`PreviewContentView` / `PreviewResultsPanel`）：compare 模式下加引擎切换 chips，切换只在已缓存 `bundle.results` 间切换视图，不重新翻译

### 4.4 测试

- 3 引擎并发：1 失败 2 成功 → bundle 成功且 results.count == 3
- 上限 4 下限 2 的 UI 约束单测
- 预览切换不触发重复翻译（调用计数）

**完成标志**: 对比模式端到端可用，多结果渲染正确

---

## Phase 5: 可用性探测（US-004）

### 5.1 新建 `TransFrame/Services/EngineHealthChecker.swift`

```swift
enum EngineHealth: Equatable, Sendable {
    case available            // 绿
    case misconfigured(String)?  // 红：已配置但探测失败
    case unconfigured         // 橙：缺凭据/必填
    case unverified           // 灰：无免费探测端点
    case checking             // 探测中（UI 过渡态）
}

actor EngineHealthChecker {
    static let shared = EngineHealthChecker()
    private var cache: [String: (health: EngineHealth, at: Date)] = [:]
    let ttl: TimeInterval = 60
    func health(for item: EngineRef) async -> EngineHealth
    func refresh(_ item: EngineRef) async -> EngineHealth   // 强制绕过缓存
}
```

探测端点策略：

| 引擎 | 端点 | 成功判据 |
|------|------|---------|
| apple | — | 恒 `.available` |
| mtranServer | 复用 `MTranServerChecker.isAvailable` | true→available |
| openai/兼容实例/ollama | `GET {baseURL}/models` | HTTP 200 |
| claude | `GET {baseURL}/models`（带 key） | HTTP 200 |
| gemini | `GET …/v1beta/models?key=` | HTTP 200 |
| google / baidu | 无免费端点 | `.unverified` |
| deepl | `GET {baseURL}/usage`（带 key，免费额度端点） | HTTP 200 |

- 未配置（requiresAPIKey 且 Keychain 无凭据）→ 直接 `.unconfigured`，不发请求
- 超时 5s；失败 → `.misconfigured(原因摘要)`

### 5.2 UI 接入（MultiEngineSettingsSection.engineCard）

- 替换 `checkConfiguredEngines()` 的静态集合逻辑：`@State healthCache: [EngineRef: EngineHealth]`
- `onAppear` / 配置保存回调 / 手动刷新按钮 → `EngineHealthChecker.refresh`
- 四态颜色映射 + tooltip（异常时显示失败原因摘要）
- 新增「测试翻译」按钮：走真实 translate("OK") 极短请求，结果 toast；按钮旁永久提示会消耗配额

### 5.3 测试

- mock URLProtocol：200/401/超时三分支
- unconfigured 短路（不触网）
- 缓存 TTL 内不重复请求（计数断言）
- google/baidu 恒 unverified

**完成标志**: 探测单测通过；设置页状态点随真实端点状态变化

---

## Phase 6: 旧结构拆除（US-006）

前置：Phase 1–5 全部合入且测试全绿。

- [ ] 删除 `EngineSelectionMode`（替换为 `EngineModeV2`，文件重命名）
- [ ] 删除 `SceneEngineBinding.swift`、`TranslationServicing` 旧签名、`TranslationService` 中 `translateWithFallback/translateForQuickSwitch/translateByScene/translateParallel` 旧实现（被 Phase 3/4 新函数取代后）
- [ ] `MultiEngineSettingsSection` 删除 quickSwitchSection / sceneBindingSection / primaryFallbackSection / parallelEnginesSection 及 5 处 `customName` JSON hack（约 −400 行，预计文件降到 ~550 行）
- [ ] `AppSettings` 旧属性（engineSelectionMode/parallelEngines/sceneBindings/translationFallbackEnabled）改为只读迁移源，不再 didSet 持久化；UserDefaults 旧键不删
- [ ] `TranslationResultBundle.selectionMode` 字段类型更新；`EngineResult.engine: TranslationEngineType` 扩为可携带 compatible 实例标识（display 用 displayName）
- [ ] 全仓 grep 清零：`engineSelectionMode|parallelEngines|sceneBindings|quickSwitch|customName`（除迁移代码与测试）
- [ ] 更新 `TransFrameTests/PRDAcceptanceTests.swift` 中旧模式相关断言
- [ ] README 截图与说明段落更新（中英两份）

**完成标志**: grep 清零；`./run_tests.sh` 全量绿；编译无警告新增

---

## Phase 间依赖

```
Phase 1 (迁移) ──→ Phase 2 (清单) ──→ Phase 3 (顺序) ──→ Phase 4 (对比) ──→ Phase 6 (拆除)
                                              └──────→ Phase 5 (探测，可与 4 并行) ──┘
```

## 风险与对策

| 风险 | 对策 |
|------|------|
| PRDAcceptanceTests（36K）大量绑定旧模式行为 | Phase 1 起逐 phase 适配，Phase 6 统一清尾；不一次性大改 |
| ollama 代理写入（写入 `.ollama` 配置）引入的状态分裂 | 单一判断函数 `effectiveEngine(for baseURL)`；测试覆盖两条路径 |
| 兼容实例被删除后链中残留悬空 UUID | 解析时跳过 + 日志；设置页对悬空条目显示"已失效"占位行 |
| Keychain 里旧 `.custom` 复合凭据 | 迁移时不搬 Keychain（createCompatibleProvider 按现机制走 `custom:N` 快照），仅迁移选择状态 |
| 截图预览对比切换 chips 的状态管理 | 只读缓存切换，不引入新翻译任务；用 `bundle.results` 已有字段驱动 |
