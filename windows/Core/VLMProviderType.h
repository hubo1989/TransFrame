#pragma once
// VLMProviderType.h — VLM analysis providers + OpenAI API mode + compatible
// instance config (macOS VLMProviderType.swift + CompatibleConfig in
// CompatibleTranslationProvider.swift).

#include "Json.h"
#include "Uuid.h"

#include <optional>
#include <string>
#include <vector>

namespace tf::core {

enum class VLMProviderType : int32_t {
    OpenAI,     // "openai"
    Claude,     // "claude"
    Gemini,     // "gemini"
    Ollama,     // "ollama"
    PaddleOCR,  // "paddleocr"
    Invalid = -1,
};

inline std::string toRaw(VLMProviderType p) {
    switch (p) {
        case VLMProviderType::OpenAI: return "openai";
        case VLMProviderType::Claude: return "claude";
        case VLMProviderType::Gemini: return "gemini";
        case VLMProviderType::Ollama: return "ollama";
        case VLMProviderType::PaddleOCR: return "paddleocr";
        default: return "";
    }
}
inline VLMProviderType vlmProviderFromRaw(const std::string& raw) {
    if (raw == "openai") { return VLMProviderType::OpenAI; }
    if (raw == "claude") { return VLMProviderType::Claude; }
    if (raw == "gemini") { return VLMProviderType::Gemini; }
    if (raw == "ollama") { return VLMProviderType::Ollama; }
    if (raw == "paddleocr") { return VLMProviderType::PaddleOCR; }
    return VLMProviderType::Invalid;
}
inline const char* vlmProviderDisplayName(VLMProviderType p) {
    switch (p) {
        case VLMProviderType::OpenAI: return "OpenAI";
        case VLMProviderType::Claude: return "Claude";
        case VLMProviderType::Gemini: return "Gemini";
        case VLMProviderType::Ollama: return "本地模型";
        case VLMProviderType::PaddleOCR: return "PaddleOCR";
        default: return "";
    }
}
inline std::string vlmDefaultBaseURL(VLMProviderType p) {
    switch (p) {
        case VLMProviderType::OpenAI: return "https://api.openai.com/v1";
        case VLMProviderType::Claude: return "https://api.anthropic.com/v1";
        case VLMProviderType::Gemini: return "https://generativelanguage.googleapis.com/v1beta";
        case VLMProviderType::Ollama: return "http://localhost:11434";
        case VLMProviderType::PaddleOCR: return "";
        default: return "";
    }
}
inline std::string vlmDefaultModelName(VLMProviderType p) {
    switch (p) {
        case VLMProviderType::OpenAI: return "gpt-4o";
        case VLMProviderType::Claude: return "claude-sonnet-4-20250514";
        case VLMProviderType::Gemini: return "gemini-2.0-flash";
        case VLMProviderType::Ollama: return "llava";
        case VLMProviderType::PaddleOCR: return "";
        default: return "";
    }
}
inline bool vlmTypicallyRequiresAPIKey(VLMProviderType p) {
    switch (p) {
        case VLMProviderType::OpenAI:
        case VLMProviderType::Claude:
        case VLMProviderType::Gemini:
            return true;
        default:
            return false;
    }
}

/// OpenAI API mode (chat completions vs responses API).
enum class OpenAIAPIMode : int32_t { Chat, Response, Invalid = -1 };
inline std::string toRaw(OpenAIAPIMode m) {
    if (m == OpenAIAPIMode::Chat) { return "chat"; }
    if (m == OpenAIAPIMode::Response) { return "response"; }
    return "";
}
inline OpenAIAPIMode openAIAPIModeFromRaw(const std::string& raw) {
    if (raw == "chat") { return OpenAIAPIMode::Chat; }
    if (raw == "response") { return OpenAIAPIMode::Response; }
    return OpenAIAPIMode::Invalid;
}

/// PaddleOCR mode (fast: ocr command; precise: doc_parser VL-1.5).
enum class PaddleOCRMode : int32_t { Fast, Precise, Invalid = -1 };
inline std::string toRaw(PaddleOCRMode m) {
    if (m == PaddleOCRMode::Fast) { return "fast"; }
    if (m == PaddleOCRMode::Precise) { return "precise"; }
    return "";
}
inline PaddleOCRMode paddleOCRModeFromRaw(const std::string& raw) {
    if (raw == "fast") { return PaddleOCRMode::Fast; }
    if (raw == "precise") { return PaddleOCRMode::Precise; }
    return PaddleOCRMode::Invalid;
}

/// One OpenAI-compatible instance (macOS CompatibleConfig). Persisted in the
/// SettingsKeys::compatibleProviderConfigs array; the nil UUID is the default.
struct CompatibleConfig {
    Uuid id;
    std::string displayName;
    std::string baseURL;
    std::string modelName;
    bool hasAPIKey = false;

    static CompatibleConfig defaultConfig() {
        CompatibleConfig c;
        c.id = Uuid();
        c.displayName = "Custom";
        c.baseURL = "http://localhost:8000/v1";
        c.modelName = "default";
        c.hasAPIKey = false;
        return c;
    }

    /// Credential account: "custom:<uuid>" (Swift keychainId).
    std::string keychainId() const { return "custom:" + id.toString(); }

    friend bool operator==(const CompatibleConfig&, const CompatibleConfig&) = default;
};

void to_json(Json& j, const CompatibleConfig& c);
void from_json(const Json& j, CompatibleConfig& c);

}  // namespace tf::core
