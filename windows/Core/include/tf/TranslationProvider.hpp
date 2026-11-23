// tf/TranslationProvider.hpp
// Unified translation-provider contract for the 9 Windows engines (no apple).
// Mirrors Swift TranslationProvider + TranslationPromptConfigurable +
// TranslationPromptContextProviding (Services/TranslationProvider.swift).
// Pure logic + IHttpTransport only — no UI, no Win32; Catch2 drives it with
// MockHttpTransport directly (brief: HTTP 层不依赖 UI 框架).
#ifndef TF_TRANSLATION_PROVIDER_HPP
#define TF_TRANSLATION_PROVIDER_HPP

#include "tf/CoreModels.hpp"
#include "tf/HttpTransport.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf {

/// Stored credential payload (Swift StoredCredentials): apiKey + optional
/// appID (Baidu) + free-form additional fields.
struct StoredCredentials {
    std::string apiKey;
    std::string appID;
    std::map<std::string, std::string> additional;
};

/// Credential storage abstraction. Production: Credential Manager
/// (NativeShell/src/CredManKeyStore.cpp, "TransFrame/<engine>/<instance>").
/// Tests: in-memory mock (brief Catch2 "KeyStore mock").
class ICredentialStore {
public:
    virtual ~ICredentialStore() = default;
    /// Saves (or replaces) the credential blob for one engine / instance.
    virtual void save(const std::string& account, const StoredCredentials& credentials) = 0;
    /// Reads; nullopt when absent.
    virtual std::optional<StoredCredentials> load(const std::string& account) const = 0;
    /// Removes; returns true when an item existed.
    virtual bool remove(const std::string& account) = 0;
    /// Fast presence check (drives health gating).
    virtual bool has(const std::string& account) const = 0;
};

using CredentialStorePtr = std::shared_ptr<ICredentialStore>;

/// Account naming: "TransFrame/<engine>/<instance>" (brief §5).
/// Builtin engines use the raw engine string; compatible instances use their
/// UUID. Baidu keeps its appID inside the same blob.
std::string credentialAccount(TranslationEngineType engine);
std::string credentialAccountForCompatible(const std::string& instanceUuid);

/// Masked display form for UI: "sk-****last4" (brief §5). Keys shorter than
/// 8 chars collapse to "****".
std::string maskApiKey(const std::string& apiKey);

/// Per-engine configuration (Swift TranslationEngineConfig + EngineOptions).
struct EngineConfig {
    TranslationEngineType engine = TranslationEngineType::MtranServer;
    bool isEnabled = false;
    std::string baseURL;                            // overrides default when non-empty
    std::string modelName;                          // LLM engines
    std::optional<double> timeoutSeconds;           // default 10s (brief)
    std::optional<std::int64_t> maxTokens;          // LLM batching budget
    std::optional<double> temperature;
    std::map<std::string, std::string> customHeaders;

    double effectiveTimeout() const { return timeoutSeconds.value_or(10.0); }
    static EngineConfig make(TranslationEngineType t);

    /// Slice identity for registry caching (Swift matchesCachedProvider).
    bool operator==(const EngineConfig& o) const = default;
};

/// OpenAI-compatible instance definition (Swift CompatibleTranslationProvider
/// .CompatibleConfig): UUID identity, display name, endpoint, model, keyless
/// local endpoints allowed.
struct CompatibleConfig {
    std::string id;            // UUID string
    std::string displayName;
    std::string baseURL;
    std::string modelName;
    bool hasAPIKey = true;     // false → keyless local endpoint allowed

    static CompatibleConfig makeDefault();  // "Custom", http://localhost:8000/v1

    bool operator==(const CompatibleConfig& o) const = default;
};

/// Thrown-equivalent: providers return ResultOrProviderError.
struct TranslationOutcome {
    std::optional<std::vector<TranslationResult>> results;
    std::optional<ProviderError> error;

    static TranslationOutcome ok(std::vector<TranslationResult> r) {
        return TranslationOutcome{std::move(r), std::nullopt};
    }
    static TranslationOutcome fail(ProviderError e) {
        return TranslationOutcome{std::nullopt, std::move(e)};
    }
};

/// Batch request context passed to providers.
struct TranslateRequest {
    std::vector<std::string> texts;
    std::string sourceLanguage;   // "" = auto-detect
    std::string targetLanguage;
    std::string promptTemplate;   // resolved per-engine/scene template ("" → default)
    CancellationToken cancelToken;
};

/// Unified provider interface. Every engine implements this; the scheduler
/// (TranslationScheduler) only sees this interface (brief §2).
class ITranslationProvider {
public:
    virtual ~ITranslationProvider() = default;

    virtual std::string id() const = 0;
    virtual std::string name() const = 0;
    virtual TranslationEngineType engineType() const = 0;

    /// Configured + credentials present (Swift isAvailable). Never performs I/O.
    virtual bool isAvailable() const = 0;

    /// Batch translate; preserves input order. Providers with batching
    /// (LLM family) split internally via LLMTranslationBatching.
    virtual TranslationOutcome translate(const TranslateRequest& request) = 0;

    /// Lightweight free-endpoint connectivity probe (models list / ping).
    /// MUST NOT call billed translation endpoints (PRD FR-22).
    /// Returns nullopt when no free probe exists for this engine.
    virtual std::optional<ProviderError> verifyConnection(const CancellationToken& token) = 0;
};

using TranslationProviderPtr = std::unique_ptr<ITranslationProvider>;

}  // namespace tf

#endif  // TF_TRANSLATION_PROVIDER_HPP
