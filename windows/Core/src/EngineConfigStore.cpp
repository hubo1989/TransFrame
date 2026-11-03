// EngineConfigStore.cpp — engines.json merge/parse (pure; IO in the caller).
#include "tf/EngineConfigStore.hpp"

#include <nlohmann/json.hpp>

namespace tf::core {
namespace {

std::string trimmed(std::string s) {
    const auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n';
    };
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
    while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) s.pop_back();
    return s;
}

// Lowercase ASCII only — targetLang is a short tag ("zh", "en-US"); Unicode
// case mapping is out of scope for a config tag.
std::string lowercased(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

}  // namespace

EngineConfigParseResult parseEngineConfig(const std::string& jsonUtf8) {
    EngineConfigParseResult out;
    out.config = EngineConfig::makeDefault();
    out.fileExisted = !jsonUtf8.empty();
    if (jsonUtf8.empty()) return out;

    // allow_exceptions=false -> discarded on syntax error; a non-object root
    // (bare number/string/array) is also treated as a corrupt file.
    const nlohmann::json parsed =
        nlohmann::json::parse(jsonUtf8, /*callback*/ nullptr, /*allow_exceptions*/ false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        out.ok = false;
        out.error = "engines.json 不是有效的 JSON 对象";
        return out;
    }

    // mtran.baseUrl — missing section or empty string both mean "unset" -> default.
    if (parsed.contains("mtran")) {
        const nlohmann::json& mtran = parsed.at("mtran");
        if (mtran.is_object() && mtran.contains("baseUrl")) {
            const nlohmann::json& url = mtran.at("baseUrl");
            if (url.is_string()) {
                const std::string baseUrl = trimmed(url.get<std::string>());
                if (!baseUrl.empty()) out.config.mtranBaseUrl = baseUrl;
            }
        }
    }

    // targetLang — missing/empty -> default; normalized (trim + ASCII lowercase).
    if (parsed.contains("targetLang")) {
        const nlohmann::json& lang = parsed.at("targetLang");
        if (lang.is_string()) {
            const std::string lang_ = trimmed(lang.get<std::string>());
            if (!lang_.empty()) out.config.targetLang = lowercased(lang_);
        }
    }

    return out;
}

std::string serializeEngineConfigPatch(const std::string& existingJsonUtf8,
                                       const EngineConfig& patch) {
    // Existing text is a merge base; junk is tolerated by starting from {}.
    nlohmann::json root = nlohmann::json::object();
    if (!existingJsonUtf8.empty()) {
        const nlohmann::json parsed = nlohmann::json::parse(existingJsonUtf8,
                                                            /*callback*/ nullptr,
                                                            /*allow_exceptions*/ false);
        if (!parsed.is_discarded() && parsed.is_object()) root = parsed;
    }

    nlohmann::json mtran = nlohmann::json::object();
    if (root.contains("mtran") && root.at("mtran").is_object()) {
        mtran = root.at("mtran");  // keep sibling mtran.* fields owned by wt-engines
    }
    mtran["baseUrl"] = patch.mtranBaseUrl;
    root["mtran"] = mtran;
    root["targetLang"] = patch.targetLang;
    return root.dump(2) + "\n";
}

}  // namespace tf::core
