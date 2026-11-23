// Onboarding.cpp — FR-39 pure logic (see Onboarding.hpp).
#include "tf/Onboarding.hpp"

#include <nlohmann/json.hpp>

namespace tf::core {

OnboardingChoiceInfo onboardingChoiceInfo(OnboardingChainChoice choice) {
    switch (choice) {
        case OnboardingChainChoice::MtranLocal:
            return {"mtran", "http://localhost:8989", true};
        case OnboardingChainChoice::OllamaLocal:
            return {"ollama", "http://localhost:11434", true};
        case OnboardingChainChoice::CloudOpenAI:
            return {"openai", "", false};
    }
    return {};
}

std::vector<HotkeyBinding> defaultOnboardingHotkeys() {
    // MOD_* bits: Alt=1 Control=2 (HotkeySettingsStore storage shape; matches
    // the shortcuts page defaults Ctrl+Alt+F/A/T/D/I).
    constexpr std::uint32_t kCtrlAlt = 0x3;
    return {
        {"fullScreen", 0x46, kCtrlAlt, true},        // F
        {"region", 0x41, kCtrlAlt, true},            // A
        {"translate", 0x54, kCtrlAlt, true},         // T
        {"textSelection", 0x44, kCtrlAlt, true},     // D
        {"translateInsert", 0x49, kCtrlAlt, true},   // I
    };
}

std::vector<HotkeyBinding> resolveOnboardingHotkeys(const std::string& settingsJsonUtf8) {
    std::vector<HotkeyBinding> rows = defaultOnboardingHotkeys();
    for (const HotkeyBinding& b : parseHotkeyBindings(settingsJsonUtf8)) {
        for (HotkeyBinding& row : rows) {
            if (row.actionName == b.actionName) {
                row.vk = b.vk;
                row.modifiers = b.modifiers;
                row.enabled = b.enabled;
            }
        }
    }
    return rows;
}

std::vector<OnboardingEngineRow> parseOnboardingEngines(const std::string& enginesJsonUtf8) {
    std::vector<OnboardingEngineRow> out;
    const nlohmann::json root = nlohmann::json::parse(enginesJsonUtf8, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return out;
    const auto engines = root.find("engines");
    if (engines == root.end() || !engines->is_array()) return out;
    for (const auto& e : *engines) {
        if (!e.is_object()) continue;
        const auto id = e.find("engine");
        if (id == e.end() || !id->is_string()) continue;
        OnboardingEngineRow row;
        row.engineId = id->get<std::string>();
        row.enabled = e.value("enabled", true);
        if (const auto url = e.find("baseURL"); url != e.end() && url->is_string()) {
            row.baseURL = url->get<std::string>();
        }
        out.push_back(std::move(row));
    }
    return out;
}

namespace {

bool isLocalHost(std::string host) {
    if (host.empty()) return true;  // no URL -> engine default; mtran/ollama default local
    for (char& c : host) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return host == "localhost" || host == "127.0.0.1" || host == "::1" ||
           host == "[::1]" || host.rfind("127.", 0) == 0;
}

}  // namespace

std::string onboardingUrlHost(const std::string& url) {
    const auto scheme = url.find("://");
    const std::size_t authorityStart = scheme == std::string::npos ? 0 : scheme + 3;
    std::size_t end = url.find_first_of("/\\?#", authorityStart);
    if (end == std::string::npos) end = url.size();
    std::string authority = url.substr(authorityStart, end - authorityStart);
    // Strip userinfo and port.
    const auto at = authority.rfind('@');
    if (at != std::string::npos) authority.erase(0, at + 1);
    const auto colon = authority.rfind(':');
    if (colon != std::string::npos) authority.erase(colon);
    return authority;
}

bool onboardingEngineSendsToCloud(const OnboardingEngineRow& row) {
    if (!row.enabled) return false;
    if (isLocalHost(onboardingUrlHost(row.baseURL))) return false;
    // Empty baseURL means the engine's default endpoint: only mtran/ollama
    // default to localhost; every other engine is a cloud API by default.
    if (row.baseURL.empty() &&
        (row.engineId == "mtran" || row.engineId == "ollama")) {
        return false;
    }
    return true;
}

std::string mergeOnboardingChain(const std::string& existingEnginesJsonUtf8,
                                 OnboardingChainChoice choice) {
    nlohmann::json root = nlohmann::json::object();
    if (!existingEnginesJsonUtf8.empty()) {
        const nlohmann::json parsed = nlohmann::json::parse(existingEnginesJsonUtf8,
                                                            nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) root = parsed;
    }
    const OnboardingChoiceInfo info = onboardingChoiceInfo(choice);

    nlohmann::json row = nlohmann::json::object();
    row["engine"] = info.engineId;
    row["enabled"] = true;
    if (info.baseURL[0] != '\0') row["baseURL"] = info.baseURL;

    if (!root.contains("engines") || !root["engines"].is_array()) {
        root["engines"] = nlohmann::json::array();
    }
    nlohmann::json& engines = root["engines"];
    bool updated = false;
    for (auto& e : engines) {
        if (e.is_object() && e.value("engine", std::string()) == info.engineId) {
            e["enabled"] = true;
            if (info.baseURL[0] != '\0') e["baseURL"] = info.baseURL;
            updated = true;
            break;
        }
    }
    if (!updated) engines.push_back(row);

    nlohmann::json chain = nlohmann::json::array();
    nlohmann::json entry = nlohmann::json::object();
    entry["kind"] = "builtin";
    entry["engine"] = info.engineId;
    chain.push_back(std::move(entry));
    root["globalChain"] = std::move(chain);

    return root.dump(2) + "\n";
}

std::string serializeOnboardingDoneFlag(const std::string& existingSettingsJsonUtf8) {
    nlohmann::json root = nlohmann::json::object();
    if (!existingSettingsJsonUtf8.empty()) {
        const nlohmann::json parsed = nlohmann::json::parse(existingSettingsJsonUtf8,
                                                            nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) root = parsed;
    }
    root["onboardingDone"] = true;
    return root.dump(2) + "\n";
}

bool parseOnboardingDone(const std::string& settingsJsonUtf8) {
    const nlohmann::json root = nlohmann::json::parse(settingsJsonUtf8, nullptr, false);
    if (root.is_discarded() || !root.is_object()) return false;
    const auto it = root.find("onboardingDone");
    return it != root.end() && it->is_boolean() && it->get<bool>();
}

}  // namespace tf::core
