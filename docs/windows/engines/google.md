# Google Translate — Windows engine card

Google Cloud Translation API v2（`FR-19`）。API key 必填；无免费探测。

- **Raw 值**: `google`
- **默认端点**: `https://translation.googleapis.com/language/translate/v2`
- **凭据**: 必填（`TransFrame/google`）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/CloudProviders.hpp`（`GoogleTranslateProvider`）

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | POST | `https://translation.googleapis.com/language/translate/v2?key=<API_KEY>` |
| 健康探测 | — | 无（有 key → 灰；无 key → 橙） |

key 经 URL query 传递（v2 惯例），经 HTTPS 加密通道。

## 翻译请求

```json
POST /language/translate/v2?key=<API_KEY>
Content-Type: application/json

{
  "q": ["Hello", "World"],
  "format": "text",
  "source": "en",
  "target": "zh"
}
```

- `q` 是字符串数组（一次请求多文本）
- `source` 省略时 API 自动检测
- `format: "text"` 避免被当 HTML 处理（`&` 等字符不被转义）

## 翻译响应

```json
{
  "data": {
    "translations": [
      { "translatedText": "你好", "detectedSourceLanguage": "en" },
      { "translatedText": "世界" }
    ]
  }
}
```

- `translations[i].translatedText` 与请求 `q[i]` 一一对应
- `detectedSourceLanguage` 存在时填充结果的 `sourceLanguage`

## 语言映射（`mapLanguageCode`）

| 输入 | 输出 |
|---|---|
| `zh-Hant`, `zh-TW` | `zh-TW` |
| `zh-Hans`, `zh-CN`, `zh` | `zh`（基础码） |
| `en-US` 等 | 基础码（`en`） |
| 空 / `auto` | `auto`（省略 source 字段 → 自动检测） |

## 错误码映射

| 条件 | ErrorCategory |
|---|---|
| 401 / 403（key 无效） | `auth` |
| 429 | `quota` |
| 5xx / 网络失败 / 取消 | `network` |
| 4xx 其它 | `content`（如 `Google HTTP 400`） |
| 响应无 `data.translations` / JSON 解析失败 | `content` |
| key 缺失 | `configuration` |
| 空文本 | `content`（`Cannot translate empty text`） |

## 与 macOS 差异

无。请求体（`q[]` 数组 + `format` + `source`/`target`）与
`GoogleTranslationProvider.swift` 一致；多文本单请求语义相同。
