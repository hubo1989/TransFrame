// tf/TranslationScheduler.hpp
// V2 engine selection: registry + ordered fallback chain (≤5) + compare
// parallel (2-4) + per-run result cache + scene binding routing.
// Port of Swift TranslationEngineRegistry.swift + TranslationService.swift
// (translateOrderedV2 / translateCompareV2) minus apple fallbacks.
// Pure logic: providers are injected; Catch2 tests drive mock providers with
// injected latency/failure (brief §7).
#ifndef TF_TRANSLATION_SCHEDULER_HPP
#define TF_TRANSLATION_SCHEDULER_HPP

#include "tf/CoreModels.hpp"
#include "tf/TranslationProvider.hpp"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tf {

/// Provider factory + saved compatible-instance list (the registry's external
/// seams so tests inject mocks without touching real transports).
struct SchedulerDependencies {
    /// Returns the provider for a builtin engine, or nullptr when unconfigured.
    std::function<TranslationProviderPtr(TranslationEngineType)> builtinProvider;
    /// Returns the provider for a compatible instance UUID, or nullptr when
    /// the instance no longer exists (deleted instance).
    std::function<TranslationProviderPtr(const std::string&)> compatibleProvider;
    /// Saved compatible-instance configs (displayName lookup for results).
    std::function<std::vector<CompatibleConfig>()> compatibleConfigs;
    /// Prompt config resolution: template ("" → default) for engine+scene.
    std::function<std::string(TranslationEngineType, TranslationScene, const std::string&)>
        promptResolver;
};

/// Aggregated failure for a whole-chain / whole-set loss (Swift
/// MultiEngineError.allEnginesFailed): per-engine identity + reason.
struct ChainFailureEntry {
    TranslationEngineType engine;
    std::string displayName;  // instance name when applicable
    std::string errorText;
};

/// Scheduler run outcome. Either a bundle (≥1 engine success) or the
/// aggregated per-engine failure list (Swift semantics: failures precede the
/// winning result in `results`).
struct SchedulerOutcome {
    std::optional<TranslationResultBundle> bundle;
    std::vector<ChainFailureEntry> failures;  // set when bundle empty

    bool isSuccess() const { return bundle.has_value(); }
};

/// Ordered chain request (≤5 entries, clamped; brief FR-20).
struct ChainRequest {
    std::vector<EngineChainItem> chain;
    std::vector<std::string> texts;
    std::string sourceLanguage;
    std::string targetLanguage;
    TranslationScene scene = TranslationScene::Screenshot;
    CancellationToken cancelToken;
};

/// Compare request (2-4 entries, clamped; brief FR-21).
struct CompareRequest {
    std::vector<EngineChainItem> engines;
    std::vector<std::string> texts;
    std::string sourceLanguage;
    std::string targetLanguage;
    TranslationScene scene = TranslationScene::Screenshot;
    CancellationToken cancelToken;
};

/// Scene → selection routing (brief FR-23): unbound scenes fall back to the
/// global chain/compare set.
struct SceneEngineBinding {
    TranslationScene scene = TranslationScene::Screenshot;
    std::vector<EngineChainItem> chain;       // when ordered
    std::vector<EngineChainItem> compareSet;  // when compare
};

class TranslationScheduler {
public:
    explicit TranslationScheduler(SchedulerDependencies deps);

    /// Ordered fallback: try entries in order; first success wins and earlier
    /// failures ride along in results. Unavailable members are skipped with a
    /// logged failure (Swift translateOrderedV2). Cancellation aborts the
    /// chain immediately instead of grinding through (Swift PR #92).
    SchedulerOutcome translateOrdered(const ChainRequest& request);

    /// Compare: run entries in parallel, one EngineResult per entry in
    /// selection order; ≥1 success → bundle; all failed → aggregated
    /// failures (Swift translateCompareV2). The per-entry result cache makes
    /// later "switch engine / re-view" hits free (no re-translation).
    SchedulerOutcome translateCompare(const CompareRequest& request);

