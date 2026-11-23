// HotkeySettingsStore.cpp — settings.json 快捷键段 merge/parse（纯函数）。
#include "tf/HotkeySettingsStore.hpp"

#include <nlohmann/json.hpp>

namespace tf::core {

const char* const kHotkeyActionNames[5] = {
    "fullScreen", "region", "translate", "textSelection", "translateInsert",
};

std::string hotkeySettingsKeyFor(const std::string& actionName) {
    // 与 SettingsKeys.h 的 *Shortcut 常量逐字一致。
    if (actionName == "fullScreen") return "TransFrame.fullScreenShortcut";
    if (actionName == "region") return "TransFrame.selectionShortcut";
    if (actionName == "translate") return "TransFrame.translationModeShortcut";
    if (actionName == "textSelection") return "TransFrame.textSelectionTranslationShortcut";
    if (actionName == "translateInsert") return "TransFrame.translateAndInsertShortcut";
    return {};
}

std::optional<std::string> hotkeyActionNameFromSettingsKey(const std::string& key) {
    for (const char* n : kHotkeyActionNames) {
        if (key == hotkeySettingsKeyFor(n)) return std::string(n);
    }
    return std::nullopt;
}

namespace {

// nlohmann: negative/float/bool all rejected; only >=0 integers accepted.
bool tryGetU32(const nlohmann::json& node, const char* field, std::uint32_t* out) {
    if (!node.is_object() || !node.contains(field)) return false;
    const nlohmann::json& v = node.at(field);
    if (v.is_number_unsigned()) {
        *out = v.get<std::uint32_t>();
        return true;
    }
    if (v.is_number_integer()) {
        const auto n = v.get<std::int64_t>();
        if (n >= 0 && n <= 0xFFFFFFFFll) {
            *out = static_cast<std::uint32_t>(n);
            return true;
        }
    }
    return false;
}

}  // namespace

std::vector<HotkeyBinding> parseHotkeyBindings(const std::string& jsonUtf8) {
    std::vector<HotkeyBinding> out;
    const nlohmann::json root = nlohmann::json::parse(jsonUtf8, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return out;
    for (const char* name : kHotkeyActionNames) {
        const std::string key = hotkeySettingsKeyFor(name);
        if (!root.contains(key)) continue;
        const nlohmann::json& entry = root.at(key);
        std::uint32_t vk = 0;
        std::uint32_t mods = 0;
        if (!tryGetU32(entry, "vk", &vk)) continue;
        if (!tryGetU32(entry, "modifiers", &mods)) continue;
        HotkeyBinding b;
        b.actionName = name;
        b.vk = vk;
        b.modifiers = mods;
        b.enabled = entry.value("enabled", true);
        out.push_back(std::move(b));
    }
    return out;
}

std::string serializeHotkeyBindings(const std::string& existingJsonUtf8,
                                    const std::vector<HotkeyBinding>& bindings) {
    nlohmann::json root = nlohmann::json::object();
    if (!existingJsonUtf8.empty()) {
        const nlohmann::json parsed = nlohmann::json::parse(existingJsonUtf8,
                                                            nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) root = parsed;
    }
    for (const HotkeyBinding& b : bindings) {
        nlohmann::json entry = nlohmann::json::object();
        entry["vk"] = b.vk;
        entry["modifiers"] = b.modifiers;
        entry["enabled"] = b.enabled;
        root[hotkeySettingsKeyFor(b.actionName)] = entry;
    }
    return root.dump(2) + "\n";
}

}  // namespace tf::core
