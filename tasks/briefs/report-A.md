# 子任务 A 进度报告（未完成）

本次仅完成部分 Core 模型草稿及依赖文件，尚不能作为 Windows 工程骨架交付。请勿将此提交视为 US-W01 完成。

## 文件清单

- `windows/Core/Geometry.h`、`Uuid.h`、`Json.h`、`Json.cpp`：几何、UUID 和部分 JSON 辅助。
- `windows/Core/TranslationEngineType.h`、`TranslationScene.h`、`EngineSelectionV2.h`、`TranslationEngineConfig.h`、`SceneEngineBinding.h`、`TranslationPromptConfig.h`：部分翻译模型与 prompt 替换。
- `windows/Core/AppLanguage.h`、`OCREngineType.h`、`ExportFormat.h`、`Styles.h`：语言、OCR、导出和样式模型草稿。
- `windows/external/nlohmann/json.hpp`：nlohmann/json v3.11.3 单头文件；`windows/external/catch2/catch_amalgamated.hpp` 和 `.cpp`：Catch2 v3.7.1 amalgamation。尚需补充依赖许可证文件及来源文档。

## 接口清单

当前仅有部分值类型及其辅助函数，**尚未定义** `IOcrEngine`、`ITranslationProvider`、`IHotkeyListener` 等纯虚接口。

## Schema 对齐与风险

| 项目 | 当前状态 / 差异 |
| --- | --- |
| CGRect、UUID、关联值枚举、Date | 本机 Swift JSONEncoder 实测分别为 `[[x,y],[width,height]]`、大写 UUID、`{"builtin":{"_0":"openai"}}`、距 2001 年秒数；草稿实现尚未做跨语言往返测试。 |
| enum-keyed Dictionary | 实测为键值交替数组；`TranslationPromptConfig` 草稿按此实现，但尚未验证完整实例。 |
| Apple / Vision | TranslationEngineType 不含 apple；OCR 将 vision 映射为 windows。需要设计和测试跨平台设置迁移，当前未知引擎处理不一致。 |
| SceneEngineBinding 默认 fallback | `// TODO(schema-align)`：草稿使用 Custom，PRD 未指定；需与任务 C 协商，不能视为已裁决。 |
| Date JSON | `std::chrono::system_clock::time_point` 的 ADL 序列化尚未验证，可能需要专门包装类型或显式调用。 |
| Json / UUID / Geometry | 尚未完成语义和安全审查；UUID 随机数回退、Base64 无效输入处理、CGRect 交集边界均需复核。 |
| 设置、其他模型 | Settings 全部键、11 种 Annotation、Screenshot/CaptureDocument、BilingualSegment、TranslationResultBundle、四类历史等尚未实现。 |

## 验证与未运行项

- 本机 macOS：对已创建的 Core 头文件运行 `clang++ -std=c++20 -Wall -Wextra -Werror -Iwindows -Iwindows/external -x c++ -fsyntax-only -`（输入包含全部当前模型头文件和空 `main`），退出码 0。仅为语法检查，不证明链接或行为正确。
- Swift 临时探针运行成功，观察了上述 JSON 编码与颜色默认值；探针未纳入仓库。
- **NOT RUN**：MSVC x64/ARM64、Catch2、MSIX 打包；当前解决方案和测试工程尚不存在，暂时无法提供可运行的项目复现命令。
- **尚未实现**：`TransFrame.sln`、Core/NativeShell/App/Tests 工程、WinUI 窗口和 MSIX、VisualLineGrouping、LLM batching / response parser、测试及同源 fixture、Windows CI、README。