    /// Scene routing: picks the bound selection for the scene or falls back
    /// to the global pair (brief FR-23).
    SchedulerOutcome translateForScene(const ChainRequest& globalChain,
                                       const CompareRequest& globalCompare,
                                       const SceneEngineBinding& binding,
                                       const std::vector<std::string>& texts,
                                       const std::string& sourceLanguage,
                                       const std::string& targetLanguage,
                                       const CancellationToken& token);

    // --- per-run compare cache (brief FR-21 "结果缓存免重译") ---
    /// Cache key: engine identity + source/target + joined texts. One entry
    /// per successful engine result.
    struct CacheKey {
        std::string engineKey;        // builtin raw or "custom:<uuid>"
        std::string sourceLanguage;
        std::string targetLanguage;
        std::string textsHash;

        bool operator<(const CacheKey& o) const {
            if (engineKey != o.engineKey) return engineKey < o.engineKey;
            if (sourceLanguage != o.sourceLanguage) return sourceLanguage < o.sourceLanguage;
            if (targetLanguage != o.targetLanguage) return targetLanguage < o.targetLanguage;
            return textsHash < o.textsHash;
        }
    };

    std::optional<EngineResult> cachedResult(const EngineChainItem& item,
                                             const std::string& sourceLanguage,
                                             const std::string& targetLanguage,
                                             const std::vector<std::string>& texts) const;
    void storeResult(const EngineChainItem& item, const std::string& sourceLanguage,
                     const std::string& targetLanguage, const std::vector<std::string>& texts,
                     const EngineResult& result);
    void clearResultCache();

private:
    struct ResolveResult {
        TranslationProviderPtr provider;
        std::string displayName;  // instance display name or ""
        bool resolved = false;
    };
    ResolveResult resolve(const EngineChainItem& item) const;
    static std::string engineKeyFor(const EngineChainItem& item);
    static std::string hashTexts(const std::vector<std::string>& texts);
    EngineResult runProvider(const ResolveResult& resolved, const EngineChainItem& item,
                             const std::vector<std::string>& texts,
                             const std::string& sourceLanguage,
                             const std::string& targetLanguage, TranslationScene scene,
                             const CancellationToken& token);

    SchedulerDependencies deps_;
    mutable std::mutex cacheMutex_;
    std::map<CacheKey, EngineResult> resultCache_;
};

/// EngineRegistry: creation + caching of the 9 builtin providers and
/// compatible instances (Swift TranslationEngineRegistry, ported shape).
/// Config-slice equality decides cache hits; manual overrides win.
class EngineRegistry {
public:
    using BuiltinFactory = std::function<TranslationProviderPtr(
        TranslationEngineType, const EngineConfig&)>;
    using CompatibleFactory = std::function<TranslationProviderPtr(
        const CompatibleConfig&, const EngineConfig&)>;

    EngineRegistry(BuiltinFactory builtinFactory, CompatibleFactory compatibleFactory,
                   CredentialStorePtr credentials, HttpTransportPtr transport);

    /// Provider for a builtin engine (cached by config slice).
    TranslationProviderPtr provider(TranslationEngineType engine, const EngineConfig& config);
    /// Provider for a compatible instance (cached by UUID+config).
    TranslationProviderPtr compatibleProvider(const CompatibleConfig& config);
    /// Saved instance list seam (settings layer owns persistence).
    std::function<std::vector<CompatibleConfig>()> compatibleConfigsProvider;

    void clearCompatibleProviders();

private:
    BuiltinFactory builtinFactory_;
    CompatibleFactory compatibleFactory_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
    std::mutex mutex_;
    std::map<TranslationEngineType, std::pair<EngineConfig, TranslationProviderPtr>> builtin_;
    std::map<std::string, std::pair<CompatibleConfig, TranslationProviderPtr>> compatible_;
};

/// Default 9-engine factory wiring used by the app (tests inject their own).
/// EngineConfig comes from the settings layer; the factory only shapes
/// transport + credential access.
TranslationProviderPtr makeBuiltinProvider(TranslationEngineType engine,
                                           const EngineConfig& config,
                                           const CredentialStorePtr& credentials,
                                           const HttpTransportPtr& transport);

}  // namespace tf

#endif  // TF_TRANSLATION_SCHEDULER_HPP
