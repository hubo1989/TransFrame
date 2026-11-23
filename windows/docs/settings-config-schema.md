# TransFrame 设置持久化 Schema 草案（Windows engines.json 采用）

> 来源：`TransFrame/Models/AppSettings.swift` Keys + 各模型层。macOS 用 UserDefaults（前缀 `TransFrame.`）；Windows 端建议以单一 JSON 文件承载非敏感字段，敏感字段走 Credential Manager / DPAPI。本 schema 键名与 macOS UserDefaults 键一一对应（去掉 `TransFrame.` 前缀即为字段名），敏感字段除外。

## 1. 顶层结构（Windows 配置文件草案）

```json
{
  "saveLocation": "C:\\Users\\<user>\\Desktop",
  "appLanguage": "system",
  "translationSourceLanguage": "auto",
  "translationTargetLanguage": null,
  "translateAndInsertSourceLanguage": "auto",
  "translateAndInsertTargetLanguage": null,
  "vlmProvider": "openai",
  "vlmOpenAIMode": "chat",
  "vlmBaseURLByProvider": { "openai": "https://api.openai.com/v1" },
  "vlmModelNameByProvider": { "openai": "gpt-4o-mini" },
  "paddleOCRMode": "fast",
  "paddleOCRUseCloud": false,
  "paddleOCRCloudBaseURL": "",
  "paddleOCRCloudModelId": "",
  "engineModeV2": "ordered",
  "engineChainV2": [],
  "compareEnginesV2": [],
  "engineConfigs": {},
  "compatibleProviderConfigs": [],
  "promptConfig": { "enginePrompts": {}, "compatibleEnginePrompts": {}, "scenePrompts": {} },
  "preferredTranslationEngine": "apple",
  "mtranServerURL": "",
  "shortcuts": {
    "fullScreenShortcut": { "keyCode": 19, "modifiers": 4608 },
    "selectionShortcut": { "keyCode": 21, "modifiers": 4608 },
    "translationModeShortcut": { "keyCode": 17, "modifiers": 4608 },
    "textSelectionTranslationShortcut": { "keyCode": 16, "modifiers": 4608 },
    "translateAndInsertShortcut": { "keyCode": 34, "modifiers": 4608 },
    "pickColorShortcut": { "keyCode": 8, "modifiers": 4096 }
  },
  "advanced": {
    "strokeColor": "#FF3B30",
    "strokeWidth": 2.0,
    "textSize": 14.0
  },
  "misc": {
    "defaultFormat": "png",
    "jpegQuality": 0.9,
    "heicQuality": 0.9,
    "ocrEngine": "vision",
    "translationMode": "below",
    "translationAutoDetect": true,
    "onboardingCompleted": false,
    "userSkippedPermissions": false,
    "rectangleFilled": false,
    "ellipseFilled": false,
    "mosaicBlockSize": 10
  }
}
```

## 2. 字段表

### 2.1 通用

| 字段 | 类型 | 默认 | 范围/枚举 | macOS 键 |
|---|---|---|---|---|
| saveLocation | string (路径) | 桌面 | 任意存在的目录 | `TransFrame.saveLocation` |
| appLanguage | string | `"system"` | `system`/`en`/`zh-Hans`/`de`/`es`/`fr`/…（AppLanguage.allCases） | 由 LanguageManager 管理 [待确认] |

### 2.2 语言

| 字段 | 类型 | 默认 | 范围/枚举 | macOS 键 |
|---|---|---|---|---|
| translationSourceLanguage | string | `"auto"` | `auto,en,zh-Hans,zh-Hant,ja,ko,fr,de,es,it,pt,ru,ar,hi,th,vi,nl,pl,tr,uk,cs,sv,da,fi,no,el,he,id,ms,ro` | `TransFrame.translationSourceLanguage` |
| translationTargetLanguage | string \| null | null（跟随系统） | 同上非 auto | `TransFrame.translationTargetLanguage`（null=键不存在） |
| translateAndInsertSourceLanguage | string | `"auto"` | 同上 | `TransFrame.translateAndInsertSourceLanguage` |
| translateAndInsertTargetLanguage | string \| null | null | 同上非 auto | `TransFrame.translateAndInsertTargetLanguage` |
| translationAutoDetect | bool | true | — | `TransFrame.translationAutoDetect`（读取用） |

### 2.3 VLM（图像理解）

