# OpenAI — Windows engine card

Chat-completions LLM 引擎（`FR-19`）。API key 必填；`gpt-4o-mini` 默认模型。

- **Raw 值**: `openai`
- **默认端点**: `https://api.openai.com/v1`（可覆盖 `baseURL`）
- **默认模型**: `gpt-4o-mini`
- **凭据**: 必填（`TransFrame/openai`）；keyless 仅当 `baseURL` 为 localhost（LM Studio / vLLM 预设）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/LLMChatProvider.hpp`（`engine = OpenAI` 分支）

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | POST | `https://api.openai.com/v1/chat/completions` |
| 免费验证 | GET | `https://api.openai.com/v1/models` |

`localhost`/`127.0.0.1` 主机名自动改写为 `127.0.0.1`（`resolveLocalhost`）。

## 翻译请求

```json
POST /v1/chat/completions
Authorization: Bearer sk-...
Content-Type: application/json

{
  "model": "gpt-4o-mini",
  "temperature": 0.3,
  "messages": [
    { "role": "system", "content": "You are a professional translator..." },
    { "role": "user", "content": "Translate the following text from English to Chinese (Simplified)....\n\nText to translate:\nHello\n---\nWorld" }
  ]
}
```

批处理由 `LLMTranslationBatching` 完成：多文本以 `\n---\n` 连接；含分隔符时
prompt 追加「保留 `---` 分隔符」提示。温度默认 0.3（配置可覆盖）。

## 翻译响应

```json
{ "choices": [ { "message": { "content": "你好\n---\n世界" }, "finish_reason": "stop" } ] }
```

- 正文按 `\n---\n` 拆回逐文本结果
- `finish_reason == "length"`（或 SSE `delta.finish_reason == "length"`）→ **截断错误**，
  绝不当成功（macOS `LLMResponseTruncationTests` 语义）
- SSE 流式响应逐段拼接 `delta.content`
- 正文里出现字面 `data:` 行不影响普通 JSON 解析（SSE 判定只认 body 以 `data:` 开头）

## 健康探测

`EngineHealthChecker`：无 key → 橙（不发请求）；有 key → GET `models` 列表（免费）→
200 绿 / 其它红。

## 错误码映射

| 条件 | ErrorCategory |
|---|---|
| 401 / 403 | `auth` |
| 429 | `quota` |
| 5xx / 网络失败 / 取消 | `network` |
| `finish_reason: length`（截断） | `content` |
| 响应无 `choices` / JSON 解析失败 | `content` |
| API key 缺失 | `configuration` |
| key 经非 localhost 的明文 HTTP 发送 | `auth`（HTTPS 守卫拒绝，`guardApiKeyTransport`） |

## 与 macOS 差异

无行为差异。macOS 的 apple 引擎在 Windows 不存在（PRD §6.12）；OpenAI 分支的
端点/头/批处理/截断判定与 `LLMTranslationProvider.swift` 逐条对齐。
