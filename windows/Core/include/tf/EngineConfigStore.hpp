#pragma once
// tf/EngineConfigStore.hpp — engines.json read/write (FR-35, shared contract).
//
// File: %APPDATA%\TransFrame\engines.json (Roaming; share with wt-engines —
// full contract in windows/docs/engines-config-shared.md):
//   {
//     "mtran":     { "baseUrl": "http://localhost:8989" },
//     "targetLang": "zh"
//   }
//
// This module is pure (string in / string out); file IO stays with the caller
// (std::filesystem on Windows, temp dirs in tests) so Core stays platform-clean
// and the JSON merge semantics are unit-tested without a disk.
#ifndef TF_ENGINE_CONFIG_STORE_HPP
#define TF_ENGINE_CONFIG_STORE_HPP

#include <string>

namespace tf::core {

struct EngineConfig {
    std::string mtranBaseUrl;  // "http://host[:port]" — caller validates via MTranServerProvider::Configuration::parse
    std::string targetLang;    // lowercase short tag, e.g. "zh"

    static EngineConfig makeDefault() { return {"http://localhost:8989", "zh"}; }
};

struct EngineConfigParseResult {
    EngineConfig config;    // fields missing from the file fall back to defaults
    bool ok = true;         // false only when the file exists but is not valid JSON
    std::string error;      // human-readable reason when !ok (config still = defaults)
    bool fileExisted = false;
};

/// Pure: parse engines.json content. Unknown fields are tolerated (they belong
/// to the engine-management sections owned by wt-engines).
EngineConfigParseResult parseEngineConfig(const std::string& jsonUtf8);

/// Pure: merge an EngineConfig patch into existing engines.json text, keeping
/// every unknown key verbatim (parallel branches own their sections).
/// An empty existingJson produces a minimal valid file.
std::string serializeEngineConfigPatch(const std::string& existingJsonUtf8,
                                       const EngineConfig& patch);

}  // namespace tf::core

#endif  // TF_ENGINE_CONFIG_STORE_HPP