| 字段 | 类型 | 默认 | 范围/枚举 | macOS 键 |
|---|---|---|---|---|
| vlmProvider | string | `"openai"` | `openai`/`claude`/`gemini`/`ollama`/`paddleocr` | `TransFrame.vlmProvider` |
| vlmOpenAIMode | string | `"chat"` | `chat`/`response` | `TransFrame.vlmOpenAIMode` |
| vlmBaseURLByProvider | object<string,string> | 每 provider 默认：openai=`https://api.openai.com/v1`，claude=`https://api.anthropic.com/v1`，gemini=`https://generativelanguage.googleapis.com/v1beta`，ollama=`http://localhost:11434` | URL | `TransFrame.vlmBaseURL.<provider>` |
| vlmModelNameByProvider | object<string,string> | openai=`gpt-4o-mini`，claude=`claude-sonnet-4-20250514`，gemini=`gemini-2.0-flash`，ollama=`llama3` | 字符串 | `TransFrame.vlmModelName.<provider>` |
| vlmAPIKey（敏感） | object<string,string> | {} | per-provider | macOS Keychain；旧明文 `TransFrame.vlmAPIKey` 仅迁移用 |

### 2.4 PaddleOCR

| 字段 | 类型 | 默认 | 范围/枚举 | macOS 键 |
|---|---|---|---|---|
| paddleOCRMode | string | `"fast"` | `fast`/`precise` | `TransFrame.paddleOCRMode` |
| paddleOCRUseCloud | bool | false [待确认] | — | `TransFrame.paddleOCRUseCloud` |
| paddleOCRCloudBaseURL | string | "" | URL | `TransFrame.paddleOCRCloudBaseURL` |
| paddleOCRCloudModelId | string | "" | 字符串 | `TransFrame.paddleOCRCloudModelId` |
| paddleOCRCloudAPIKey（敏感） | string | "" | — | Keychain [待确认：`TransFrame.paddleOCRCloudAPIKey` UserDefaults 键已定义但是否使用] |
| paddleOCRServerAddress | string | "" | URL（旧） | `TransFrame.paddleOCRServerAddress` |

### 2.5 多引擎

| 字段 | 类型 | 默认 | 范围/枚举 | macOS 键 |
|---|---|---|---|---|
| engineModeV2 | string | `"ordered"` | `ordered`/`compare` | `TransFrame.engineModeV2` |
| engineChainV2 | array<EngineChainItem> | `[]`（运行时回退为 `[apple]` [待确认：init 默认链内容]） | 1–5 项 | `TransFrame.engineChainV2` |
| compareEnginesV2 | array<EngineChainItem> | `[]` | 2–4 项 | `TransFrame.compareEnginesV2` |
| engineConfigs | object<engineRawValue, EngineConfig> | 仅 `apple:{isEnabled:true}` | 见 2.5.1 | `TransFrame.engineConfigs` |
| compatibleProviderConfigs | array<CompatibleConfig> | `[]` | 0–5 项 | `TransFrame.compatibleProviderConfigs` |
| preferredTranslationEngine | string | `"apple"` | `apple`/`mtran` | `TransFrame.preferredTranslationEngine` |
| mtranServerURL | string | "" | URL（如 `http://localhost:8989`） | `TransFrame.mtranServerURL` |

#### 2.5.1 EngineChainItem

```json
{ "id": "<uuid>", "kind": "builtin" | ["compatible", "<uuid>"] }
```
（Swift 编码：`case builtin(TranslationEngineType)` 关联值 / `case compatible(UUID)`；Windows JSON 建议展平为 `{"kind":"builtin","engine":"openai"}` 或 `{"kind":"compatible","providerId":"<uuid>"}` [待确认：编码格式选型]）

#### 2.5.2 EngineConfig（per 引擎）

```json
{
  "id": "openai",
  "isEnabled": false,
  "customName": null,
  "options": {
    "baseURL": "https://api.openai.com/v1",
    "modelName": "gpt-4o-mini",
    "timeout": 30,
    "maxTokens": 2048,
    "temperature": 0.3,
    "customHeaders": null
  }
}
```

| 字段 | 类型 | 默认 | 范围 |
|---|---|---|---|
| id | string | — | `apple`/`mtran`/`openai`/`claude`/`gemini`/`ollama`/`google`/`deepl`/`baidu`/`custom` |
| isEnabled | bool | 仅 apple=true | — |
| options.timeout | number (秒) | 30（ollama 60） | >0 |
| options.maxTokens | int | 2048 | >0 |
| options.temperature | number | 0.3 | 0.0–2.0 |
| 引擎 API Key / Baidu appID+secretKey（敏感） | string | — | — | macOS Keychain |

#### 2.5.3 CompatibleConfig

```json
{ "id": "<uuid>", "displayName": "Custom", "baseURL": "http://localhost:8000/v1", "modelName": "gpt-4o-mini", "hasAPIKey": true }
```
上限 5 项；API Key 本体在 Keychain（`hasAPIKey` 仅为标志）。

