// tf/CompatibleChatProvider.hpp
// OpenAI-compatible custom endpoint with UUID instance identity (port of
// Swift CompatibleTranslationProvider.swift): per-instance credential slot
// "custom:<uuid>", keyless local endpoints, models-list free probe.
#ifndef TF_COMPATIBLE_CHAT_PROVIDER_HPP
#define TF_COMPATIBLE_CHAT_PROVIDER_HPP

#include "tf/LLMTranslationCore.hpp"  // Outcome<T, E>
#include "tf/TranslationProvider.hpp"

namespace tf {

class CompatibleChatProvider final : public ITranslationProvider {
public:
    CompatibleChatProvider(CompatibleConfig compatibleConfig, EngineConfig engineConfig,
                           CredentialStorePtr credentials, HttpTransportPtr transport);

    std::string id() const override { return compatible_.id; }  // instance UUID
    std::string name() const override { return compatible_.displayName; }
    TranslationEngineType engineType() const override { return TranslationEngineType::Custom; }
    bool isAvailable() const override;
    TranslationOutcome translate(const TranslateRequest& request) override;
    std::optional<ProviderError> verifyConnection(const CancellationToken& token) override;

    /// Prompt-context identifier (Swift compatiblePromptIdentifier): the
    /// instance UUID — prompt layer 2 keys on it.
    std::string compatiblePromptIdentifier() const { return compatible_.id; }
    const CompatibleConfig& compatibleConfig() const { return compatible_; }

    /// Endpoint builders (Swift): baseURL + /chat/completions, /models.
    std::string translateEndpoint() const;
    std::string verifyEndpoint() const;

    std::string buildPrompt(const std::string& combinedText, const std::string& sourceLanguage,
                            const std::string& targetLanguage,
                            const std::string& promptTemplate) const;

    /// Resolves the effective instance for the legacy "custom" slot: prefer a
    /// serialized CompatibleConfig in engineConfig.customName, else the first
    /// saved instance, else the default (Swift resolveConfigurationSnapshot).
    static CompatibleConfig resolveConfigurationSnapshot(
        const CompatibleConfig& requested, const std::vector<CompatibleConfig>& savedConfigs);

private:
    Outcome<std::string, ProviderError> callApi(const std::string& prompt,
                                                const StoredCredentials* credentials,
                                                const CancellationToken& token);

    CompatibleConfig compatible_;
    EngineConfig engineConfig_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
};

}  // namespace tf

#endif  // TF_COMPATIBLE_CHAT_PROVIDER_HPP
