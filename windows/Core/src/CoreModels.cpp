// CoreModels.cpp — implementation for tf/CoreModels.hpp
#include "tf/CoreModels.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>

namespace tf {

// ---------------------------------------------------------------------------
// UUID
// ---------------------------------------------------------------------------
Uuid Uuid::random() {
    static thread_local std::mt19937_64 rng([] {
        std::random_device rd;
        std::seed_seq seq{rd(), rd(), rd(), rd()};
        return std::mt19937_64(seq);
    }());
    Uuid u;
    for (auto& b : u.bytes) b = static_cast<std::uint8_t>(rng() & 0xFF);
    // RFC 4122 v4 markers.
    u.bytes[6] = (u.bytes[6] & 0x0F) | 0x40;
    u.bytes[8] = (u.bytes[8] & 0x3F) | 0x80;
    return u;
}

Uuid Uuid::nil() {
    Uuid u;
    u.bytes.fill(0);
    return u;
}

bool Uuid::isNil() const {
    return *this == Uuid::nil();
}

static int hexVal(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<Uuid> Uuid::parse(const std::string& text) {
    std::string s = text;
    // Trim.
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch) { return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch) { return !std::isspace(ch); }).base(), s.end());
    // Strip urn / braces.
    if (s.rfind("urn:uuid:", 0) == 0) s = s.substr(9);
    if (!s.empty() && s.front() == '{' && s.back() == '}') s = s.substr(1, s.size() - 2);

    auto isHex = [](const std::string& t, std::size_t from, std::size_t n) {
        if (from + n > t.size()) return false;
        for (std::size_t i = from; i < from + n; ++i) {
            if (hexVal(t[i]) < 0) return false;
        }
        return true;
    };
    // Accept canonical 8-4-4-4-12 and plain 32-hex.
    if (s.size() == 36 && s[8] == '-' && s[13] == '-' && s[18] == '-' && s[23] == '-' &&
        isHex(s, 0, 8) && isHex(s, 9, 4) && isHex(s, 14, 4) && isHex(s, 19, 4) && isHex(s, 24, 12)) {
        Uuid u;
        int hi = -1;
        int idx = 0;
        for (char c : s) {
            if (c == '-') continue;
            int v = hexVal(c);
            if (hi < 0) hi = v;
            else {
                u.bytes[idx++] = static_cast<std::uint8_t>((hi << 4) | v);
                hi = -1;
            }
        }
        return u;
    }
    if (s.size() == 32 && isHex(s, 0, 32)) {
        Uuid u;
        int hi = -1;
        int idx = 0;
        for (char c : s) {
            int v = hexVal(c);
            if (hi < 0) hi = v;
            else {
                u.bytes[idx++] = static_cast<std::uint8_t>((hi << 4) | v);
                hi = -1;
            }
        }
        return u;
    }
    return std::nullopt;
}

std::string Uuid::toString() const {
    static const char* hex = "0123456789abcdef";
    std::string s;
    s.reserve(36);
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) s += '-';
        s += hex[bytes[i] >> 4];
        s += hex[bytes[i] & 0xF];
    }
    return s;
}

// ---------------------------------------------------------------------------
// NormRect / TextSegment / OcrResult
// ---------------------------------------------------------------------------
NormRect NormRect::unionWith(const NormRect& o) const {
    const double l = std::min(minX(), o.minX());
    const double t = std::min(minY(), o.minY());
    const double r = std::max(maxX(), o.maxX());
    const double b = std::max(maxY(), o.maxY());
    return {l, t, r - l, b - t};
}

TextSegment::TextSegment(std::string text_, NormRect box, float conf, bool edited)
    : text(std::move(text_)), boundingBox(box), confidence(conf), isUserEdited(edited) {
    id = Uuid::random().toString();
}

TextSegment TextSegment::make(std::string text, NormRect box, float conf) {
    return TextSegment(std::move(text), box, conf, false);
}

std::string OcrResult::fullText() const {
    std::vector<const TextSegment*> sorted;
    sorted.reserve(observations.size());
    for (const auto& o : observations) sorted.push_back(&o);
    std::stable_sort(sorted.begin(), sorted.end(), [](const TextSegment* a, const TextSegment* b) {
        return a->boundingBox.minY() < b->boundingBox.minY();
    });
    std::string out;
    for (const auto* s : sorted) {
        if (!out.empty()) out += '\n';
        out += s->text;
    }
    return out;
}

OcrResult OcrResult::empty(std::int64_t w, std::int64_t h) {
    OcrResult r;
    r.imageWidth = w;
    r.imageHeight = h;
    r.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    return r;
}

// ---------------------------------------------------------------------------
// TranslationEngineType
// ---------------------------------------------------------------------------
std::vector<TranslationEngineType> allTranslationEngines() {
    return {TranslationEngineType::MtranServer, TranslationEngineType::OpenAI,
            TranslationEngineType::Claude, TranslationEngineType::Gemini,
            TranslationEngineType::Ollama, TranslationEngineType::Google,
            TranslationEngineType::DeepL, TranslationEngineType::Baidu,
            TranslationEngineType::Custom};
}

