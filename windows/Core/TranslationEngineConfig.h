#pragma once
// TranslationEngineConfig.h — per-engine configuration
// (macOS TranslationEngineConfig.swift). Encoded as an array of objects in
// UserDefaults key "TransFrame.engineConfigs".

#include "Json.h"
#include "TranslationEngineType.h"

#include <map>
#include <optional>
#include <string>

namespace tf::core {

struct EngineOptions {
    std::optional<std::string> baseURL;
    std::optional<std::string> modelName;
    std::optional<double> timeout;        // seconds
    std::optional<int32_t> maxTokens;
    std::optional<double> temperature;    // 0.0 - 2.0
    std::optional<std::map<std::string, std::string>> customHeaders;

    friend bool operator==(const EngineOptions&, const EngineOptions&) = default;

    void toJson(Json& j) const {
        j = Json::object();
        if (baseURL) { j["baseURL"] = *baseURL; }
        if (modelName) { j["modelName"] = *modelName; }
        if (timeout) { j["timeout"] = *timeout; }
        if (maxTokens) { j["maxTokens"] = *maxTokens; }
        if (temperature) { j["temperature"] = *temperature; }
        if (customHeaders) {
            Json headers = Json::object();
            for (const auto& [k, v] : *customHeaders) { headers[k] = v; }
            j["customHeaders"] = std::move(headers);
        }
    }
    static EngineOptions fromJson(const Json& j) {
        EngineOptions o;
        o.baseURL = getOptional<std::string>(j, "baseURL");
        o.modelName = getOptional<std::string>(j, "modelName");
        o.timeout = getOptional<double>(j, "timeout");
        o.maxTokens = getOptional<int32_t>(j, "maxTokens");
        o.temperature = getOptional<double>(j, "temperature");
        if (auto it = j.find("customHeaders"); it != j.end() && it->is_object()) {
            std::map<std::string, std::string> headers;
            for (auto h = it->begin(); h != it->end(); ++h) {
                if (h.value().is_string()) { headers[h.key()] = h.value().get<std::string>(); }
            }
            o.customHeaders = std::move(headers);
        }
        return o;
    }
};

inline void to_json(Json& j, const EngineOptions& o) { o.toJson(j); }
inline void from_json(const Json& j, EngineOptions& o) { o = EngineOptions::fromJson(j); }

struct TranslationEngineConfig {
    TranslationEngineType id = TranslationEngineType::MtranServer;
    bool isEnabled = false;
    std::optional<EngineOptions> options;
    /// Custom display name; for .custom engines macOS smuggles the serialized
    /// CompatibleConfig JSON here (kept verbatim for migration compatibility).
    std::optional<std::string> customName;

    TranslationEngineConfig() = default;
    TranslationEngineConfig(TranslationEngineType type, bool enabled = false,
                            std::optional<EngineOptions> opts = std::nullopt,
                            std::optional<std::string> name = std::nullopt)
        : id(type), isEnabled(enabled), options(std::move(opts)), customName(std::move(name)) {}

    /// macOS default: only the platform-native engine is enabled. On Windows
    /// there is no Apple engine, so no engine is default-enabled; the first-run
    /// onboarding forces a default chain choice instead (PRD §12 risk table).
    static TranslationEngineConfig defaultFor(TranslationEngineType type) {
        return TranslationEngineConfig(type, /*enabled=*/false, EngineOptions());
    }

    friend bool operator==(const TranslationEngineConfig&,
                           const TranslationEngineConfig&) = default;

    void toJson(Json& j) const {
        j = Json{{"id", toRaw(id)}, {"isEnabled", isEnabled}};
        j["options"] = options ? Json(*options) : Json();
        j["customName"] = customName ? Json(*customName) : Json();
    }
    static TranslationEngineConfig fromJson(const Json& j) {
        TranslationEngineConfig c;
        const std::string raw = j.at("id").get<std::string>();
        c.id = fromRaw(TranslationEngineType{}, raw);
        if (c.id == TranslationEngineType::Invalid) {
            throw std::invalid_argument("unknown engine id: " + raw);
        }
        c.isEnabled = getOptionalOr<bool>(j, "isEnabled", false);
        c.options = getOptional<EngineOptions>(j, "options");
        c.customName = getOptional<std::string>(j, "customName");
        return c;
    }
};

inline void to_json(Json& j, const TranslationEngineConfig& c) { c.toJson(j); }
inline void from_json(const Json& j, TranslationEngineConfig& c) {
    c = TranslationEngineConfig::fromJson(j);
}

}  // namespace tf::core
