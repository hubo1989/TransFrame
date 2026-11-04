# TransFrame 设置界面 Windows 还原规格（macOS 1:1 对照）

> 依据：`TransFrame/Features/Settings/` 全部 14 个 Swift 文件 + 模型层
> （`AppSettings.swift` / `TranslationEngineConfig.swift` / `TranslationPromptConfig.swift` /
> `EngineSelectionV2.swift` / `VLMProviderType.swift` / `KeyboardShortcut.swift` /
> `CompatibleTranslationProvider.swift` / `TranslationEngine.swift` / `AppLanguage.swift`）。
> 用途：Windows 端 1:1 还原实现工单。不确定的行为一律标 `[待确认]`。

## 0. 窗口骨架

| 项 | 规格 |
|---|---|
| 窗口控制器 | `SettingsWindowController`（单例，`SettingsWindowController.shared`），重复打开只 `makeKeyAndOrderFront` |
| 初始尺寸 | 800 × 600 |
| 布局 | `NavigationSplitView`：左侧 sidebar 列表（宽固定 200），右侧 detail `ScrollView` + `VStack(spacing:20)`，水平 padding 24 / 顶部 24 / 底部 36 |
| 选中页签记忆 | 不持久化，每次打开默认 `.general`（`@State selectedTab = .general`）[待确认：Windows 是否需记住上次页签] |
| 卡片样式 | `.macosGroupedCard()` 分组卡片；Windows 对应分组 GroupBox / Card |

### 0.1 页签清单（SettingsTab）

| 页签 | rawValue | 图标 (SF Symbol) | 色调 |
|---|---|---|---|
| 通用 general | `general` | gearshape | blue |
| 引擎 engines | `engines` | engine.combustion | orange |
| Prompt prompts | `prompts` | text.bubble | pink |
| 语言 languages | `languages` | globe | cyan |
| 快捷键 shortcuts | `shortcuts` | keyboard | purple |
| 高级 advanced | `advanced` | pencil.tip.crop.circle | green |

注：advanced 的本地化键是 `settings.section.annotations`（历史命名）。

---

## 1. 通用页签（GeneralSettingsTab）

### 1.1 卡片 A：权限（settings.section.permissions）

三行 `PermissionItem` + 一个"重新检查"按钮。每行：图标 + 标题 + 提示文本 + 右侧状态（检查中 spinner / 已授权 ✓ / 未授权 ✗ + "授权"按钮）。

| 控件 | 类型 | 状态来源（ViewModel） | 触发动作 |
|---|---|---|---|
| 屏幕录制 | 状态行 + 授权按钮 | `hasScreenRecordingPermission` | `requestScreenRecordingPermission()` |
| 辅助功能 | 状态行 + 授权按钮 | `hasAccessibilityPermission` | `requestAccessibilityPermission()` |
| 文件夹访问（保存目录） | 状态行 + 授权按钮 | `hasFolderAccessPermission` | `requestFolderAccess()` |
| 重新检查 | borderless 按钮 | `isCheckingPermissions` | `checkPermissions()` |

- 授权按钮点击：先弹系统授权浮层（`PermissionFlowController.authorize`，锚定按钮 frame），再调 onGrant。
- 页签 `onAppear` 自动 `checkPermissions()`。
- **Windows 对应**：屏幕录制/辅助功能无直接等价；文件夹访问 → 文件夹选择器权限。[待确认：Windows 端该卡片保留哪些权限项]

### 1.2 卡片 B：保存位置 + 界面语言

| 控件 | 类型 | 绑定 | 持久化 |
|---|---|---|---|
| 保存路径显示 | 只读文本（`saveLocationPath`，中部截断） | — | — |
| "选择…" 按钮 | Button → 目录选择器 | `viewModel.selectSaveLocation()` | `TransFrame.saveLocation`（路径字符串）+ Security-scoped Bookmark（macOS `SaveLocationBookmark`；Windows 用普通路径即可） |
| "在 Finder 中显示" 按钮 | Button(图标 folder) | `revealSaveLocation()` | — |
| 界面语言 | Picker(menu) | `AppLanguage`：`system` / `en` / `zh-Hans` / `de` / `es` / `fr`（+其余 AppLanguage.allCases） | 经 `LanguageManager.currentLanguage`，非 AppSettings.Keys [待确认：具体存储键由 LanguageManager 决定] |

默认保存位置：桌面（`desktopDirectory`）。

---

## 2. 引擎页签（EngineSettingsTab）

三张卡片自上而下：凭证访问行、VLM 配置、多引擎翻译配置。

### 2.1 卡片 A：凭证访问（CredentialAccessRow）

