// EngineConfigStore.cpp — engines.json merge/parse (pure; IO in the caller).
#include "tf/EngineConfigStore.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <fstream>
#include <utility>

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

bool isFinite(double v) {
    return std::isfinite(v);
}

// Read a string field: trim; empty / type mismatch -> fallback (keep default).
// `lower` additionally ASCII-lowercases the accepted value (mac targetLang
// normalization semantics).
std::string readString(const nlohmann::json& obj, const char* key,
                       const std::string& fallback, bool lower = false) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const nlohmann::json& v = obj.at(key);
    if (!v.is_string()) return fallback;
    std::string s = trimmed(v.get<std::string>());
    if (s.empty()) return fallback;
    return lower ? lowercased(std::move(s)) : s;
}

// Read an enum-tag string: accepted values validated against `allowed`
// (mac enum rawValues); anything else -> default (no parse failure).
std::string readEnum(const nlohmann::json& obj, const char* key,
                     const std::string& fallback,
                     std::initializer_list<const char*> allowed) {
    const std::string v = readString(obj, key, "");
    for (const char* a : allowed) {
        if (v == a) return v;
    }
    return fallback;
}

// Read a double in [lo, hi]; NaN/inf/out-of-range/type mismatch -> default.
// (mac stores raw Doubles in UserDefaults; the range check is the Windows
// hardening — a corrupt value must not produce a zero-width stroke etc.)
double readDouble(const nlohmann::json& obj, const char* key, double fallback,
                  double lo, double hi) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const nlohmann::json& v = obj.at(key);
    if (!v.is_number() || v.is_number_integer()) return fallback;
    const double d = v.get<double>();
    if (!isFinite(d) || d < lo || d > hi) return fallback;
    return d;
}

bool readBool(const nlohmann::json& obj, const char* key, bool fallback) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const nlohmann::json& v = obj.at(key);
    if (!v.is_boolean()) return fallback;
    return v.get<bool>();
}

// Read a {red,green,blue,opacity} object; any component missing/bad/out of
// [0,1] -> whole color falls back (mac CodableColor decodes atomically too).
ColorConfig readColor(const nlohmann::json& obj, const char* key, ColorConfig fallback) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const nlohmann::json& v = obj.at(key);
    if (!v.is_object()) return fallback;
    const double r = readDouble(v, "red", -1.0, 0.0, 1.0);
    const double g = readDouble(v, "green", -1.0, 0.0, 1.0);
    const double b = readDouble(v, "blue", -1.0, 0.0, 1.0);
    const double a = readDouble(v, "opacity", -1.0, 0.0, 1.0);
    if (r < 0 || g < 0 || b < 0 || a < 0) return fallback;
    return {r, g, b, a};
}

// Read a {keyCode, modifiers} object (mac shortcut storage shape).
ShortcutConfig readShortcut(const nlohmann::json& obj, const char* key,
                            ShortcutConfig fallback) {
    if (!obj.is_object() || !obj.contains(key)) return fallback;
    const nlohmann::json& v = obj.at(key);
    if (!v.is_object() || !v.contains("keyCode") || !v.contains("modifiers")) return fallback;
    const nlohmann::json& kc = v.at("keyCode");
    const nlohmann::json& mods = v.at("modifiers");
    if (!kc.is_number_unsigned() || !mods.is_number_unsigned()) return fallback;
    return {kc.get<std::uint32_t>(), mods.get<std::uint32_t>()};
}

// mac: EngineChainItem (Codable) — {"id": "<uuid>", "kind": {"builtin": "mtran"}}
// (Swift synthesizes a keyed enum container). Compatible entries decode the
// same way with "compatible": "<uuid>".
std::vector<EngineChainEntry> readChain(const nlohmann::json& obj, const char* key) {
    std::vector<EngineChainEntry> out;
    if (!obj.is_object() || !obj.contains(key)) return out;
    const nlohmann::json& v = obj.at(key);
    if (!v.is_array()) return out;
    for (const nlohmann::json& item : v) {
        if (!item.is_object() || !item.contains("kind") || !item.at("kind").is_object()) continue;
        const nlohmann::json& kind = item.at("kind");
        if (kind.contains("builtin") && kind.at("builtin").is_string()) {
            const std::string tag = kind.at("builtin").get<std::string>();
            if (!tag.empty()) out.push_back({false, tag, ""});
        } else if (kind.contains("compatible") && kind.at("compatible").is_string()) {
            const std::string uuid = kind.at("compatible").get<std::string>();
            if (!uuid.empty()) out.push_back({true, "", uuid});
        }
    }
    return out;
}

