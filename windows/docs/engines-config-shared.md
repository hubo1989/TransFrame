# engines.json 共享契约（wt-settings ↔ wt-engines）

- 文件：`%APPDATA%\TransFrame\engines.json`（Roaming，UTF-8 无 BOM）。
- 两个分支共同读写同一文件；本文档是唯一权威约定。
- 归属：wt-settings 设置窗口通用页写入 `mtran.baseUrl` / `targetLang`；
  引擎管理段落（ollama / compatible 等）由 wt-engines 定义，写方保留未知键。

## 1. 文件结构（v1）

```json
{
  "mtran": {
    "baseUrl": "http://localhost:8989"
  },
  "targetLang": "zh"
}
```

## 2. 字段语义

| 字段 | 类型 | 默认值 | 语义 |
|---|---|---|---|
| 文件不存在 | — | — | 全部字段走默认值，不自动创建文件 |
| `mtran.baseUrl` | string | `http://localhost:8989` | MTranServer 端点（http://host[:port]）；使用前必须过 `tf::MTranServerProvider::Configuration::parse`，不过按默认值用，不得影响启动 |
| `targetLang` | string | `zh` | 目标语言短标签（BCP-47 风格）；读取方 trim + ASCII 小写化 |
| 其它键 | any | 保留 | 引擎管理段落由 wt-engines 定义；写方必须原样保留 |

## 3. 读写规则（两分支共同遵守）

1. **缺文件 = 默认值**：文件不存在/空/非对象 JSON → 全部按默认值运行，不得因配置损坏无法启动。
2. **保存即合并**：写方以旧文件为合并基底，只改 `mtran.baseUrl` / `targetLang`，
   其余键原样保留（`tf::core::serializeEngineConfigPatch`）；禁止整文件覆盖只写已知键。
3. **原子写**：写 `<file>.tmp` 后 rename 替换（`MoveFileExW(MOVEFILE_REPLACE_EXISTING)`）。
4. **环境变量优先**：`TRANSFRAME_MTRAN_URL` / `TRANSFRAME_TARGET_LANG` 高于文件值
   （AppMain 现有行为保持）；否则文件 > 内置默认。
5. **无自动创建**：读取方不预创建文件；首次保存才创建。

## 4. 测试向量（两分支共用，见 EngineConfigStoreTests）

| 输入（engines.json 内容） | parseEngineConfig 结果 |
|---|---|
| （文件不存在/空） | defaults，ok=true，fileExisted=false |
| `{}` | defaults，ok=true |
| `{"mtran":{"baseUrl":"http://10.0.0.2:9000"},"targetLang":"en"}` | baseUrl=http://10.0.0.2:9000，targetLang=en |
| `{"mtran":{"baseUrl":"  http://X:1  "},"targetLang":" EN "}` | baseUrl trim 去空白；targetLang→"en" |
| `{"ollama":{"baseUrl":"…"}}` | mtran/targetLang 走默认，ollama 段保留 |
| 非对象 JSON（`"x"` / `42` / 数组） | ok=false（error 说明），配置走默认值 |
| `{"mtran":{"baseUrl":42},"targetLang":[]}` | 类型错字段按缺失处理，其余正常，ok=true |
| `{"mtran":{"baseUrl":""}}` | baseUrl/targetLang 均走默认 |

## 5. 共享代码

- 纯函数在 Core：`tf::core::parseEngineConfig` / `serializeEngineConfigPatch`
  （`windows/Core/include/tf/EngineConfigStore.hpp`）。wt-engines 直接复用 Core 静态库，
  不要再写第二份解析器。
- 原子写与 %APPDATA% 目录解析在 App 层（AppMain.cpp）。
- 测试：`windows/Tests/EngineConfigStoreTests.cpp`。
