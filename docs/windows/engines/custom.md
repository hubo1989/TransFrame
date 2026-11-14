# Custom（OpenAI 兼容实例）— Windows engine card

用户自定义 OpenAI 兼容端点（`FR-19`），支持**多实例**：每实例独立 UUID 身份
（显示名可改，UUID 稳定）。

- **Raw 值**: `custom`
- **默认端点**: `http://localhost:8000/v1`（LM Studio 常用预设）
- **凭据**: 可选 — `hasAPIKey = false` 或 localhost 端点 → 无 key 可用
  （macOS PR#92 语义：本地服务不应因缺 key 被硬禁用）
- **凭据槽**: `TransFrame/custom:<uuid>`（实例 UUID，显示名变更不影响）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/CompatibleChatProvider.hpp`

## 多实例模型

```
CompatibleConfig {
  id:          "<UUIDv4>"      // 稳定身份：凭据槽、prompt 绑定、链成员
  displayName: "LM Studio"     // 仅 UI 展示
  baseURL:     "http://localhost:8000/v1"
  modelName:   "qwen2.5"
  hasAPIKey:   false
}
```

- 调度链（ordered ≤5 / compare 2-4）里每实例是独立成员（`EngineChainItem::makeCompatible(uuid)`）
- 悬空 UUID（实例已删但链未更新）→ 跳过并记录失败，不炸链
- `resolveConfigurationSnapshot`：以**已保存**的实例配置为准（显示名/URL 改后
  链里的旧快照不生效），无匹配时回退默认

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | POST | `<baseURL>/chat/completions` |
| 免费验证 | GET | `<baseURL>/models` |

`appendingApiSuffix` 智能拼接：`base` 已含 `v1` 时不重复；
`localhost` → `127.0.0.1`。

## 翻译请求

OpenAI 形状，模型名来自实例配置：

```json
POST /v1/chat/completions
Content-Type: application/json
[Authorization: Bearer <key>]   ← 仅 hasAPIKey 且已存 key 时

{
  "model": "qwen2.5",
  "temperature": 0.3,
  "messages": [ { "role": "user", "content": "Translate the following text...Hello" } ]
}
```

## 翻译响应

OpenAI 兼容解析：`choices[0].message.content`；`finish_reason: length` → 截断错误；
批处理/分隔符语义与 LLM 引擎一致（`LLMTranslationCore` 共用）。

## 健康探测

- 悬空 UUID（已删）→ 红
- `hasAPIKey` 且凭据槽空 → 橙
- 其余 → GET `models`（免费）：200 绿 / 失败红

## 错误码映射

| 条件 | ErrorCategory |
|---|---|
| 401 / 403 | `auth` |
| 429 | `quota` |
| 网络失败 / 5xx / 取消 | `network` |
| `finish_reason: length` / 响应异常 | `content` |
| baseURL 无效 | `configuration` |
| key 经非 localhost 明文 HTTP 发送 | `auth`（HTTPS 守卫） |

## 与 macOS 差异

无。多实例 UUID、`custom:<uuid>` 凭据槽、keyless localhost、`models` 免费验证、
快照解析优先已保存配置 — 均与 `CompatibleTranslationProvider.swift`（含 PR#92）
语义一致。
