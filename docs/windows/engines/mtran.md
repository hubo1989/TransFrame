# MTranServer — Windows engine card

本地/自托管 LibreTranslate 兼容引擎（默认链首，`FR-18`）。无需凭据。

- **Raw 值**: `mtran`（与 macOS 设置 JSON 互通）
- **默认端点**: `http://localhost:8989`
- **凭据**: 无（`requiresAPIKey = false`）
- **超时**: 默认 10s（`Configuration.timeout`）
- **实现**: `windows/Core/include/tf/MTranServerProvider.hpp`

## 端点

| 用途 | 方法 | URL | 说明 |
|---|---|---|---|
| 翻译 | POST | `http://<host>:<port>/translate` | JSON body |
| 健康探测 | GET | `http://<host>:<port>/health` → `/` → `/translate` | 探测梯子，2s 探测超时 |

端点配置仅接受 `host`、`host:port`、`http://host[:port]`（`Configuration::parse`）。
带路径/查询/非 HTTP scheme 一律拒绝（`Configuration` 无效 → 引擎不可用）。

## 翻译请求

```json
POST /translate
Content-Type: application/json

{ "from": "en", "text": "Hello", "to": "zh" }
```

多文本时逐条发送（每文本一个请求）；`from`/`to` 经过
`normalizeLanguageCode` 归一（见下）。

## 翻译响应

```json
{ "result": "你好", "detected_language": "en" }
```

- `result` 空或缺失 → `content` 错误（`Empty translation`）
- `detected_language` 存在时填充 `TranslationResult.sourceLanguage`，否则回退请求值

## 语言归一（`normalizeLanguageCode`）

| 输入 | 输出 |
|---|---|
| `zh-Hans`, `zh-CN`, `zh_hans`, `zh_cn` | `zh` |
| `zh-Hant`, `zh-TW`, `zh_hant`, `zh_tw` | `zh-TW` |
| `en-US`, `en-GB`, `en_us`, `en_gb` | `en` |
| `ja-JP`, `ja_jp` | `ja` |
| `ko-KR`, `ko_kr` | `ko` |
| 其它带 `-`/`_` | 取基础码 |
| `auto` | 透传 |

## 健康探测（`/health` 梯子 + 5s TTL 缓存）

`MTranServerHealthCache`（TTL 5s）缓存可达性；`probeEndpoint` 依次尝试
`/health` → `/` → `/translate`：

- 任一 2xx → 可达（`Reachable`）
- 401/403/404/405/429/501 在 `/health`、`/` 上 → 尝试下一级（网关可能只保护部分路径）
- 其它状态/传输失败 → `Unreachable`
- 取消 → `Cancelled`

`EngineHealthChecker` 四态映射：可达 → 绿；不可达 → 红；无需凭据故无橙/灰分支。

## 错误码映射

| 条件 | ErrorCategory | 消息样例 |
|---|---|---|
| 连接拒绝/DNS/超时 | `network` | `MTranServer endpoint unreachable` |
| HTTP 5xx | `network` | `MTranServer HTTP 503` |
| 响应 JSON 解析失败 | `content` | `Invalid MTranServer response` |
| `result` 空/缺失 | `content` | `Empty translation` |
| 取消 | `network` | `cancelled` |
| 端点配置无效 | `configuration` | `Invalid MTranServer URL` |

## 与 macOS 差异

无（PRD §6.12 白名单内）。探测梯子、TTL、语言归一与
`MTranServerEngine.swift` 语义一致；唯一实现差异：探测默认端点固定
`localhost:8989`（`makeDefault`），远程端点的探测由设置层传入实际配置。