const char* translationEngineRaw(TranslationEngineType t) {
    switch (t) {
        case TranslationEngineType::MtranServer: return "mtran";
        case TranslationEngineType::OpenAI: return "openai";
        case TranslationEngineType::Claude: return "claude";
        case TranslationEngineType::Gemini: return "gemini";
        case TranslationEngineType::Ollama: return "ollama";
        case TranslationEngineType::Google: return "google";
        case TranslationEngineType::DeepL: return "deepl";
        case TranslationEngineType::Baidu: return "baidu";
        case TranslationEngineType::Custom: return "custom";
    }
    return "custom";
}

std::optional<TranslationEngineType> translationEngineFromRaw(const std::string& raw) {
    for (auto t : allTranslationEngines()) {
        if (raw == translationEngineRaw(t)) return t;
    }
    return std::nullopt;
}

const char* translationEngineDisplayName(TranslationEngineType t) {
    switch (t) {
        case TranslationEngineType::MtranServer: return "MTranServer";
        case TranslationEngineType::OpenAI: return "OpenAI";
        case TranslationEngineType::Claude: return "Claude";
        case TranslationEngineType::Gemini: return "Gemini";
        case TranslationEngineType::Ollama: return "Ollama";
        case TranslationEngineType::Google: return "Google Translate";
        case TranslationEngineType::DeepL: return "DeepL";
        case TranslationEngineType::Baidu: return "Baidu Translate";
        case TranslationEngineType::Custom: return "Custom";
    }
    return "Custom";
}

bool translationEngineRequiresApiKey(TranslationEngineType t) {
    switch (t) {
        case TranslationEngineType::MtranServer:
        case TranslationEngineType::Ollama:
            return false;
        case TranslationEngineType::OpenAI:
        case TranslationEngineType::Claude:
        case TranslationEngineType::Gemini:
        case TranslationEngineType::Google:
        case TranslationEngineType::DeepL:
        case TranslationEngineType::Baidu:
        case TranslationEngineType::Custom:
            return true;
    }
    return true;
}

bool translationEngineRequiresAppId(TranslationEngineType t) {
    return t == TranslationEngineType::Baidu;
}

std::string translationEngineDefaultBaseUrl(TranslationEngineType t) {
    switch (t) {
        case TranslationEngineType::OpenAI: return "https://api.openai.com/v1";
        case TranslationEngineType::Claude: return "https://api.anthropic.com/v1";
        case TranslationEngineType::Gemini: return "https://generativelanguage.googleapis.com/v1beta";
        case TranslationEngineType::Ollama: return "http://localhost:11434";
        case TranslationEngineType::Google: return "https://translation.googleapis.com/language/translate/v2";
        case TranslationEngineType::DeepL: return "https://api.deepl.com/v2/translate";
        case TranslationEngineType::Baidu: return "https://fanyi-api.baidu.com/api/trans/vip/translate";
        case TranslationEngineType::MtranServer: return "http://localhost:8989";
        case TranslationEngineType::Custom: return "http://localhost:8000/v1";
    }
    return "";
}

std::string translationEngineDefaultModelName(TranslationEngineType t) {
    switch (t) {
        case TranslationEngineType::OpenAI: return "gpt-4o-mini";
        case TranslationEngineType::Claude: return "claude-sonnet-4-20250514";
        case TranslationEngineType::Gemini: return "gemini-2.0-flash";
        case TranslationEngineType::Ollama: return "llama3";
        default: return "";
    }
}

// ---------------------------------------------------------------------------
// EngineChainItem
// ---------------------------------------------------------------------------
EngineChainItem EngineChainItem::makeBuiltin(TranslationEngineType t) {
    EngineChainItem item;
    item.kind = Kind::Builtin;
    item.builtin = t;
    item.id = Uuid::random().toString();
    return item;
}

EngineChainItem EngineChainItem::makeCompatible(const std::string& uuid) {
    EngineChainItem item;
    item.kind = Kind::Compatible;
    item.compatibleId = uuid;
    item.id = Uuid::random().toString();
    return item;
}

TranslationEngineType EngineChainItem::resolvedEngine() const {
    return kind == Kind::Builtin ? builtin : TranslationEngineType::Custom;
}

const char* translationSceneRaw(TranslationScene s) {
    switch (s) {
        case TranslationScene::Screenshot: return "screenshot";
        case TranslationScene::TextSelection: return "text_selection";
        case TranslationScene::TranslateAndInsert: return "translate_and_insert";
    }
    return "screenshot";
}

