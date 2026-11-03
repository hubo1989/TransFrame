# Gemini — Windows engine card

Google Gemini，经 OpenAI 兼容端点接入（`FR-19`）。API key 必填。

- **Raw 值**: `gemini`
- **默认端点**: `https://generativelanguage.googleapis.com/v1beta`
- **默认模型**: `gemini-2.0-flash`
- **凭据**: 必填（`TransFrame/gemini`）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/LLMChatProvider.hpp`（`engine = Gemini` 分支）

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | POST | `https://generativelanguage.googleapis.com/v1beta/openai/chat/completions` |
| 健康探测 | — | 无（有 key → 灰；无 key → 橙） |

Gemini 走 **OpenAI 兼容层**：请求/响应形状与 OpenAI 相同（`chat/completions`、
Bearer 头、`choices[].message.content` 解析）。

## 翻译请求

```json
POST /v1beta/openai/chat/completions
Authorization: Bearer <gemini-key>
Content-Type: application/json

{
  "model": "gemini-2.0-flash",
  "temperature": 0.3,
  "messages": [ { "role": "user", "content": "Translate the following text...Hello" } ]
}
```

## 翻译响应

```json
{ "choices": [ { "message": { "content": "你好" }, "finish_reason": "stop" } ] }
```

截断/解析语义与 OpenAI 分支一致（`finish_reason: length` → `content` 错误）。

## 错误码映射

| 条件 | ErrorCategory |
|---|---|
| 401 / 403 | `auth` |
| 429 | `quota` |
| 5xx / 网络失败 / 取消 | `network` |
| `finish_reason: length` | `content` |
| 响应异常 / JSON 解析失败 | `content` |
| key 缺失 | `configuration` |
| key 经明文 HTTP 发送（非 localhost） | `auth`（HTTPS 守卫） |

## 与 macOS 差异

无。macOS 亦使用 OpenAI 兼容端点（`v1beta/openai/chat/completions`）。