| 控件 | 类型 | 绑定 | 说明 |
|---|---|---|---|
| 状态行 | 图标+标题+提示 | `credentialAuth.state`（`notNeeded`/`locked`/`authenticating`/`unlocked`/`failed`） | `notNeeded`→key.slash 图标；`unlocked`→✓；`locked/failed`→锁图标+"解锁"按钮 |
| 解锁按钮 | borderedProminent + touchid 图标 | `viewModel.unlockCredentialAccess()` | 仅 `locked/failed` 时显示；`authenticating` 时显示 spinner |

Windows 对应：Windows Credential Manager / DPAPI。[待确认：Windows 端凭证门闸形态]

### 2.2 卡片 B：VLM 配置（VLMConfigurationSection）

| 控件 | 类型 | 绑定 | 取值域 | 默认 | 持久化键 |
|---|---|---|---|---|---|
| Provider 选择 | Picker(segmented) | `viewModel.vlmProvider` → `settings.vlmProvider` | `openai` / `claude` / `gemini` / `ollama` / `paddleocr` | `openai` | `TransFrame.vlmProvider` |
| OpenAI API 模式（仅 provider==openai） | Picker(segmented) | `vlmEditingOpenAIMode` → `settings.vlmOpenAIMode` | `chat` / `response` | `chat` | `TransFrame.vlmOpenAIMode` |
| API Key | TextField/SecureField + 显隐切换 | `vlmEditingAPIKey`（编辑态）→ 保存到 Keychain | 任意字符串 | 空 | Keychain（per-provider）；旧明文键 `TransFrame.vlmAPIKey` 已迁移删除 |
| Base URL | TextField | `vlmEditingBaseURL` → `setVLMBaseURL(_:for:)` | URL 字符串 | 按 provider：openai=`https://api.openai.com/v1`，claude=`https://api.anthropic.com/v1`，gemini=`https://generativelanguage.googleapis.com/v1beta`，ollama=`http://localhost:11434` | `TransFrame.vlmBaseURL.<provider>` |
| 模型名 | TextField + 拉取模型下拉 + 获取按钮 | `vlmEditingModelName` → `setVLMModelName(_:for:)` | 字符串；拉取 `/models` 填充 Menu | openai=`gpt-4o-mini`，claude=`claude-sonnet-4-20250514`，gemini=`gemini-2.0-flash`，ollama=`llama3` | `TransFrame.vlmModelName.<provider>`（旧 `TransFrame.vlmModelName` 兼容读取） |
| 测试按钮 + 结果 | Button + 结果图标文本 | `vlmTestResult` / `vlmTestSuccess` | — | — | — |

联动/显隐：
- provider 切换 → `loadVLMEditingState(for:)` 重载编辑态，清空测试结果。
- provider==`paddleocr` → 整个 API Key/BaseURL/模型区替换为 `PaddleOCRStatusSection`。
- provider==`openai` → 额外显示 API 模式行。

#### PaddleOCRStatusSection（provider==paddleocr 时）

| 控件 | 类型 | 绑定 | 取值域 | 默认 | 持久化键 |
|---|---|---|---|---|---|
| 服务状态 | 状态行（`paddleOCRVersion`） | 只读 | 已安装(版本号)/未安装 | — | — |
| OCR 模式 | Picker | `paddleOCRMode` | `fast` / `precise`（fast≈1s 行分组；precise≈12s VL-1.5） | `fast` | `TransFrame.paddleOCRMode` |
| 使用云端 | Toggle | `paddleOCRUseCloud` | Bool | false [待确认] | `TransFrame.paddleOCRUseCloud` |
| 云端 Base URL | TextField（useCloud 时显示） | `paddleOCRCloudBaseURL` | URL | — | `TransFrame.paddleOCRCloudBaseURL` |
| 云端 API Key | SecureField（useCloud 时显示） | `paddleOCRCloudAPIKey` | 字符串 | 空 | Keychain [待确认：UserDefaults 键 `TransFrame.paddleOCRCloudAPIKey` 已定义但是否落 UserDefaults] |
| 云端模型 ID | TextField + 模型下拉（useCloud 时显示） | `paddleOCRCloudModelId` | 字符串 | — | `TransFrame.paddleOCRCloudModelId` |

### 2.3 卡片 C：多引擎翻译配置（MultiEngineSettingsSection）

#### C-1 翻译策略卡

| 控件 | 类型 | 绑定 | 取值域 | 默认 | 持久化 |
|---|---|---|---|---|---|
| 策略选择 | Picker(segmented) | `settings.engineModeV2` | `ordered`（顺序回退链）/ `compare`（并行对比） | `ordered` | `TransFrame.engineModeV2` |

