// tf/LLMChatProvider.hpp
// One chat-completions provider covering OpenAI / Claude / Gemini / Ollama,
// ported from Swift LLMTranslationProvider.swift (endpoint routing, header
// sets, HTTPS-key guard, batching, models-list verify). The Windows engine
// set has no apple; "custom" OpenAI-compatible instances use
// CompatibleChatProvider.hpp instead (they carry their own UUID identity).
#ifndef TF_LLM_CHAT_PROVIDER_HPP
#define TF_LLM_CHAT_PROVIDER_HPP

#include "tf/LLMTranslationCore.hpp"
#include "tf/TranslationProvider.hpp"

namespace tf {

class LLMChatProvider final : public ITranslationProvider {
public:
    LLMChatProvider(TranslationEngineType engine,  // openai|claude|gemini|ollama
                    EngineConfig config, CredentialStorePtr credentials,
                    HttpTransportPtr transport);

    // ITranslationProvider
    std::string id() const override { return translationEngineRaw(engine_); }
    std::string name() const override;
    TranslationEngineType engineType() const override { return engine_; }
    bool isAvailable() const override;
    TranslationOutcome translate(const TranslateRequest& request) override;
    std::optional<ProviderError> verifyConnection(const CancellationToken& token) override;

    // --- exposed for tests ---
    std::string buildPrompt(const std::string& combinedText, const std::string& sourceLanguage,
                            const std::string& targetLanguage,
                            const std::string& promptTemplate) const;
    std::string endpointForRequest() const;
    std::string verifyEndpoint() const;

private:
    Outcome<std::string, ProviderError> callApi(const std::string& prompt,
                                                const StoredCredentials* credentials,
                                                const CancellationToken& token);

    TranslationEngineType engine_;
    EngineConfig config_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
};

/// English display names for prompts ("Auto Detect", "Chinese (Simplified)"…),
/// mirroring Swift TranslationLanguage.promptDisplayName (locale=en).
std::string promptLanguageDisplayName(const std::string& code);

}  // namespace tf

#endif  // TF_LLM_CHAT_PROVIDER_HPP
