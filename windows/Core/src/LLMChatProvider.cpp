// LLMChatProvider.cpp — OpenAI / Claude / Gemini / Ollama chat-completions
// provider (port of Swift LLMTranslationProvider.swift).
#include "tf/LLMChatProvider.hpp"
#include "tf/Json.hpp"

#include <algorithm>

namespace tf {

namespace {

std::string trimCopy(const std::string& s) {
    std::size_t b = 0, e = s.size();
    auto isSpace = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
    };
    while (b < e && isSpace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && isSpace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

/// English language display names for the prompt (Swift TranslationLanguage
/// displayName with locale en, normalized "Primary (Region)" form).
std::string languageName(const std::string& code) {
    static const std::vector<std::pair<std::string, std::string>> kNames = {
        {"en", "English"}, {"zh-Hans", "Chinese (Simplified)"}, {"zh-Hant", "Chinese (Traditional)"},
        {"ja", "Japanese"}, {"ko", "Korean"}, {"fr", "French"}, {"de", "German"},
        {"es", "Spanish"}, {"it", "Italian"}, {"pt", "Portuguese"}, {"ru", "Russian"},
        {"ar", "Arabic"}, {"hi", "Hindi"}, {"th", "Thai"}, {"vi", "Vietnamese"},
        {"nl", "Dutch"}, {"pl", "Polish"}, {"tr", "Turkish"}, {"uk", "Ukrainian"},
        {"cs", "Czech"}, {"sv", "Swedish"}, {"da", "Danish"}, {"fi", "Finnish"},
        {"no", "Norwegian"}, {"el", "Greek"}, {"he", "Hebrew"}, {"id", "Indonesian"},
        {"ms", "Malay"}, {"ro", "Romanian"}, {"zh", "Chinese (Simplified)"},
    };
    if (code.empty() || code == "auto" || code == "Auto") return "Auto Detect";
    for (const auto& [k, v] : kNames) {
        if (code == k) return v;
    }
    // Base-code fallback ("en-US" → "English").
    std::size_t dash = code.find('-');
    if (dash != std::string::npos && dash > 0) {
        const std::string base = code.substr(0, dash);
        for (const auto& [k, v] : kNames) {
            if (base == k) return v;
        }
    }
    return code;
}

}  // namespace

std::string promptLanguageDisplayName(const std::string& code) { return languageName(code); }

LLMChatProvider::LLMChatProvider(TranslationEngineType engine, EngineConfig config,
                                 CredentialStorePtr credentials, HttpTransportPtr transport)
    : engine_(engine), config_(std::move(config)),
      credentials_(std::move(credentials)), transport_(std::move(transport)) {}

std::string LLMChatProvider::name() const {
    switch (engine_) {
        case TranslationEngineType::OpenAI: return "OpenAI Translation";
        case TranslationEngineType::Claude: return "Claude Translation";
        case TranslationEngineType::Gemini: return "Gemini Translation";
        case TranslationEngineType::Ollama: return "Ollama Translation";
        default: return "LLM Translation";
    }
}

bool LLMChatProvider::isAvailable() const {
    // Ollama needs no key (Swift).
    if (engine_ == TranslationEngineType::Ollama) return true;
    // Local OpenAI-compatible endpoints (LM Studio / vLLM presets on
    // localhost) may run keyless — a credential miss must not hard-disable
    // them (Swift PR #92 behavior).
    if (engine_ == TranslationEngineType::OpenAI && !config_.baseURL.empty()) {
        if (auto parsed = parseUrl(config_.baseURL)) {
            if (isLocalhostHost(parsed->host)) return true;
        }
    }
    return credentials_ && credentials_->has(credentialAccount(engine_));
}

std::string LLMChatProvider::endpointForRequest() const {
    std::string base = config_.baseURL.empty() ? translationEngineDefaultBaseUrl(engine_)
                                               : config_.baseURL;
    base = resolveLocalhost(base);
    switch (engine_) {
        case TranslationEngineType::Claude:
            return appendingApiSuffix(base, "v1/messages");
        case TranslationEngineType::Gemini:
            return appendingApiSuffix(base, "v1beta/openai/chat/completions");
        case TranslationEngineType::Ollama:
            return appendingApiSuffix(base, "v1/chat/completions");
        default:  // openai — base typically ends with /v1
            return appendingApiSuffix(base, "chat/completions");
    }
}

std::string LLMChatProvider::verifyEndpoint() const {
    const std::string base = resolveLocalhost(
        config_.baseURL.empty() ? translationEngineDefaultBaseUrl(engine_) : config_.baseURL);
    // Same path family as translation so "test passes but translate 404"
    // (or vice versa) cannot diverge (Swift PR comment).
    return appendingApiSuffix(base, engine_ == TranslationEngineType::Ollama ? "v1/models"
                                                                             : "models");
}

std::string LLMChatProvider::buildPrompt(const std::string& combinedText,
                                         const std::string& sourceLanguage,
                                         const std::string& targetLanguage,
                                         const std::string& promptTemplate) const {
    const std::string source = languageName(sourceLanguage);
    const std::string target = languageName(targetLanguage);

    if (!promptTemplate.empty()) {
        return TranslationPromptConfig::substitute(promptTemplate, source, target, combinedText);
    }
    // Default prompt with separator-preservation hint when batching markers
    // are present (Swift buildPrompt).
    const std::string sep(LLMTranslationBatching::separator());
    const bool hasSeparator = combinedText.find(sep) != std::string::npos;
    std::string hint =
        hasSeparator
            ? "Preserve the literal '---' separators exactly where they appear in the input. Do "
              "not add or remove any separators."
            : "";
    return "Translate the following text from " + source + " to " + target + ".\n"
           "Provide ONLY the translated text without any explanations, notes, or formatting.\n" +
           hint + "\n\nText to translate:\n" + combinedText;
}

TranslationOutcome LLMChatProvider::translate(const TranslateRequest& request) {
    if (request.texts.empty()) return TranslationOutcome::ok({});

    std::optional<StoredCredentials> creds;
    if (translationEngineRequiresApiKey(engine_) && engine_ != TranslationEngineType::Ollama) {
        creds = credentials_->load(credentialAccount(engine_));
        if (!creds.has_value()) {
            return TranslationOutcome::fail(ProviderError::configuration(
                "API key required for " + std::string(translationEngineRaw(engine_))));
        }
    }

    const std::int64_t maxTokens = config_.maxTokens.value_or(2048);
    const auto batches = LLMTranslationBatching::split(request.texts, maxTokens,
                                                       request.promptTemplate);

    std::vector<TranslationResult> all;
    for (const auto& batch : batches) {
        const std::string combined = [&] {
            std::string out;
            const std::string sep(LLMTranslationBatching::separator());
            for (std::size_t i = 0; i < batch.size(); ++i) {
                if (i) out += sep;
                out += batch[i];
            }
            return out;
        }();
        const std::string prompt =
            buildPrompt(combined, request.sourceLanguage, request.targetLanguage,
                        request.promptTemplate);
        auto textOutcome = callApi(prompt, creds ? &*creds : nullptr, request.cancelToken);
        if (!textOutcome.isOk()) {
            return TranslationOutcome::fail(textOutcome.error);
        }
        const std::string translated = textOutcome.value;

        // Split by separator; on count match → zip; single text → whole;
        // mismatch → per-text fallback (Swift translateSingleBatch).
        std::vector<std::string> parts;
        {
            const std::string sep(LLMTranslationBatching::separator());
            std::size_t start = 0;
            while (true) {
                std::size_t idx = translated.find(sep, start);
                if (idx == std::string::npos) {
                    parts.push_back(translated.substr(start));
                    break;
                }
                parts.push_back(translated.substr(start, idx - start));
                start = idx + sep.size();
            }
        }
        if (parts.size() == batch.size()) {
            for (std::size_t i = 0; i < batch.size(); ++i) {
                all.emplace_back(batch[i], trimCopy(parts[i]),
                                 request.sourceLanguage.empty() ? "Auto" : request.sourceLanguage,
                                 request.targetLanguage);
            }
        } else if (batch.size() == 1) {
            all.emplace_back(combined, translated,
                             request.sourceLanguage.empty() ? "Auto" : request.sourceLanguage,
                             request.targetLanguage);
        } else {
            // Batch split failed → retranslate texts individually.
            for (const auto& text : batch) {
                TranslateRequest single = request;
                single.texts = {text};
                auto one = translate(single);
                if (!one.results.has_value()) return one;
                for (auto& r : *one.results) all.push_back(std::move(r));
            }
        }
    }
    return TranslationOutcome::ok(std::move(all));
}

std::optional<ProviderError> LLMChatProvider::verifyConnection(const CancellationToken& token) {
    // OpenAI/Ollama: free models-list GET. Claude/Gemini have no free probe —
    // report "no free probe" (nullopt) so the health checker marks them
    // gray (Swift EngineHealthChecker policy).
    if (engine_ != TranslationEngineType::OpenAI && engine_ != TranslationEngineType::Ollama) {
        return std::nullopt;
    }
    std::optional<StoredCredentials> creds;
    if (engine_ == TranslationEngineType::OpenAI) {
        creds = credentials_ ? credentials_->load(credentialAccount(engine_)) : std::nullopt;
    }
    HttpRequest req;
    req.method = "GET";
    req.url = verifyEndpoint();
    req.timeoutSeconds = 10;
    req.cancelToken = token;
    if (creds.has_value() && !creds->apiKey.empty()) {
        if (auto guard = guardApiKeyTransport(req.url, true)) return *guard;
        req.headers["Authorization"] = "Bearer " + creds->apiKey;
    }
    auto outcome = transport_->send(req);
    if (!outcome.error.has_value() && outcome.response->ok()) return std::nullopt;
    if (outcome.error.has_value()) return outcome.error;
    return ProviderError::network("Connection verify failed: HTTP " +
                                  std::to_string(outcome.response->status));
}

Outcome<std::string, ProviderError> LLMChatProvider::callApi(
    const std::string& prompt, const StoredCredentials* credentials,
    const CancellationToken& token) {
    const std::string endpoint = endpointForRequest();
    const std::string model =
        config_.modelName.empty() ? translationEngineDefaultModelName(engine_) : config_.modelName;

    HttpRequest req;
    req.method = "POST";
    req.url = endpoint;
    req.timeoutSeconds = static_cast<int>(config_.effectiveTimeout());
    req.cancelToken = token;
    req.headers["Content-Type"] = "application/json";

    const bool hasKey = credentials && !credentials->apiKey.empty();
    switch (engine_) {
        case TranslationEngineType::Claude:
            if (hasKey) {
                if (auto guard = guardApiKeyTransport(req.url, true)) {
                    return Outcome<std::string, ProviderError>::makeError(*guard);
                }
                req.headers["x-api-key"] = credentials->apiKey;
                req.headers["anthropic-version"] = "2023-06-01";
            }
            break;
        case TranslationEngineType::Gemini:
            if (hasKey) {
                if (auto guard = guardApiKeyTransport(req.url, true)) {
                    return Outcome<std::string, ProviderError>::makeError(*guard);
                }
                req.headers["Authorization"] = "Bearer " + credentials->apiKey;
            }
            break;
        case TranslationEngineType::Ollama:
            break;  // no auth
        default:
            if (hasKey) {
                if (auto guard = guardApiKeyTransport(req.url, true)) {
                    return Outcome<std::string, ProviderError>::makeError(*guard);
                }
                req.headers["Authorization"] = "Bearer " + credentials->apiKey;
            }
            break;
    }
    for (const auto& [k, v] : config_.customHeaders) req.headers[k] = v;

    Json body;
    if (engine_ == TranslationEngineType::Claude) {
        body["model"] = model;
        body["max_tokens"] = config_.maxTokens.value_or(2048);
        body["messages"] = Json::array({Json{{"role", "user"}, {"content", prompt}}});
    } else {
        body["model"] = model;
        body["messages"] = Json::array({Json{{"role", "user"}, {"content", prompt}}});
        body["stream"] = false;
        body["temperature"] = config_.temperature.value_or(0.3);
        body["max_tokens"] = config_.maxTokens.value_or(2048);
    }
    req.body = body.dump();

    auto outcome = transport_->send(req);
    if (outcome.error.has_value()) {
        return Outcome<std::string, ProviderError>::makeError(*outcome.error);
    }
    const HttpResponse& resp = *outcome.response;
    if (resp.status == 401) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::auth("Invalid API key"));
    }
    if (resp.status == 429) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::quota("Rate limited"));
    }
    if (resp.status != 200) {
        return Outcome<std::string, ProviderError>::makeError(ProviderError::network(
            "API error: HTTP " + std::to_string(resp.status)));
    }
    return LLMTranslationResponseParser::extractText(resp.body, engine_);
}

}  // namespace tf
