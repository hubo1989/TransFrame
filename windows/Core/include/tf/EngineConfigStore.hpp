#pragma once
// tf/EngineConfigStore.hpp — engines.json read/write (FR-35, shared contract).
//
// File: %APPDATA%\TransFrame\engines.json (Roaming; share with wt-engines —
// full contract in windows/docs/engines-config-shared.md).
//
// Field parity: every persisted setting on the mac app (TransFrame/Models/
// AppSettings.swift — UserDefaults keys below noted per field as `mac:`)
// has a counterpart here. JSON keys are snake_case; C++ members keep the
// repo's camelCase style (`mac:` key names are camelCase — the mapping is
// noted per field).
//
// Not persisted here by design:
//   - vlmAPIKey / paddleOCRCloudAPIKey — mac stores these in Keychain
//     (KeychainService), never in UserDefaults; the Windows side keeps them
//     in CredMan (NativeShell), never in engines.json.
//   - SaveLocationBookmark (mac-only security-scoped bookmark); saveLocation
//     plain path is carried instead.
//   - engineConfigs / promptConfig / sceneBindings / compatibleProviderConfigs
//     and legacy engineSelectionMode are owned by the engine-management
//     branch (wt-engines); they survive merges as unknown keys.
//
// This module is pure except writeEngineConfigFileAtomic (std::filesystem
// only, no Win32); file IO otherwise stays with the caller so Core stays
// platform-clean and the JSON merge semantics are unit-tested without a disk.
#ifndef TF_ENGINE_CONFIG_STORE_HPP
#define TF_ENGINE_CONFIG_STORE_HPP

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace tf::core {

// mac: CodableColor (Styles.swift) — {red,green,blue,opacity} components.
struct ColorConfig {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double opacity = 1.0;

    static ColorConfig makeRed() { return {1.0, 0.0, 0.0, 1.0}; }     // mac: .red
    static ColorConfig makeYellow() { return {1.0, 1.0, 0.0, 1.0}; }  // mac: .yellow
    bool operator==(const ColorConfig&) const = default;
};

// mac: KeyboardShortcut — keyCode + Carbon modifier mask (cmd=0x0100,
// shift=0x0200, ctrl=0x1000, alt=0x0800; kVK_ANSI_* key codes). These mirror
// the mac UserDefaults values for schema parity; the Windows runtime
// registers its own VK_* hotkeys via HotkeySettingsStore.
struct ShortcutConfig {
    std::uint32_t keyCode = 0;
    std::uint32_t modifiers = 0;
    bool operator==(const ShortcutConfig&) const = default;
};

// mac: EngineChainItem.Kind — builtin(TranslationEngineType) or compatible(UUID).
struct EngineChainEntry {
    bool isCompatible = false;   // false -> builtinEngine, true -> compatibleId
    std::string builtinEngine;   // mac rawValue: "apple"|"mtran"|"openai"|...
    std::string compatibleId;    // stable UUID of a compatible-engine instance
    bool operator==(const EngineChainEntry&) const = default;
};

// mac: VLM Configuration (vlmProvider / vlmBaseURL / vlmModelName / vlmOpenAIMode).
struct VlmConfig {
    std::string provider = "openai";  // openai|claude|gemini|ollama|paddleocr
    std::string baseURL = "https://api.openai.com/v1";
    std::string modelName = "gpt-4o";
    std::string openAIMode = "chat";  // chat|responses
    bool operator==(const VlmConfig&) const = default;
};

// mac: PaddleOCR Configuration (paddleOCRMode / ... / paddleOCRLocalVLModelDir).
struct PaddleOcrConfig {
    std::string mode = "fast";  // fast|precise
    bool useCloud = false;
    std::string cloudBaseURL;
    std::string cloudModelId;
    std::string localVlModelDir;
    bool operator==(const PaddleOcrConfig&) const = default;
};

// mac: translateAndInsertSourceLanguage / translateAndInsertTargetLanguage.
// Empty targetLang = "follow system" (mac stores nil by removing the key).
struct TranslateAndInsertConfig {
    std::string sourceLang = "auto";
    std::string targetLang;
    bool operator==(const TranslateAndInsertConfig&) const = default;
};

struct EngineConfig {
    // --- v1 fields (engines-config-shared.md) ---
    std::string mtranBaseUrl;  // mac: mtranServerURL — "http://host[:port]"
    std::string targetLang;    // lowercase short tag, e.g. "zh"

    // --- General (mac: defaultFormat / jpegQuality / heicQuality / saveLocation) ---
    std::string defaultFormat = "png";  // png|jpeg|heic
    double jpegQuality = 0.9;
    double heicQuality = 0.9;
    std::string saveLocation;  // empty = platform default (Desktop on mac)

    // --- Translation languages (mac: translationSourceLanguage /
    //     translationAutoDetect; translationTargetLanguage == targetLang) ---
    std::string sourceLang = "auto";
    bool autoDetect = true;

    // --- Engine selection (mac: ocrEngine / translationEngine / translationMode /
    //     preferredTranslationEngine / translationFallbackEnabled) ---
    std::string ocrEngine = "vision";
    std::string translationEngine = "apple";
    std::string translationMode = "below";  // below|inline
    std::string preferredTranslationEngine = "apple";  // apple|mtran
    bool translationFallbackEnabled = true;

    // --- Engine Selection V2 (mac: engineModeV2 / engineChainV2 / compareEnginesV2) ---
    std::string engineModeV2 = "ordered";  // ordered|compare
    std::vector<EngineChainEntry> engineChainV2;     // default: [builtin apple]
    std::vector<EngineChainEntry> compareEnginesV2;  // default: [apple, mtran]

    // --- Annotation defaults (mac: strokeColor / strokeWidth / textSize /
    //     rectangleFilled / ellipseFilled / shapeFillColor / mosaicBlockSize) ---
    ColorConfig strokeColor = ColorConfig::makeRed();
    double strokeWidth = 2.0;
    double textSize = 14.0;
    bool rectangleFilled = false;
    bool ellipseFilled = false;
    ColorConfig shapeFillColor = ColorConfig::makeYellow();
    double mosaicBlockSize = 10.0;

    // --- Shortcuts (mac: *Shortcut keys; values mirror mac defaults) ---
    ShortcutConfig fullScreenShortcut{0x14, 0x0300};            // cmd+shift+3
    ShortcutConfig selectionShortcut{0x15, 0x0300};             // cmd+shift+4
    ShortcutConfig translationModeShortcut{0x11, 0x0300};       // cmd+shift+T
    ShortcutConfig textSelectionTranslationShortcut{0x16, 0x0300};  // cmd+shift+Y
    ShortcutConfig translateAndInsertShortcut{0x22, 0x0300};    // cmd+shift+I
    ShortcutConfig pickColorShortcut{0x08, 0x1100};             // cmd+ctrl+C

    // --- Onboarding (mac: onboardingCompleted / userSkippedPermissions) ---
    bool onboardingCompleted = false;
    bool userSkippedPermissions = false;

    // --- VLM / PaddleOCR / translate-and-insert (structs above) ---
    VlmConfig vlm;
    PaddleOcrConfig paddleOcr;
    TranslateAndInsertConfig translateAndInsert;

    static EngineConfig makeDefault();

    bool operator==(const EngineConfig&) const = default;
};

struct EngineConfigParseResult {
    EngineConfig config;    // fields missing from the file fall back to defaults
    bool ok = true;         // false only when the file exists but is not valid JSON
    std::string error;      // human-readable reason when !ok (config still = defaults)
    bool fileExisted = false;
};

/// Pure: parse engines.json content. Unknown fields are tolerated (they belong
/// to the engine-management sections owned by wt-engines). Type-mismatched or
/// invalid-enum values fall back to defaults without failing the parse.
EngineConfigParseResult parseEngineConfig(const std::string& jsonUtf8);

/// Pure: merge an EngineConfig patch into existing engines.json text, keeping
/// every unknown key verbatim (parallel branches own their sections).
/// An empty existingJson produces a minimal valid file.
std::string serializeEngineConfigPatch(const std::string& existingJsonUtf8,
                                       const EngineConfig& patch);

struct AtomicWriteResult {
    bool ok = false;
    std::string error;
};

/// Atomically replace filePath with jsonUtf8: write `<file>.tmp`, flush, then
/// rename over the target (std::filesystem::rename — MOVEFILE_REPLACE_EXISTING
/// semantics on Windows, i.e. the AppMain writeTextFileAtomic contract).
/// std::filesystem only; no Win32 include.
AtomicWriteResult writeEngineConfigFileAtomic(const std::filesystem::path& filePath,
                                              const std::string& jsonUtf8);

}  // namespace tf::core

#endif  // TF_ENGINE_CONFIG_STORE_HPP