### 2.6 Prompt

| 字段 | 类型 | 默认 | 范围/枚举 | macOS 键 |
|---|---|---|---|---|
| promptConfig.enginePrompts | object<engineRawValue, string> | {} | 键限 openai/claude/gemini/ollama | `TransFrame.promptConfig` |
| promptConfig.compatibleEnginePrompts | object<uuid字符串, string> | {} | — | 同上 |
| promptConfig.scenePrompts | object<sceneRawValue, string> | {} | `screenshot`/`text_selection`/`translate_and_insert` | 同上 |

模板变量：`{source_language}` `{target_language}` `{text}`。解析优先级：场景 > 引擎 > 内置默认。

### 2.7 快捷键（Windows: VK code + 修饰符）

| 字段 | 类型 | 默认（mac） | Windows 默认建议 | macOS 键 |
|---|---|---|---|---|
| fullScreenShortcut | {keyCode:int, modifiers:int} | ⌘⇧3 | Ctrl+Shift+3 | `TransFrame.fullScreenShortcut` |
| selectionShortcut | 同上 | ⌘⇧4 | Ctrl+Shift+4 | `TransFrame.selectionShortcut` |
| translationModeShortcut | 同上 | ⌘⇧T | Ctrl+Shift+T | `TransFrame.translationModeShortcut` |
| textSelectionTranslationShortcut | 同上 | ⌘⇧Y | Ctrl+Shift+Y | `TransFrame.textSelectionTranslationShortcut` |
| translateAndInsertShortcut | 同上 | ⌘⇧I | Ctrl+Shift+I | `TransFrame.translateAndInsertShortcut` |
| pickColorShortcut | 同上 | ⌃⌘C | Ctrl+Alt+C | `TransFrame.pickColorShortcut` |

约束：至少一个修饰键；六者互不冲突（冲突检测在保存层）。

### 2.8 高级（标注/截图样式）

| 字段 | 类型 | 默认 | 范围 | macOS 键 |
|---|---|---|---|---|
| strokeColor | string (颜色) | red（`#FF3B30`） | 预设 9 色 / 任意颜色 | `TransFrame.strokeColor`（JSON CodableColor） |
| strokeWidth | number | 2.0 | 1.0–20.0 | `TransFrame.strokeWidth` |
| textSize | number | 14.0 | 8.0–72.0 | `TransFrame.textSize` |
| shapeFillColor | string (颜色) | — | — | `TransFrame.shapeFillColor` |
| rectangleFilled | bool | false | — | `TransFrame.rectangleFilled` |
| ellipseFilled | bool | false | — | `TransFrame.ellipseFilled` |
| mosaicBlockSize | number | 10 [待确认] | — | `TransFrame.mosaicBlockSize` |

### 2.9 杂项（设置页未直接暴露）

| 字段 | 类型 | 默认 | 范围 | macOS 键 |
|---|---|---|---|---|
| defaultFormat | string | `"png"` | `png`/`jpeg`/`heic` | `TransFrame.defaultFormat` |
| jpegQuality | number | 0.9 | 0.1–1.0 | `TransFrame.jpegQuality` |
| heicQuality | number | 0.9 | 0.1–1.0 | `TransFrame.heicQuality` |
| ocrEngine | string | `"vision"` | OCREngineType 枚举 | `TransFrame.ocrEngine` |
| translationMode | string | `"below"` | TranslationMode 枚举 | `TransFrame.translationMode` |
| onboardingCompleted | bool | false | — | `TransFrame.onboardingCompleted` |
| userSkippedPermissions | bool | false | — | `TransFrame.userSkippedPermissions` |

## 3. 迁移源（只读，Windows 不需要实现）

`engineSelectionMode` / `sceneBindings` / `parallelEngines` / `TransFrame.vlmAPIKey`（明文）/ 无后缀 `vlmBaseURL` / `vlmModelName` / `mtranServerHost` / `mtranServerPort`。

## 4. 敏感字段处理（Windows）

| 字段 | macOS | Windows 建议 |
|---|---|---|
| VLM API Key（per provider） | Keychain | Credential Manager（`TransFrame.vlm.<provider>`）或 DPAPI 加密文件 |
| 翻译引擎 API Key / Baidu appID+secretKey | Keychain | 同上（`TransFrame.engine.<id>`） |
| 兼容实例 API Key | Keychain | 同上（`TransFrame.compatible.<uuid>`） |
| PaddleOCR 云端 API Key | Keychain | 同上（`TransFrame.paddleocr.cloud`） |
