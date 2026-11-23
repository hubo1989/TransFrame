# DeepL — Windows engine card

DeepL Translation API（`FR-19`）。API key 必填；免费版经 `api-free.deepl.com` 端点。

- **Raw 值**: `deepl`
- **默认端点**: `https://api.deepl.com/v2/translate`（`api-free.deepl.com` 可配置）
- **凭据**: 必填（`TransFrame/deepl`）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/CloudProviders.hpp`（`DeepLProvider`）

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | POST | `https://api.deepl.com/v2/translate` |
| 健康探测 | — | 无（有 key → 灰；无 key → 橙；验证会翻译 "1" 消耗配额，不探测） |

`normalizeEndpoint`：`baseURL` 去尾斜杠后补 `/translate`（已有则不重复）；
免费版填 `https://api-free.deepl.com/v2`。

## 翻译请求

```json
POST /v2/translate
Authorization: DeepL-Auth-Key <key>
Content-Type: application/json

{ "text": ["Hello"], "target_lang": "DE" }
```

- `text` 是字符串数组
- `source_lang` 省略时自动检测（小写；`auto` → 省略）
- `target_lang` 大写（DeepL API 规则，`targetCode`）

## 语言大小写规则

| 输入 | source（小写） | target（大写） |
|---|---|---|
| `zh-Hans` / `zh` | `zh` | `ZH` |
| `zh-Hant` / `zh-TW` | `zh-hant` | `ZH-HANT` |
| `en-US` / `en-GB` | `en-us` | `EN` |
| `auto` / 空 | 省略 | — |

## 翻译响应

```json
{ "translations": [ { "detected_source_language": "EN", "text": "Hallo" } ] }
```

`detected_source_language` 大写，直接透传到结果的 `sourceLanguage`。

## 错误码映射

| 条件 | ErrorCategory | 消息 |
|---|---|---|
| 403 | `auth` | `DeepL: invalid API key` |
| 456（配额耗尽，DeepL 专有码） | `quota` | `DeepL: quota exceeded (HTTP 456)` |
| 401 | `auth` | （statusError 映射） |
| 429 | `quota` | （statusError 映射） |
| 5xx / 网络失败 / 取消 | `network` | |
| 4xx 其它 | `content` | |
| 响应无 `translations` / JSON 解析失败 | `content` | `Invalid DeepL response` |
| key 缺失 | `configuration` | `DeepL API key not configured` |

## 与 macOS 差异

无。`DeepL-Auth-Key` 头、大小写规则、456 配额映射与
`DeepLTranslationProvider.swift` 一致。
