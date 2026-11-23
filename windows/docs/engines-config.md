# engines.json — Windows 引擎配置（FR-16..24/27）

`%APPDATA%\TransFrame\engines.json` 驱动 Windows 端全部翻译引擎接线：
内置引擎（9 个）、OpenAI 兼容多实例、全局链/对比集、场景绑定与三层
Prompt 覆盖。加载方：`windows/App/EngineBootstrap.cpp`（`tf::app` 命名空间）。

- 文件**可选**：不存在或 JSON 畸形时回退内置默认（MTranServer 单链），
  畸形细节通过 `OutputDebugString` 输出，不影响启动。
- 各字段与 Core 中各 Provider 共用的 `EngineConfig` /
  `CompatibleConfig`（`tf/TranslationProvider.hpp`）一一对应。

## 默认行为（无 engines.json）

| 项 | 值 |
|---|---|
| 启用引擎 | 仅 MTranServer（`http://localhost:8989`） |
| 全局链（ordered） | `[MTranServer]` |
| 全局对比集 | 空 |
| 场景绑定 | 全部未绑定 → 用全局链 |
| Prompt | 全部默认（插入场景用专用简洁默认模板） |

## 字段参考

### `engines` — 内置引擎（FR-18）

数组；`engine` 取值：`mtran | openai | claude | gemini | ollama | google | deepl | baidu`。
同一引擎重复出现时后者覆盖前者。**列出即视为启用**，`enabled: false` 显式停用。

```json
{"engine": "openai", "enabled": true, "baseURL": "https://api.openai.com/v1",
 "modelName": "gpt-4o-mini", "timeout": 30.0, "maxTokens": 2048,
 "temperature": 0.2, "customHeaders": {"X-Title": "TransFrame"}}
```

| 字段 | 类型 | 缺省 | 说明 |
|---|---|---|---|
| `engine` | string | 必填 | 引擎 raw id（见上） |
| `enabled` | bool | `true` | 引擎开关；停用后链解析返回空 → 自动落到下一引擎 |
| `baseURL` | string | 各引擎默认 | 端点。mtran 接受 `host[:port]` 或 `http://host[:port]`；默认 `http://localhost:8989` |
| `modelName` | string | 各引擎默认 | LLM 类引擎（openai/claude/gemini/ollama）模型名 |
| `timeout` | number | 10 | 请求超时（秒） |
| `maxTokens` | int | 2048 | LLM 输出预算（影响批处理分段） |
| `temperature` | number | 引擎默认 | 采样温度 |
| `customHeaders` | object | `{}` | 附加 HTTP 头（string → string） |

未列出的引擎一律视为停用。密钥**不写在本文件**——走 Windows 凭据管理器，
账户名 `TransFrame/<engine>`（兼容实例为 `TransFrame/custom:<uuid>`），
由设置页（后续任务）写入；bootstrap 只接 `CredManKeyStore` 读。

### `compatibleInstances` — OpenAI 兼容多实例（FR-19）

```json
{"id": "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60", "displayName": "Local vLLM",
 "baseURL": "http://localhost:8000/v1", "modelName": "qwen2.5-7b", "hasAPIKey": false}
```

| 字段 | 类型 | 缺省 | 说明 |
|---|---|---|---|
| `id` | UUID string | 必填 | 实例身份；链/绑定/凭证全部以此键接 |
| `displayName` | string | `"Custom"` | 结果弹窗/健康卡片显示名 |
| `baseURL` | string | 必填 | 兼容端点（自动追加 `/chat/completions`、`/models`） |
| `modelName` | string | 空 | 模型名 |
| `hasAPIKey` | bool | `true` | `false` = 免密钥本地端点；`true` 时从凭据管理器 `TransFrame/custom:<uuid>` 读 |

`id` 重复时后者覆盖前者；id 非 UUID 或缺 `baseURL` 的条目视为配置错误
（抛出 → 回退默认）。

### `globalChain` / `globalCompareSet` — 全局选择（FR-20/21）

条目两种写法（可混用）：

```json
{"kind": "builtin", "engine": "mtran"}
{"kind": "compatible", "id": "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60"}
```

同时兼容 macOS V2 导出编码 `{"id":"…","kind":{"builtin":{"_0":"mtran"}}}`。
ordered 链上限 5、对比集 2–4（由 Scheduler 截断/钳制）。`globalChain`
缺省或为空 → 内置默认 `[mtran]`。

