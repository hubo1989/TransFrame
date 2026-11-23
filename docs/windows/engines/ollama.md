# Ollama — Windows engine card

本地 LLM 引擎（`FR-19`）。OpenAI 兼容端点，无需 key，`llama3` 默认模型。

- **Raw 值**: `ollama`
- **默认端点**: `http://localhost:11434`
- **默认模型**: `llama3`
- **凭据**: 无（keyless 本地端点）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/LLMChatProvider.hpp`（`engine = Ollama` 分支）

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | POST | `http://127.0.0.1:11434/v1/chat/completions` |
| 免费验证 | GET | `http://127.0.0.1:11434/v1/models` |

`localhost` 自动改写为 `127.0.0.1`；无 `Authorization` 头（keyless）。

## 翻译请求

与 OpenAI 形状一致（`/v1/chat/completions`、`messages` 数组、`temperature`），
但不带 `Authorization` 头：

```json
POST /v1/chat/completions
Content-Type: application/json

{
  "model": "llama3",
  "temperature": 0.3,
  "messages": [ { "role": "user", "content": "Translate the following text...Hello" } ]
}
```

## 翻译响应

OpenAI 兼容：`choices[0].message.content`；`finish_reason: length` → 截断错误
（同 OpenAI 分支解析器）。

## 健康探测

无需凭据 → 永不橙。GET `v1/models`（免费）：200 → 绿；连接拒绝 → 红。

## 错误码映射

| 条件 | ErrorCategory |
|---|---|
| 401 / 403 | `auth` |
| 429 | `quota` |
| 连接拒绝 / 5xx / 取消 | `network` |
| `finish_reason: length` | `content` |
| 响应异常 / JSON 解析失败 | `content` |

## 与 macOS 差异

无。keyless 语义、`v1/models` 探测、明文 HTTP 允许（localhost，不走 HTTPS 守卫，
因为无 key 可泄露）。