联动：`ordered` → 显示 `orderedChainSectionV2`；`compare` → 显示 `compareEnginesSectionV2`。

#### C-2 顺序回退链（ordered）

| 控件 | 类型 | 说明 |
|---|---|---|
| 链列表 | 有序列表（1...5 项，计数显示 `n/5`），支持拖拽排序 | `settings.engineChainV2: [EngineChainItem]`；每项 `kind = builtin(TranslationEngineType)` 或 `compatible(UUID)`，自有稳定 `id: UUID`（同一引擎可重复入链） |
| 行内替换 Menu | Menu | 将该行替换为任一已启用内置引擎或兼容实例 |
| 行内删除 | 按钮 | 移除该行 |
| "+" 添加 Menu | Menu | 列出所有 `isEnabled` 的内置引擎（排除 `.custom`）+ 全部兼容实例 |

持久化：`TransFrame.engineChainV2`（JSON 数组）。修改后须显式 `saveEngineChainV2()`（值类型 didSet 不可靠——Windows 实现同样要显式保存）。

#### C-3 并行对比（compare）

| 控件 | 类型 | 说明 |
|---|---|---|
| 引擎集合编辑 | 列表 + 添加/移除 | `settings.compareEnginesV2`，2...4 项（`compareEnginesMinimum=2`，`compareEnginesLimit=4`）；候选项同上 |
| 计数 | 文本 | `n/4` |

持久化：`TransFrame.compareEnginesV2`（JSON 数组，同样显式保存）。

#### C-4 可用引擎卡（enginesSection）

按类别分组显示引擎卡片：

| 类别 | 引擎（`uiCards`） |
|---|---|
| 内置 builtIn | `apple`（Apple Translation，本地）、`mtran`（MTranServer） |
| LLM | `openai`、`claude`、`gemini`（`.ollama` 卡片已收敛，经 openai 卡的 Ollama 预设配置） |
| 云服务 cloudService | `google`、`deepl`、`baidu` |
| 兼容 compatible | 动态 `compatibleProviderConfigs`（0...5 个） |

每张内置引擎卡片：图标 + 名称 + 描述 + 健康状态（real health probe，缓存 `healthCache` [待确认：状态枚举细节]）+ 点击打开 `EngineConfigSheet`。
每张兼容引擎卡片：gearshape.2 图标 + displayName + 是否被链/对比集选中状态 + 点击打开 `CompatibleEngineConfigSheet`。
兼容实例添加按钮：数量 <5 时显示；==5 时显示"已达上限"文案。

默认状态：仅 `apple` 引擎 `isEnabled=true`（`TranslationEngineConfig.default(for:)`）。

### 2.4 Sheet 1：引擎配置（EngineConfigSheet，500×450）

字段随引擎变化：

| 控件 | 类型 | 绑定/来源 | 显示条件 |
|---|---|---|---|
| OpenAI 预设 Picker | Picker | `openAIPreset`：`openAI`（官方）/ `ollamaLocal`（本地 Ollama）/ `custom` | 仅 engine==`.openai` |
| 启用开关 | Toggle(switch) | `config.isEnabled` | 恒显 |
| API Key | SecureField + 显隐 + 获取密钥链接 | Keychain | `engine.requiresAPIKey` 且非（openai+ollamaLocal 预设）且非（openai+custom+本地 URL） |
| Baidu AppID + Secret Key | 两个 SecureField | Keychain | 仅 `.baidu` |
| MTranServer URL | TextField（占位 `http://localhost:8989`） | `options.baseURL` → `TransFrame.mtranServerURL` | 仅 `.mtran` |
| Base URL | TextField | `options.baseURL` | 引擎有 defaultBaseURL 时 |
| 模型名 | TextField + 模型下拉 + 获取按钮 | `options.modelName` | `defaultModelName != nil` 或 `.custom` |
| 测试连接 | Button + 结果 | `isTesting`/`testResult`/`testSuccess` | 恒显（`canTest` 时可用） |
| 取消 / 保存 | Button | 保存→`saveConfig()`；取消→回滚临时凭证 | 保存按钮在 `!isEnabled && !hasValidConfig` 时禁用 |

预设联动（openai）：
- `ollamaLocal`：隐藏 API Key，baseURL 预填 `http://localhost:11434`，配置经 `ollamaProxyConfig` 代理保存（`onOllamaProxySave` 回调），不动 `.openai` 绑定。
- `custom` + 本地 URL（`isLocalEndpoint`）：隐藏 API Key。
- 模型获取：调 `<baseURL>/models`。