std::optional<TranslationScene> translationSceneFromRaw(const std::string& raw) {
    if (raw == "screenshot") return TranslationScene::Screenshot;
    if (raw == "text_selection") return TranslationScene::TextSelection;
    if (raw == "translate_and_insert") return TranslationScene::TranslateAndInsert;
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// TranslationPromptConfig (Swift promptPreview layering).
// ---------------------------------------------------------------------------
const std::string& TranslationPromptConfig::defaultPrompt() {
    static const std::string p =
        "Translate the following text from {source_language} to {target_language}.\n"
        "Provide only the translation without any explanations or additional text.\n"
        "\n"
        "Text to translate:\n"
        "{text}";
    return p;
}

const std::string& TranslationPromptConfig::defaultInsertPrompt() {
    static const std::string p =
        "Translate the following text from {source_language} to {target_language}.\n"
        "The translation will be inserted at the cursor position.\n"
        "Provide only the translation without any explanations, formatting, or additional text.\n"
        "Keep the translation concise and natural for the target language.\n"
        "\n"
        "Text to translate:\n"
        "{text}";
    return p;
}

std::string TranslationPromptConfig::resolve(TranslationEngineType engine,
                                             TranslationScene scene,
                                             const std::string& compatiblePromptId) const {
    // Layer order (Swift promptPreview): scene > compatible > engine >
    // insert-scene default > global default.
    for (const auto& [s, p] : scenePrompts) {
        if (s == scene && !p.empty()) return p;
    }
    if (engine == TranslationEngineType::Custom && !compatiblePromptId.empty()) {
        for (const auto& [id, p] : compatibleEnginePrompts) {
            if (id == compatiblePromptId && !p.empty()) return p;
        }
    }
    for (const auto& [e, p] : enginePrompts) {
        if (e == engine && !p.empty()) return p;
    }
    if (scene == TranslationScene::TranslateAndInsert) return defaultInsertPrompt();
    return defaultPrompt();
}

std::string TranslationPromptConfig::substitute(const std::string& templateText,
                                                const std::string& sourceLanguage,
                                                const std::string& targetLanguage,
                                                const std::string& text) {
    std::string out = templateText;
    auto replaceAll = [&out](const std::string& needle, const std::string& value) {
        std::size_t pos = 0;
        while ((pos = out.find(needle, pos)) != std::string::npos) {
            out.replace(pos, needle.size(), value);
            pos += value.size();
        }
    };
    replaceAll("{source_language}", sourceLanguage);
    replaceAll("{target_language}", targetLanguage);
    replaceAll("{text}", text);
    return out;
}

// ---------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------
TranslationResult::TranslationResult(std::string src, std::string translated,
                                     std::string srcLang, std::string tgtLang)
    : sourceText(std::move(src)),
      translatedText(std::move(translated)),
      sourceLanguage(std::move(srcLang)),
      targetLanguage(std::move(tgtLang)) {}

BilingualSegment BilingualSegment::fromResult(const TranslationResult& r) {
    BilingualSegment b;
    b.id = Uuid::random().toString();
    TextSegment seg(r.sourceText, NormRect{}, 1.0f, false);
    b.original = std::move(seg);
    b.translated = r.translatedText;
    b.sourceLanguage = r.sourceLanguage;
    b.targetLanguage = r.targetLanguage;
    return b;
}

EngineResult EngineResult::makeFailure(TranslationEngineType engine, std::string errorText,
                                       double latency, std::string displayName) {
    EngineResult r;
    r.engine = engine;
    r.failed = true;
    r.errorText = std::move(errorText);
    r.latencySeconds = latency;
    r.displayName = std::move(displayName);
    return r;
}

std::vector<TranslationEngineType> TranslationResultBundle::successfulEngines() const {
    std::vector<TranslationEngineType> out;
    for (const auto& r : results) {
        if (r.isSuccess()) out.push_back(r.engine);
    }
    return out;
}

// ---------------------------------------------------------------------------
// VLM / OCR selectors + error categories
// ---------------------------------------------------------------------------
const char* vlmProviderRaw(VlmProviderType t) {
    switch (t) {
        case VlmProviderType::OpenAI: return "openai";
        case VlmProviderType::Claude: return "claude";
        case VlmProviderType::Gemini: return "gemini";
        case VlmProviderType::Ollama: return "ollama";
        case VlmProviderType::PaddleOCR: return "paddleocr";
    }
    return "openai";
}

std::optional<VlmProviderType> vlmProviderFromRaw(const std::string& raw) {
    if (raw == "openai") return VlmProviderType::OpenAI;
    if (raw == "claude") return VlmProviderType::Claude;
    if (raw == "gemini") return VlmProviderType::Gemini;
    if (raw == "ollama") return VlmProviderType::Ollama;
    if (raw == "paddleocr") return VlmProviderType::PaddleOCR;
    return std::nullopt;
}

const char* ocrEngineRaw(OcrEngineType t) {
    return t == OcrEngineType::WindowsOcr ? "windowsOcr" : "paddleocr";
}

const char* errorCategoryName(ErrorCategory c) {
    switch (c) {
        case ErrorCategory::Network: return "network";
        case ErrorCategory::Quota: return "quota";
        case ErrorCategory::Auth: return "auth";
        case ErrorCategory::Content: return "content";
        case ErrorCategory::Configuration: return "configuration";
    }
    return "network";
}

}  // namespace tf