### `sceneBindings` — 场景绑定（FR-23）

对象，key 为场景 raw id：`screenshot | text_selection | translate_and_insert`。

```json
{"screenshot": {"mode": "ordered", "engines": [{"kind": "builtin", "engine": "mtran"}]},
 "text_selection": {"mode": "compare", "engines": [...]}}
```

- `mode`：`ordered`（缺省，按序回退）/ `compare`（并行对比）。
- `engines`：同 `globalChain` 条目格式；**空或缺省 = 未绑定**，回退全局链。
- compare 绑定当前由调度器完整执行；结果弹窗 v1 显示首个引擎的译文，
  并排对比 UI 随设置页交付。

### `prompts` — 三层 Prompt 覆盖（FR-24）

```json
{"enginePrompts": {"openai": "…"},
 "compatibleEnginePrompts": {"5e8f9a2b-…": "…"},
 "scenePrompts": {"translate_and_insert": "…"}}
```

优先级：**scene > compatible-instance > engine > 默认**（与 macOS
`promptPreview` 一致）。模板占位符：`{source_language}` `{target_language}`
`{text}`。默认模板见 `tf/TranslationPromptConfig`（插入场景专用简洁默认）。

## 环境变量（调试覆盖）

| 变量 | 作用 |
|---|---|
| `TRANSFRAME_MTRAN_URL` | **优先于 engines.json** 覆盖 mtran 端点（继承 v1 行为，调试用途） |
| `TRANSFRAME_TARGET_LANG` | 目标语言（默认 `zh`） |

## 健康状态查询（FR-22，设置页用）

`tf::app::EngineBootstrap` 构造时即挂好 `EngineHealthChecker`
（`DefaultEngineProbe` + 共享 transport + 凭据存储），设置页后续可直接：

```cpp
g.engines->healthOf(tf::TranslationEngineType::MtranServer);   // 内置引擎
g.engines->healthOfCompatible(instanceUuid);                   // 兼容实例
```

四态：绿=可用 / 红=不可达 / 橙=未配置（缺密钥）/ 灰=无免费探测端点
（Google/Baidu 有密钥时）。探测只用免费端点（mtran `/health`、LLM models
列表），**绝不调用计费翻译接口**；结果缓存 TTL 60s。

## 完整示例

```json
{
  "version": 1,
  "engines": [
    {"engine": "mtran", "enabled": true, "baseURL": "http://localhost:8989"},
    {"engine": "openai", "enabled": false, "modelName": "gpt-4o-mini", "timeout": 30.0}
  ],
  "compatibleInstances": [
    {"id": "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60", "displayName": "Local vLLM",
     "baseURL": "http://localhost:8000/v1", "modelName": "qwen2.5-7b", "hasAPIKey": false}
  ],
  "globalChain": [
    {"kind": "builtin", "engine": "mtran"},
    {"kind": "compatible", "id": "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60"}
  ],
  "globalCompareSet": [],
  "sceneBindings": {
    "translate_and_insert": {"mode": "ordered", "engines": [
      {"kind": "compatible", "id": "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60"},
      {"kind": "builtin", "engine": "mtran"}
    ]}
  },
  "prompts": {
    "compatibleEnginePrompts": {
      "5e8f9a2b-1c3d-4e5f-8a90-1b2c3d4e5f60": "Translate to {target_language}:\n{text}"
    },
    "scenePrompts": {
      "translate_and_insert": "Translate from {source_language} to {target_language}.\nKeep it plain text for cursor insertion.\n\n{text}"
    }
  }
}
```

## 校验规则速查

| 情形 | 行为 |
|---|---|
| 文件不存在 | 内置默认 |
| JSON 畸形 / 根非对象 | 抛 `std::runtime_error` → App 回退默认 + debug 输出 |
| 未知 `engine` / 场景 key / 链条目 | 抛出（同上回退），不静默丢弃 |
| 引擎行缺字段 | 该字段用 Provider 默认 |
| `globalChain` 空或缺省 | 回退内置默认 `[mtran]` |
| 场景绑定 `engines` 空 | 视为未绑定 → 回退全局链 |
| 实例 `id` 非 UUID / 缺 `baseURL` | 抛出（同上回退） |