EngineOptions 数值默认（`EngineOptions.default`）：timeout 30s（ollama 60s）、maxTokens 2048、temperature 0.3；temperature 有效域 0.0–2.0。[待确认：Sheet UI 是否暴露 timeout/maxTokens/temperature 输入——当前 body 未见对应控件]

### 2.5 Sheet 2：兼容引擎配置（CompatibleEngineConfigSheet）

| 控件 | 类型 | 绑定 | 说明 |
|---|---|---|---|
| 显示名称 | TextField | `displayName` | 新建时标题为"新建兼容引擎" |
| Base URL | TextField（占位 `http://localhost:8000/v1`） | `baseURL` | OpenAI 兼容端点 |
| 模型名 | TextField + 模型下拉 + 获取按钮 | `modelName`（占位 `gpt-4o-mini`） | 下拉来自 `/models` 拉取 |
| API Key | SecureField + 显隐 | `apiKey`（保存进 Keychain，`hasAPIKey` 标志进配置） | |
| 测试连接 | Button + 结果 | 同上 | |
| 取消 / 保存 | Button | onSave → `compatibleProviderConfigs`（上限 5） | |

持久化：`TransFrame.compatibleProviderConfigs`（JSON 数组：`{id: UUID, displayName, baseURL, modelName, hasAPIKey}`）；API Key 本体在 Keychain（按实例 id）。

---

## 3. Prompt 页签（PromptSettingsView）

### 3.1 引擎 Prompt（enginePromptsSection）

支持自定义 Prompt 的内置 LLM 引擎固定为：`openai`、`claude`、`gemini`、`ollama`。每行：图标 + 引擎名 + （已设置自定义 Prompt 时铅笔标记）+ "编辑"按钮。

### 3.2 兼容引擎 Prompt

仅当 `compatibleProviderConfigs` 非空时显示（Divider + 小节标题）。每行同上，按 displayName 列出，编辑目标为 `.compatible(providerUUID字符串)`。

### 3.3 场景 Prompt（scenePromptsSection）

`TranslationScene.allCases`：`screenshot`（截图翻译）、`text_selection`（划词翻译）、`translate_and_insert`（翻译插入）。每行：图标 + 场景名 + 已设置标记 + "编辑"按钮。

### 3.4 默认 Prompt 预览（defaultPromptSection）

只读展示 `TranslationPromptConfig.defaultPrompt`（通用）与 `defaultInsertPrompt`（翻译插入场景）。

### 3.5 PromptEditorSheet

| 控件 | 类型 | 说明 |
|---|---|---|
| Prompt 编辑器 | TextEditor | `editingPrompt` |
| 模板变量按钮 | 按钮列表 | `{source_language}` / `{target_language}` / `{text}`（插入光标处）[待确认：完整变量清单以 templateVariables 为准] |
| 取消 / 保存 | Button | 保存→`promptConfig.enginePrompts[engine]` / `compatibleEnginePrompts[uuid字符串]` / `scenePrompts[scene]` |

持久化：`TransFrame.promptConfig`（JSON：`{enginePrompts: {engineRawValue: String}, compatibleEnginePrompts: {uuid: String}, scenePrompts: {sceneRawValue: String}}`）。

解析优先级（`resolvePrompt`）：场景级 > 引擎级 > 默认。[待确认：兼容引擎 prompt 与场景 prompt 的叠加次序细节]

---

## 4. 语言页签（LanguageSettingsTab）

一张卡片，两组语言对：

| 控件 | 类型 | 绑定 | 取值域 | 默认 | 持久化键 |
|---|---|---|---|---|---|
| 全局翻译-源语言 | Picker(menu) | `translationSourceLanguage` | `auto` + 全部语言（`TranslationLanguage.allCases`） | `auto` | `TransFrame.translationSourceLanguage` |
| 全局翻译-目标语言 | Picker(menu) | `translationTargetLanguage`（`TranslationLanguage?`） | "跟随系统"(nil) + 全部非 auto 语言 | nil（跟随系统） | `TransFrame.translationTargetLanguage`（nil 时移除键） |
| 翻译插入-源语言 | Picker(menu) | `translateAndInsertSourceLanguage` | 同上 | `auto` | `TransFrame.translateAndInsertSourceLanguage` |
| 翻译插入-目标语言 | Picker(menu) | `translateAndInsertTargetLanguage`（Optional） | 跟随系统 + 非 auto | nil | `TransFrame.translateAndInsertTargetLanguage`（nil 时移除键） |

