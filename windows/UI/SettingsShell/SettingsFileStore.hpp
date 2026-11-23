// windows/UI/SettingsShell/SettingsFileStore.hpp
// FR-35 设置页持久化粘合层 —— 六页签共用的文件 IO + JSON 合并助手。
//
// 定位（对应任务第 4 点「持久化全部走 Core 既有 store」）：
//   * 文件只有两个：%APPDATA%\TransFrame\engines.json 与 settings.json，
//     与 App/Core 共用（EngineConfigStore / HotkeySettingsStore 的契约文件）。
//   * 字段归属 Core store 的页（语言/快捷键/高级/引擎的 vlm·paddle·链…）
//     一律经 Core 纯函数 parseEngineConfig / serializeEngineConfigPatch /
//     parseHotkeyBindings / serializeHotkeyBindings 读写 —— 未知键原样保留。
//   * Core store 尚未收录、但 schema 已定义或 PRD 要求 UI 先落盘的键
//     （appLanguage / engineConfigs / compatibleProviderConfigs / promptConfig /
//     pickColorShortcut / checkForUpdates.win）经本文件的 MergeEnginesKey /
//     MergeSettingsKey 直接写 JSON 顶层键 —— Core 的 parse 容忍未知键、
//     serialize 以既有文本为合并基，因此这些键在 Core 写盘路径下存活。
//     [报告项] 这些键待 Core store 正式收录（见最终交付差异清单）。
//
// 纯 C++（std + Win32 取目录），无 WinRT —— 可被任何页与测试直接调用。
// 线程模型：调用方（设置 STA）串行调用；本层不自加锁。
#ifndef TF_UI_SETTINGS_FILE_STORE_HPP
#define TF_UI_SETTINGS_FILE_STORE_HPP

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace tf::ui::store {

/// %APPDATA%\TransFrame（不存在则创建；失败返回空串）。
std::wstring RoamingDir();
/// %LOCALAPPDATA%\TransFrame（不创建）。
std::wstring LocalDir();

/// engines.json / settings.json 全文（UTF-8；缺文件/不可读 → 空串）。
std::string ReadEnginesJson();
std::string ReadSettingsJson();

/// 原子写（Core writeEngineConfigFileAtomic：tmp + rename）。
bool WriteEnginesJsonAtomic(const std::string& jsonUtf8);
bool WriteSettingsJsonAtomic(const std::string& jsonUtf8);

/// 读 engines.json 顶层键的 JSON 值（缺键/坏文件 → nullopt）。
std::optional<nlohmann::json> EnginesKey(const std::string& key);
/// 读 settings.json 顶层键的 JSON 值（同上）。
std::optional<nlohmann::json> SettingsKey(const std::string& key);

/// 把 `value` 合并进 enginesJsonUtf8 的顶层 `key`，保留其余键原样；
/// 输入非法 JSON 时以 {key: value} 重建。返回新全文。
std::string MergeEnginesKey(const std::string& enginesJsonUtf8, const std::string& key,
                            const nlohmann::json& value);
/// settings.json 版本，语义同上。
std::string MergeSettingsKey(const std::string& settingsJsonUtf8, const std::string& key,
                             const nlohmann::json& value);

}  // namespace tf::ui::store

#endif  // TF_UI_SETTINGS_FILE_STORE_HPP
