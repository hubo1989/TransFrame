#pragma once
// tf/HotkeySettingsStore.hpp — %APPDATA%\TransFrame\settings.json 快捷键段读写
// （FR-33：改键持久化；文件其余段落保留给后续设置项）。
//
// 形状（Win32 VK + MOD_* 位，本机格式；macOS Carbon 格式的映射留在 App 层）：
//   {
//     "TransFrame.fullScreenShortcut":        {"vk": 70, "modifiers": 3},
//     "TransFrame.selectionShortcut":         {"vk": 65, "modifiers": 3},
//     "TransFrame.translationModeShortcut":   {"vk": 84, "modifiers": 3},
//     "TransFrame.textSelectionTranslationShortcut": {"vk": 68, "modifiers": 3},
//     "TransFrame.translateAndInsertShortcut":       {"vk": 73, "modifiers": 3}
//   }
// 键名 = SettingsKeys.h 的 *Shortcut 常量（TransFrame. 前缀，与 macOS schema 对齐）。
//
// 纯函数（string in / string out），IO 在调用方——与 EngineConfigStore 同模式。
#ifndef TF_HOTKEY_SETTINGS_STORE_HPP
#define TF_HOTKEY_SETTINGS_STORE_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

struct HotkeyBinding {
    std::string actionName;       // "fullScreen"|"region"|"translate"|"textSelection"|"translateInsert"
    std::uint32_t vk = 0;         // Win32 VK_*
    std::uint32_t modifiers = 0;  // Win32 MOD_* 位（1=Alt 2=Ctrl 4=Shift 8=Win）
    bool enabled = true;
};

/// 设置文件里的五个键名 ↔ 动作名映射（kHotkeyActionNames 顺序 = HotkeyAction 枚举序）。
extern const char* const kHotkeyActionNames[5];

/// 动作名 -> "TransFrame.*Shortcut" 设置键名。
std::string hotkeySettingsKeyFor(const std::string& actionName);
/// 设置键名 -> 动作名；未知键名返回 nullopt。
std::optional<std::string> hotkeyActionNameFromSettingsKey(const std::string& key);

/// 纯：从 settings.json 文本提取快捷键段。字段缺失/类型错/未知键名 → 跳过该条
/// （返回的 vector 不含它）；非法 JSON → 返回空 vector。
std::vector<HotkeyBinding> parseHotkeyBindings(const std::string& jsonUtf8);

/// 纯：把快捷键绑定合并进 settings.json 文本，保留所有未知键；
/// 五个动作全部写出（enabled=false 也写，表示"已禁用"）。
std::string serializeHotkeyBindings(const std::string& existingJsonUtf8,
                                    const std::vector<HotkeyBinding>& bindings);

}  // namespace tf::core

#endif  // TF_HOTKEY_SETTINGS_STORE_HPP