语言全集（rawValue）：`auto, en, zh-Hans, zh-Hant, ja, ko, fr, de, es, it, pt, ru, ar, hi, th, vi, nl, pl, tr, uk, cs, sv, da, fi, no, el, he, id, ms, ro`。

关联键：`TransFrame.translationAutoDetect`（Bool，默认 true）[待确认：当前设置 UI 是否还暴露该开关]。

---

## 5. 快捷键页签（ShortcutSettingsTab）

一张卡片，6 个 `ShortcutRecorder` 行（当前组合按钮 + 重置按钮；录制中高亮"录制中"提示）：

| 快捷键 | 绑定（ViewModel 转发） | 默认 | 持久化键 |
|---|---|---|---|
| 全屏截图 | `fullScreenShortcut` | ⌘⇧3 | `TransFrame.fullScreenShortcut` |
| 区域截图 | `selectionShortcut` | ⌘⇧4 | `TransFrame.selectionShortcut` |
| 翻译模式 | `translationModeShortcut` | ⌘⇧T | `TransFrame.translationModeShortcut` |
| 划词翻译 | `textSelectionTranslationShortcut` | ⌘⇧Y | `TransFrame.textSelectionTranslationShortcut` |
| 翻译插入 | `translateAndInsertShortcut` | ⌘⇧I | `TransFrame.translateAndInsertShortcut` |
| 取色 | `pickColorShortcut` | ⌃⌘C | `TransFrame.pickColorShortcut` |

- 录制状态机：`recordingType: ShortcutRecordingType?`（六值枚举，同一时刻仅一个在录）。
- 存储格式：`{keyCode: UInt32, modifiers: UInt32}` 字典（Carbon 键码 + 修饰位掩码 cmd/shift/option/control）。Windows 对应 VK code + Win 修饰符。
- 保存后 post `shortcutDidChangeNotification`，AppDelegate `updateHotkeys()` 重新注册。
- 校验：至少一个修饰键（`hasRequiredModifiers`）；冲突检测在 ViewModel。[待确认：冲突 UI 呈现形式]
- 重置：恢复对应 `KeyboardShortcut.xxxDefault`。

## 6. 高级页签（AdvancedSettingsTab）

一张卡片 + 重置按钮：

| 控件 | 类型 | 绑定 | 取值域 | 默认 | 持久化键 |
|---|---|---|---|---|---|
| 描边颜色 | 9 个预设色圆形按钮（选中带环）[待确认：是否另有自定义取色器] | `strokeColor`（Color） | 预设：red/orange/yellow/green/blue/purple/pink/white/black | `.red` | `TransFrame.strokeColor`（JSON 编码 CodableColor） |
| 描边宽度 | Slider | `strokeWidth` | 1.0 – 20.0 | 2.0 | `TransFrame.strokeWidth` |
| 文本大小 | Slider | `textSize` | 8.0 – 72.0 | 14.0 | `TransFrame.textSize` |
| 全部重置 | destructive 按钮 + 确认 Alert | — | `viewModel.resetAllToDefaults()` → `settings.resetToDefaults()` + `appDelegate.updateHotkeys()` | — | 清空全部上述键 |

未在设置页暴露但存在的相关键（供 schema 参考）：`rectangleFilled`(false)、`ellipseFilled`(false)、`shapeFillColor`、`mosaicBlockSize`、`jpegQuality`(0.1–1.0, 默认 0.9)、`heicQuality`(0.1–1.0, 默认 0.9)、`defaultFormat`(png/jpeg/heic, 默认 png)、`ocrEngine`(默认 vision)、`translationMode`(默认 below)、`onboardingCompleted`(false)、`userSkippedPermissions`(false)、`paddleOCRServerAddress`、`mtranServerHost`/`mtranServerPort`（旧）。

## 7. 持久化机制汇总（Windows 迁移要点）

1. macOS 全部走 `UserDefaults`，键前缀 `TransFrame.`；API Key 类（VLM、各引擎、兼容实例、PaddleOCR 云端）在 Keychain，UserDefaults 只存非敏感字段。
2. 结构化值（engineConfigs / promptConfig / compatibleProviderConfigs / engineChainV2 / compareEnginesV2 / 快捷键 / 颜色）为 JSON/Data 编码。
3. `engineConfigs` / `engineChainV2` / `compareEnginesV2` 修改后必须显式调用保存函数（@Observable didSet 不可靠）——Windows 端同样避免"改内存即落盘"的隐式假设。
4. 旧键（engineSelectionMode / sceneBindings / parallelEngines / TransFrame.vlmAPIKey / 无后缀 vlmBaseURL / vlmModelName）仅作迁移源读取，不再写入。
