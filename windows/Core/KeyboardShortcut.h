#pragma once
// KeyboardShortcut.h — global hotkey configuration (macOS KeyboardShortcut.swift).
// Persisted under SettingsKeys::*Shortcut as {"keyCode": UInt32, "modifiers": UInt32}.
//
// keyCode: on macOS these are Carbon virtual key codes (HIToolbox Events.h).
// Windows equivalents are documented in NativeShell (task B); the model keeps
// the raw 32-bit code verbatim so a settings file restored from macOS still
// round-trips even before a key-mapping table exists.
//
// Modifiers (Carbon bit flags, also the persisted format):
//   cmdKey=0x0100 shiftKey=0x0200 alphaLock=0x0400 optionKey=0x0800 controlKey=0x1000

#include "Json.h"

#include <cstdint>
#include <optional>
#include <string>

namespace tf::core {

/// Carbon modifier bits (kept in a namespace to avoid polluting tf::core).
namespace CarbonModifiers {
inline constexpr std::uint32_t cmdKey = 0x0100;
inline constexpr std::uint32_t shiftKey = 0x0200;
inline constexpr std::uint32_t alphaLock = 0x0400;
inline constexpr std::uint32_t optionKey = 0x0800;
inline constexpr std::uint32_t controlKey = 0x1000;
}  // namespace CarbonModifiers

struct KeyboardShortcut {
    std::uint32_t keyCode = 0;
    std::uint32_t modifiers = 0;

    KeyboardShortcut() = default;
    KeyboardShortcut(std::uint32_t key, std::uint32_t mods) noexcept : keyCode(key), modifiers(mods) {}

    // MARK: Default shortcuts (macOS defaults: Cmd+Shift+3/4/T/Y/I;
    // kVK_ANSI_3='3'=0x14, '4'=0x15, T=0x11, Y=0x10, I=0x22).
    static KeyboardShortcut fullScreenDefault() {
        return {0x14, CarbonModifiers::cmdKey | CarbonModifiers::shiftKey};
    }
    static KeyboardShortcut selectionDefault() {
        return {0x15, CarbonModifiers::cmdKey | CarbonModifiers::shiftKey};
    }
    static KeyboardShortcut translationModeDefault() {
        return {0x11, CarbonModifiers::cmdKey | CarbonModifiers::shiftKey};
    }
    static KeyboardShortcut textSelectionTranslationDefault() {
        return {0x10, CarbonModifiers::cmdKey | CarbonModifiers::shiftKey};
    }
    static KeyboardShortcut translateAndInsertDefault() {
        return {0x22, CarbonModifiers::cmdKey | CarbonModifiers::shiftKey};
    }

    /// At least one of cmd/control/option must be held (Swift hasRequiredModifiers).
    bool hasRequiredModifiers() const {
        const std::uint32_t required =
            CarbonModifiers::cmdKey | CarbonModifiers::controlKey | CarbonModifiers::optionKey;
        return (modifiers & required) != 0;
    }

    /// Shift-only shortcuts are not global-safe (intercepts typing) — same rule as Swift.
    bool isValid() const { return hasRequiredModifiers(); }

    friend bool operator==(const KeyboardShortcut&, const KeyboardShortcut&) = default;

    // MARK: JSON — {"keyCode": n, "modifiers": n}
    void toJson(Json& j) const {
        j = Json{{"keyCode", keyCode}, {"modifiers", modifiers}};
    }
    static KeyboardShortcut fromJson(const Json& j) {
        KeyboardShortcut s;
        s.keyCode = static_cast<std::uint32_t>(j.at("keyCode").get<std::uint64_t>());
        s.modifiers = static_cast<std::uint32_t>(j.at("modifiers").get<std::uint64_t>());
        return s;
    }
};

inline void to_json(Json& j, const KeyboardShortcut& s) { s.toJson(j); }
inline void from_json(const Json& j, KeyboardShortcut& s) { s = KeyboardShortcut::fromJson(j); }

}  // namespace tf::core
