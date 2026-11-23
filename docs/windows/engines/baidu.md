# Baidu Translate — Windows engine card

百度翻译开放平台 API（`FR-19`）。**AppID + 密钥** 双凭据；MD5 签名。

- **Raw 值**: `baidu`
- **默认端点**: `https://fanyi-api.baidu.com/api/trans/vip/translate`
- **凭据**: AppID + 密钥必填（`TransFrame/baidu`，同一 blob 内 `appID` + `apiKey`）
- **超时**: 10s 默认
- **实现**: `windows/Core/include/tf/CloudProviders.hpp`（`BaiduProvider`）

## 端点

| 用途 | 方法 | URL |
|---|---|---|
| 翻译 | GET | `https://fanyi-api.baidu.com/api/trans/vip/translate?q=...&from=...&to=...&appid=...&salt=...&sign=...` |
| 健康探测 | — | 无（有凭据 → 灰；缺 → 橙） |

## 签名（`makeSign`）

```
sign = MD5( appid + q + salt + secretKey )
```

- `salt`：每次请求随机 6 位数字（100000…999999）
- `q` 参与签名的是**原文**（未 URL 编码）
- 签名已知向量（RFC 1321）：
  - `MD5("abc")` = `900150983cd24fb0d6963f7d28e17f72`
  - `MD5("")` = `d41d8cd98f00b204e9800998ecf8427e`

## 翻译请求

每文本一个 GET（`q` 为单字符串，多文本逐条 — 与 macOS 一致）：

```
GET /api/trans/vip/translate?q=Hello&from=en&to=zh&appid=20231205000&salt=483920&sign=<md5>
```

`q`、`from`、`to` 等参数 URL 编码。

## 翻译响应

```json
{ "from": "en", "to": "zh", "trans_result": [ { "src": "Hello", "dst": "你好" } ] }
```

## 语言映射（`mapLanguageCode`）

| 输入 | 输出 |
|---|---|
| `zh-Hant`, `zh-TW` | `cht` |
| `zh-Hans`, `zh-CN`, `zh` | `zh` |
| `ja` | `jp` |
| `ko` | `kor` |
| `fr` | `fra` |
| `es` | `spa` |
| `de` / `pt` / `ru` / `it` / `en` | 原样 |
| 空 / `auto` | `auto` |
| 其它 | 原样（小写化后透传） |

## 错误码映射（`error_code` 字符串）

| `error_code` | ErrorCategory | 含义 |
|---|---|---|
| `54003` | `quota` | QPS 超限（速率） |
| `54004` | `auth` | 账户余额不足 / key 无效 |
| `52001` / `52002` 等 | `network` | 上游/系统错误 |
| 其它非空码 | `auth`（含码+`error_msg`） | |
| 响应无 `trans_result` / JSON 解析失败 | `content` | `Invalid Baidu response` |
| AppID 或密钥缺失 | `configuration` | `Baidu credentials incomplete` |

## 与 macOS 差异

无。签名算法（MD5 实现为内置、不依赖平台 API）、salt 范围、逐文本 GET、
`54003`/`54004` 映射与 `BaiduTranslationProvider.swift` 一致。
