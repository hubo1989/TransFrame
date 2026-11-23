#pragma once
// TranslationEngineType.h — translation engine inventory.
//
// Schema alignment with macOS TransFrame/Models/TranslationEngineType.swift:
//   raw values identical; `.apple` intentionally ABSENT on Windows
//   (PRD §6.12: no public system translation API; default chain head is
//   MTranServer). JSON keys written by macOS that reference "apple" are
//   tolerated on read (parse yields Invalid and the entry is dropped by the
//   caller) so a settings file shared across platforms degrades gracefully.

#include <optional>
#include <string>

namespace tf::core {

enum class TranslationEngineType : int32_t {
    MtranServer,   // "mtran"    — external MTranServer instance
    OpenAI,        // "openai"   — GPT
    Claude,        // "claude"   — Anthropic
    Gemini,        // "gemini"   — Google Gemini
    Ollama,        // "ollama"   — local LLM endpoint
    Google,        // "google"   — Google Cloud Translation
    DeepL,         // "deepl"    — DeepL API
    Baidu,         // "baidu"    — Baidu Translate API
    Custom,        // "custom"   — OpenAI-compatible instance
    Invalid = -1,
};

namespace TranslationEngineTypes {
// Swift `allCases` order minus `.apple` (macOS: apple, mtran, openai, claude,
// gemini, ollama, google, deepl, baidu, custom).
constexpr TranslationEngineType kAll[] = {
    TranslationEngineType::MtranServer, TranslationEngineType::OpenAI,
    TranslationEngineType::Claude,      TranslationEngineType::Gemini,
    TranslationEngineType::Ollama,      TranslationEngineType::Google,
    TranslationEngineType::DeepL,       TranslationEngineType::Baidu,
    TranslationEngineType::Custom,
};
constexpr size_t kCount = sizeof(kAll) / sizeof(kAll[0]);
}  // namespace TranslationEngineTypes

// Free functions so templates can use Enum::fromRaw via ADL-style helpers.
inline std::string toRaw(TranslationEngineType e) {
    switch (e) {
        case TranslationEngineType::MtranServer: return "mtran";
        case TranslationEngineType::OpenAI: return "openai";
        case TranslationEngineType::Claude: return "claude";
        case TranslationEngineType::Gemini: return "gemini";
        case TranslationEngineType::Ollama: return "ollama";
        case TranslationEngineType::Google: return "google";
        case TranslationEngineType::DeepL: return "deepl";
        case TranslationEngineType::Baidu: return "baidu";
        case TranslationEngineType::Custom: return "custom";
        default: return "";
    }
}

inline TranslationEngineType fromRaw(TranslationEngineType, const std::string& raw) {
    if (raw == "mtran") { return TranslationEngineType::MtranServer; }
    if (raw == "openai") { return TranslationEngineType::OpenAI; }
    if (raw == "claude") { return TranslationEngineType::Claude; }
    if (raw == "gemini") { return TranslationEngineType::Gemini; }
    if (raw == "ollama") { return TranslationEngineType::Ollama; }
    if (raw == "google") { return TranslationEngineType::Google; }
    if (raw == "deepl") { return TranslationEngineType::DeepL; }
    if (raw == "baidu") { return TranslationEngineType::Baidu; }
    if (raw == "custom") { return TranslationEngineType::Custom; }
    return TranslationEngineType::Invalid;
}

// Adapter shape used by forEachEnumKey.
struct TranslationEngineTypeAdapter {
    using Enum = TranslationEngineType;
    static std::optional<TranslationEngineType> fromRaw(const std::string& raw) {
        const TranslationEngineType e = tf::core::fromRaw(TranslationEngineType{}, raw);
        if (e == TranslationEngineType::Invalid) { return std::nullopt; }
        return e;
    }
};

/// Settings-page cards (macOS uiCards minus apple; `ollama` is configured via
/// the OpenAI-compatible card, `custom` via the dynamic instances section).
inline const TranslationEngineType* uiCards(size_t& count) {
    static const TranslationEngineType cards[] = {
        TranslationEngineType::MtranServer, TranslationEngineType::OpenAI,
        TranslationEngineType::Claude,      TranslationEngineType::Gemini,
        TranslationEngineType::Google,      TranslationEngineType::DeepL,
        TranslationEngineType::Baidu,
    };
    count = sizeof(cards) / sizeof(cards[0]);
    return cards;
}

inline bool requiresApiKey(TranslationEngineType e) {
    switch (e) {
        case TranslationEngineType::MtranServer:
        case TranslationEngineType::Ollama:
            return false;
        default:
            return true;  // openai/claude/gemini/google/deepl/custom/baidu
    }
}

}  // namespace tf::core