void writeChain(nlohmann::json& obj, const char* key,
                const std::vector<EngineChainEntry>& chain) {
    nlohmann::json arr = nlohmann::json::array();
    int index = 0;
    for (const EngineChainEntry& e : chain) {
        nlohmann::json kind = nlohmann::json::object();
        // Entry ids are opaque mac-side UUIDs; a deterministic index keeps
        // the Windows file round-trip stable without inventing UUIDs.
        kind[e.isCompatible ? "compatible" : "builtin"] =
            e.isCompatible ? e.compatibleId : e.builtinEngine;
        arr.push_back(nlohmann::json{{"id", "win-" + std::to_string(index++)},
                                     {"kind", std::move(kind)}});
    }
    obj[key] = std::move(arr);
}

}  // namespace

EngineConfig EngineConfig::makeDefault() {
    EngineConfig c;
    c.mtranBaseUrl = "http://localhost:8989";
    c.targetLang = "zh";
    c.engineChainV2 = {{false, "apple", ""}};
    c.compareEnginesV2 = {{false, "apple", ""}, {false, "mtran", ""}};
    return c;
}

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

    // v1 fields (contract in windows/docs/engines-config-shared.md).
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
    out.config.targetLang =
        readString(parsed, "targetLang", out.config.targetLang, /*lower*/ true);

    // General — mac keys camelCase: defaultFormat/jpegQuality/heicQuality/saveLocation.
    out.config.defaultFormat =
        readEnum(parsed, "defaultFormat", out.config.defaultFormat, {"png", "jpeg", "heic"});
    out.config.jpegQuality = readDouble(parsed, "jpegQuality", out.config.jpegQuality, 0.0, 1.0);
    out.config.heicQuality = readDouble(parsed, "heicQuality", out.config.heicQuality, 0.0, 1.0);
    out.config.saveLocation = readString(parsed, "saveLocation", out.config.saveLocation);

    // Translation languages — translationTargetLanguage == targetLang (v1 key).
    out.config.sourceLang = readString(parsed, "translationSourceLanguage",
                                       out.config.sourceLang, /*lower*/ true);
    out.config.autoDetect = readBool(parsed, "translationAutoDetect", out.config.autoDetect);

    // Engine selection (legacy single-pick keys; V2 below supersedes).
    out.config.ocrEngine = readEnum(parsed, "ocrEngine", out.config.ocrEngine, {"vision"});
    out.config.translationEngine =
        readEnum(parsed, "translationEngine", out.config.translationEngine,
                 {"apple", "mtran", "openai", "claude", "gemini", "ollama", "google", "custom"});
    out.config.translationMode =
        readEnum(parsed, "translationMode", out.config.translationMode, {"below", "inline"});
    out.config.preferredTranslationEngine =
        readEnum(parsed, "preferredTranslationEngine", out.config.preferredTranslationEngine,
                 {"apple", "mtran"});
    out.config.translationFallbackEnabled =
        readBool(parsed, "translationFallbackEnabled", out.config.translationFallbackEnabled);

    // Engine Selection V2 — engineModeV2/engineChainV2/compareEnginesV2.
    // Mac semantics: an empty/undecodable chain keeps the default (ordered
    // mode must never end up with nothing to run).
    out.config.engineModeV2 =
        readEnum(parsed, "engineModeV2", out.config.engineModeV2, {"ordered", "compare"});
    {
        std::vector<EngineChainEntry> chain = readChain(parsed, "engineChainV2");
        if (!chain.empty()) {
            if (chain.size() > 5) chain.resize(5);  // EngineSelectionLimits.orderedChainLimit
            out.config.engineChainV2 = std::move(chain);
        }
        std::vector<EngineChainEntry> compare = readChain(parsed, "compareEnginesV2");
        if (!compare.empty()) {
            if (compare.size() > 4) compare.resize(4);  // compareEnginesLimit
            out.config.compareEnginesV2 = std::move(compare);
        }
    }

    // Annotation defaults.
    out.config.strokeColor = readColor(parsed, "strokeColor", out.config.strokeColor);
    out.config.strokeWidth =
        readDouble(parsed, "strokeWidth", out.config.strokeWidth, 0.1, 100.0);
    out.config.textSize = readDouble(parsed, "textSize", out.config.textSize, 1.0, 400.0);
    out.config.rectangleFilled = readBool(parsed, "rectangleFilled",
                                          out.config.rectangleFilled);
    out.config.ellipseFilled = readBool(parsed, "ellipseFilled", out.config.ellipseFilled);
    out.config.shapeFillColor = readColor(parsed, "shapeFillColor", out.config.shapeFillColor);
    out.config.mosaicBlockSize =
        readDouble(parsed, "mosaicBlockSize", out.config.mosaicBlockSize, 1.0, 200.0);

    // Shortcuts — {keyCode, modifiers} objects (mac storage shape).
    out.config.fullScreenShortcut =
        readShortcut(parsed, "fullScreenShortcut", out.config.fullScreenShortcut);
    out.config.selectionShortcut =
        readShortcut(parsed, "selectionShortcut", out.config.selectionShortcut);
    out.config.translationModeShortcut =
        readShortcut(parsed, "translationModeShortcut", out.config.translationModeShortcut);
    out.config.textSelectionTranslationShortcut =
        readShortcut(parsed, "textSelectionTranslationShortcut",
                     out.config.textSelectionTranslationShortcut);
    out.config.translateAndInsertShortcut =
        readShortcut(parsed, "translateAndInsertShortcut",
                     out.config.translateAndInsertShortcut);
    out.config.pickColorShortcut =
        readShortcut(parsed, "pickColorShortcut", out.config.pickColorShortcut);

    // Onboarding.
    out.config.onboardingCompleted =
        readBool(parsed, "onboardingCompleted", out.config.onboardingCompleted);
    out.config.userSkippedPermissions =
        readBool(parsed, "userSkippedPermissions", out.config.userSkippedPermissions);

    // VLM.
    out.config.vlm.provider = readEnum(parsed, "vlmProvider", out.config.vlm.provider,
                                       {"openai", "claude", "gemini", "ollama", "paddleocr"});
    out.config.vlm.baseURL = readString(parsed, "vlmBaseURL", out.config.vlm.baseURL);
    out.config.vlm.modelName = readString(parsed, "vlmModelName", out.config.vlm.modelName);
    out.config.vlm.openAIMode =
        readEnum(parsed, "vlmOpenAIMode", out.config.vlm.openAIMode, {"chat", "responses"});

    // PaddleOCR (API key intentionally not read: Keychain/CredMan only).
    out.config.paddleOcr.mode =
        readEnum(parsed, "paddleOCRMode", out.config.paddleOcr.mode, {"fast", "precise"});
    out.config.paddleOcr.useCloud =
        readBool(parsed, "paddleOCRUseCloud", out.config.paddleOcr.useCloud);
    out.config.paddleOcr.cloudBaseURL =
        readString(parsed, "paddleOCRCloudBaseURL", out.config.paddleOcr.cloudBaseURL);
    out.config.paddleOcr.cloudModelId =
        readString(parsed, "paddleOCRCloudModelId", out.config.paddleOcr.cloudModelId);
    out.config.paddleOcr.localVlModelDir =
        readString(parsed, "paddleOCRLocalVLModelDir", out.config.paddleOcr.localVlModelDir);

    // Translate-and-insert languages (empty targetLang = follow system).
    out.config.translateAndInsert.sourceLang =
        readString(parsed, "translateAndInsertSourceLanguage",
                   out.config.translateAndInsert.sourceLang, /*lower*/ true);
    out.config.translateAndInsert.targetLang =
        readString(parsed, "translateAndInsertTargetLanguage",
                   out.config.translateAndInsert.targetLang, /*lower*/ true);

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
    root["mtran"] = std::move(mtran);

    auto put = [&root](const char* k, nlohmann::json v) { root[k] = std::move(v); };

    put("targetLang", patch.targetLang);
    put("defaultFormat", patch.defaultFormat);
    put("jpegQuality", patch.jpegQuality);
    put("heicQuality", patch.heicQuality);
    put("saveLocation", patch.saveLocation);
    put("translationSourceLanguage", patch.sourceLang);
    put("translationAutoDetect", patch.autoDetect);
    put("ocrEngine", patch.ocrEngine);
    put("translationEngine", patch.translationEngine);
    put("translationMode", patch.translationMode);
    put("preferredTranslationEngine", patch.preferredTranslationEngine);
    put("translationFallbackEnabled", patch.translationFallbackEnabled);
    put("engineModeV2", patch.engineModeV2);
    writeChain(root, "engineChainV2", patch.engineChainV2);
    writeChain(root, "compareEnginesV2", patch.compareEnginesV2);
    put("strokeColor", nlohmann::json{{"red", patch.strokeColor.red},
                                      {"green", patch.strokeColor.green},
                                      {"blue", patch.strokeColor.blue},
                                      {"opacity", patch.strokeColor.opacity}});
    put("strokeWidth", patch.strokeWidth);
    put("textSize", patch.textSize);
    put("rectangleFilled", patch.rectangleFilled);
    put("ellipseFilled", patch.ellipseFilled);
    put("shapeFillColor", nlohmann::json{{"red", patch.shapeFillColor.red},
                                         {"green", patch.shapeFillColor.green},
                                         {"blue", patch.shapeFillColor.blue},
                                         {"opacity", patch.shapeFillColor.opacity}});
    put("mosaicBlockSize", patch.mosaicBlockSize);
    put("fullScreenShortcut",
        nlohmann::json{{"keyCode", patch.fullScreenShortcut.keyCode},
                       {"modifiers", patch.fullScreenShortcut.modifiers}});
    put("selectionShortcut",
        nlohmann::json{{"keyCode", patch.selectionShortcut.keyCode},
                       {"modifiers", patch.selectionShortcut.modifiers}});
    put("translationModeShortcut",
        nlohmann::json{{"keyCode", patch.translationModeShortcut.keyCode},
                       {"modifiers", patch.translationModeShortcut.modifiers}});
    put("textSelectionTranslationShortcut",
        nlohmann::json{{"keyCode", patch.textSelectionTranslationShortcut.keyCode},
                       {"modifiers", patch.textSelectionTranslationShortcut.modifiers}});
    put("translateAndInsertShortcut",
        nlohmann::json{{"keyCode", patch.translateAndInsertShortcut.keyCode},
                       {"modifiers", patch.translateAndInsertShortcut.modifiers}});
    put("pickColorShortcut",
        nlohmann::json{{"keyCode", patch.pickColorShortcut.keyCode},
                       {"modifiers", patch.pickColorShortcut.modifiers}});
    put("onboardingCompleted", patch.onboardingCompleted);
    put("userSkippedPermissions", patch.userSkippedPermissions);
    put("vlmProvider", patch.vlm.provider);
    put("vlmBaseURL", patch.vlm.baseURL);
    put("vlmModelName", patch.vlm.modelName);
    put("vlmOpenAIMode", patch.vlm.openAIMode);
    put("paddleOCRMode", patch.paddleOcr.mode);
    put("paddleOCRUseCloud", patch.paddleOcr.useCloud);
    put("paddleOCRCloudBaseURL", patch.paddleOcr.cloudBaseURL);
    put("paddleOCRCloudModelId", patch.paddleOcr.cloudModelId);
    put("paddleOCRLocalVLModelDir", patch.paddleOcr.localVlModelDir);
    put("translateAndInsertSourceLanguage", patch.translateAndInsert.sourceLang);
    put("translateAndInsertTargetLanguage", patch.translateAndInsert.targetLang);

    return root.dump(2) + "\n";
}

AtomicWriteResult writeEngineConfigFileAtomic(const std::filesystem::path& filePath,
                                              const std::string& jsonUtf8) {
    AtomicWriteResult out;
    std::error_code ec;

    const std::filesystem::path tmp = filePath.string() + ".tmp";
    {
        std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
        if (!f) {
            out.error = "无法创建临时文件: " + tmp.string();
            return out;
        }
        f.write(jsonUtf8.data(), static_cast<std::streamsize>(jsonUtf8.size()));
        f.flush();
        if (!f) {
            out.error = "写入临时文件失败: " + tmp.string();
            std::remove(tmp.string().c_str());
            return out;
        }
    }
    std::filesystem::rename(tmp, filePath, ec);
    if (ec) {
        // rename over an existing file may fail via std::filesystem's strict
        // mapping; remove-then-rename matches MOVEFILE_REPLACE_EXISTING.
        std::error_code removeEc;
        std::filesystem::remove(filePath, removeEc);
        std::filesystem::rename(tmp, filePath, ec);
        if (ec) {
            out.error = "原子替换失败: " + ec.message();
            std::remove(tmp.string().c_str());
            return out;
        }
    }
    out.ok = true;
    return out;
}

}  // namespace tf::core
