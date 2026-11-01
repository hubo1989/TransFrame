// CompatibleChatProvider.cpp — OpenAI-compatible multi-instance provider.
#include "tf/CompatibleChatProvider.hpp"
#include "tf/Json.hpp"
#include "tf/LLMChatProvider.hpp"
#include "tf/LLMTranslationCore.hpp"

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

}  // namespace

CompatibleConfig CompatibleConfig::makeDefault() {
    CompatibleConfig c;
    c.id = Uuid::nil().toString();  // Swift default: fixed nil-UUID "Custom"
    c.displayName = "Custom";
    c.baseURL = "http://localhost:8000/v1";
    c.modelName = "custom-model";
    c.hasAPIKey = true;
    return c;
}

CompatibleChatProvider::CompatibleChatProvider(CompatibleConfig compatibleConfig,
                                               EngineConfig engineConfig,
                                               CredentialStorePtr credentials,
                                               HttpTransportPtr transport)
    : compatible_(std::move(compatibleConfig)),
      engineConfig_(std::move(engineConfig)),
      credentials_(std::move(credentials)),
      transport_(std::move(transport)) {}

bool CompatibleChatProvider::isAvailable() const {
    // Keyless local endpoints stay available without a stored credential
    // (Swift: localhost preset may run keyless).
    if (!compatible_.hasAPIKey) return true;
    if (auto parsed = parseUrl(compatible_.baseURL)) {
        if (isLocalhostHost(parsed->host)) return true;
    }
    return credentials_ && credentials_->has(credentialAccountForCompatible(compatible_.id));
}

std::string CompatibleChatProvider::translateEndpoint() const {
    return appendingApiSuffix(resolveLocalhost(compatible_.baseURL), "chat/completions");
}

std::string CompatibleChatProvider::verifyEndpoint() const {
    return appendingApiSuffix(resolveLocalhost(compatible_.baseURL), "models");
}

std::string CompatibleChatProvider::buildPrompt(const std::string& combinedText,
                                                const std::string& sourceLanguage,
                                                const std::string& targetLanguage,
                                                const std::string& promptTemplate) const {
    const std::string source = promptLanguageDisplayName(sourceLanguage);
    const std::string target = promptLanguageDisplayName(targetLanguage);
    if (!promptTemplate.empty()) {
        return TranslationPromptConfig::substitute(promptTemplate, source, target, combinedText);
    }
    return "Translate the following text from " + source + " to " + target + ".\n"
           "Provide ONLY the translated text without any explanations or additional text.\n"
           "\nText to translate:\n" +
           combinedText;
}

CompatibleConfig CompatibleChatProvider::resolveConfigurationSnapshot(
    const CompatibleConfig& requested, const std::vector<CompatibleConfig>& savedConfigs) {
    // Dangling UUID (instance deleted) → first saved instance → default.
    for (const auto& saved : savedConfigs) {
        if (saved.id == requested.id) return saved;
    }
    if (!savedConfigs.empty()) return savedConfigs.front();
    return requested;
}

TranslationOutcome CompatibleChatProvider::translate(const TranslateRequest& request) {
    if (request.texts.empty()) return TranslationOutcome::ok({});

    std::optional<StoredCredentials> creds;
    if (compatible_.hasAPIKey) {
        creds = credentials_->load(credentialAccountForCompatible(compatible_.id));
    }

    const std::int64_t maxTokens = engineConfig_.maxTokens.value_or(2048);
    const auto batches =
        LLMTranslationBatching::split(request.texts, maxTokens, request.promptTemplate);

    std::vector<TranslationResult> all;
    for (const auto& batch : batches) {
        std::string combined;
        const std::string sep(LLMTranslationBatching::separator());
        for (std::size_t i = 0; i < batch.size(); ++i) {
            if (i) combined += sep;
            combined += batch[i];
        }
        const std::string prompt =
            buildPrompt(combined, request.sourceLanguage, request.targetLanguage,
                        request.promptTemplate);
        auto textOutcome = callApi(prompt, creds ? &*creds : nullptr, request.cancelToken);
        if (!textOutcome.isOk()) return TranslationOutcome::fail(textOutcome.error);
        const std::string& translated = textOutcome.value;

        std::vector<std::string> parts;
        {
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
        } else {
            // Single text (or split failure): the combined text maps to the
            // whole response (Swift translateSingleBatch fallback).
            all.emplace_back(combined, translated,
                             request.sourceLanguage.empty() ? "Auto" : request.sourceLanguage,
                             request.targetLanguage);
        }
    }
    return TranslationOutcome::ok(std::move(all));
}

std::optional<ProviderError> CompatibleChatProvider::verifyConnection(
    const CancellationToken& token) {
    std::optional<StoredCredentials> creds;
    if (compatible_.hasAPIKey) {
        creds = credentials_->load(credentialAccountForCompatible(compatible_.id));
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
    if (outcome.error.has_value()) return outcome.error;
    if (outcome.response->ok()) return std::nullopt;
    return ProviderError::network("Connection verify failed: HTTP " +
                                  std::to_string(outcome.response->status));
}

Outcome<std::string, ProviderError> CompatibleChatProvider::callApi(
    const std::string& prompt, const StoredCredentials* credentials,
    const CancellationToken& token) {
    HttpRequest req;
    req.method = "POST";
    req.url = translateEndpoint();
    req.timeoutSeconds = static_cast<int>(engineConfig_.effectiveTimeout());
    req.cancelToken = token;
    req.headers["Content-Type"] = "application/json";
    if (credentials && !credentials->apiKey.empty()) {
        if (auto guard = guardApiKeyTransport(req.url, true)) {
            return Outcome<std::string, ProviderError>::makeError(*guard);
        }
        req.headers["Authorization"] = "Bearer " + credentials->apiKey;
    }
    for (const auto& [k, v] : engineConfig_.customHeaders) req.headers[k] = v;

    Json body;
    body["model"] = compatible_.modelName;
    body["messages"] = Json::array({Json{{"role", "user"}, {"content", prompt}}});
    body["stream"] = false;
    body["temperature"] = engineConfig_.temperature.value_or(0.3);
    body["max_tokens"] = engineConfig_.maxTokens.value_or(2048);
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
        return Outcome<std::string, ProviderError>::makeError(ProviderError::quota("Rate limited"));
    }
    if (resp.status != 200) {
        return Outcome<std::string, ProviderError>::makeError(
            ProviderError::network("API error: HTTP " + std::to_string(resp.status)));
    }
    // Custom endpoints are OpenAI-compatible (Swift parses with engine .custom).
    return LLMTranslationResponseParser::extractText(resp.body, TranslationEngineType::Custom);
}

}  // namespace tf
