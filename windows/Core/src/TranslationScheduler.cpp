// TranslationScheduler.cpp — ordered chain + compare set + cache + scene routing.
#include "tf/TranslationScheduler.hpp"
#include "tf/CompatibleChatProvider.hpp"
#include "tf/CloudProviders.hpp"
#include "tf/LLMChatProvider.hpp"
#include "tf/MTranServerProvider.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <sstream>
#include <thread>

namespace tf {

namespace {

double nowSeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

std::string shortError(const std::optional<ProviderError>& error) {
    if (!error.has_value()) return "";
    std::string text = error->message;
    if (text.size() > 120) text = text.substr(0, 120);
    return std::string(errorCategoryName(error->category)) + ": " + text;
}

}  // namespace

TranslationScheduler::TranslationScheduler(SchedulerDependencies deps)
    : deps_(std::move(deps)) {}

std::string TranslationScheduler::engineKeyFor(const EngineChainItem& item) {
    if (item.kind == EngineChainItem::Kind::Builtin) {
        return translationEngineRaw(item.builtin);
    }
    return "custom:" + item.compatibleId;
}

std::string TranslationScheduler::hashTexts(const std::vector<std::string>& texts) {
    // FNV-1a over the joined texts — collision-safe enough as a cache key
    // discriminant alongside language + engine identity.
    std::size_t h = 1469598103934665603ULL;
    for (const auto& t : texts) {
        for (unsigned char c : t) {
            h ^= c;
            h *= 1099511628211ULL;
        }
        h ^= 0x1F;
        h *= 1099511628211ULL;
    }
    std::ostringstream os;
    os << std::hex << h;
    return os.str();
}

TranslationScheduler::ResolveResult TranslationScheduler::resolve(
    const EngineChainItem& item) const {
    ResolveResult out;
    if (item.kind == EngineChainItem::Kind::Builtin) {
        if (deps_.builtinProvider) {
            out.provider = deps_.builtinProvider(item.builtin);
        }
        out.resolved = out.provider != nullptr;
        return out;
    }
    if (deps_.compatibleProvider) {
        out.provider = deps_.compatibleProvider(item.compatibleId);
    }
    out.resolved = out.provider != nullptr;
    if (out.resolved && deps_.compatibleConfigs) {
        for (const auto& cfg : deps_.compatibleConfigs()) {
            if (cfg.id == item.compatibleId) {
                out.displayName = cfg.displayName;
                break;
            }
        }
    }
    return out;
}

EngineResult TranslationScheduler::runProvider(
    const ResolveResult& resolved, const EngineChainItem& item,
    const std::vector<std::string>& texts, const std::string& sourceLanguage,
    const std::string& targetLanguage, TranslationScene scene, const CancellationToken& token) {
    const double start = nowSeconds();
    TranslateRequest req;
    req.texts = texts;
    req.sourceLanguage = sourceLanguage;
    req.targetLanguage = targetLanguage;
    req.cancelToken = token;
    if (deps_.promptResolver) {
        req.promptTemplate = deps_.promptResolver(item.resolvedEngine(), scene,
                                                  item.kind == EngineChainItem::Kind::Compatible
                                                      ? item.compatibleId
                                                      : std::string());
    }

    auto outcome = resolved.provider->translate(req);
    const double latency = nowSeconds() - start;

    if (outcome.results.has_value()) {
        EngineResult r;
        r.engine = item.resolvedEngine();
        for (const auto& tr : *outcome.results) {
            r.segments.push_back(BilingualSegment::fromResult(tr));
        }
        r.latencySeconds = latency;
        r.displayName = resolved.displayName;
        // Empty result set counts as failure so ordered chains fall through
        // (Swift: "returned no results" guard).
        if (r.segments.empty()) {
            r.failed = true;
            r.errorText = resolved.provider->name() + " returned no results";
        }
        return r;
    }
    EngineResult r = EngineResult::makeFailure(
        item.resolvedEngine(),
        resolved.provider->name() + ": " + shortError(outcome.error), latency,
        resolved.displayName);
    return r;
}

// ---------------------------------------------------------------------------
// Ordered chain (Swift translateOrderedV2).
// ---------------------------------------------------------------------------
SchedulerOutcome TranslationScheduler::translateOrdered(const ChainRequest& request) {
    // Clamp to the structural limit (UI enforces it; service backstop).
    std::vector<EngineChainItem> chain = request.chain;
    if (static_cast<int>(chain.size()) > EngineSelectionLimits::orderedChainLimit) {
        chain.resize(EngineSelectionLimits::orderedChainLimit);
    }
    // Defensive: empty chain still runs (Swift falls back to apple; Windows
    // has no apple — the caller supplies the chain, empty means nothing to
    // try, reported as an aggregated failure listing no engines).
    if (request.texts.empty()) {
        SchedulerOutcome out;
        TranslationResultBundle bundle;
        bundle.primaryEngine =
            chain.empty() ? TranslationEngineType::MtranServer : chain.front().resolvedEngine();
        bundle.selectionMode = EngineSelectionMode::PrimaryWithFallback;
        bundle.scene = static_cast<int>(request.scene);
        out.bundle = bundle;
        return out;
    }

    std::vector<EngineResult> collected;
    for (const auto& item : chain) {
        if (request.cancelToken.cancelled()) {
            SchedulerOutcome out;
            for (const auto& r : collected) {
                out.failures.push_back({r.engine, r.displayName, r.errorText});
            }
            out.failures.push_back({item.resolvedEngine(), "", "cancelled"});
            return out;
        }
        ResolveResult resolved = resolve(item);
        if (!resolved.resolved || !resolved.provider->isAvailable()) {
            // Unavailable / dangling UUID: skip with a logged failure.
            EngineResult skipped = EngineResult::makeFailure(
                item.resolvedEngine(),
                resolved.resolved ? "engine unavailable (not configured)"
                                  : "chain entry unavailable (no provider)",
                0.0, resolved.displayName);
            collected.push_back(std::move(skipped));
            continue;
        }
        EngineResult result = runProvider(resolved, item, request.texts, request.sourceLanguage,
                                          request.targetLanguage, request.scene,
                                          request.cancelToken);
        if (result.isSuccess()) {
            collected.push_back(std::move(result));
            TranslationResultBundle bundle;
            bundle.results = std::move(collected);
            bundle.primaryEngine = item.resolvedEngine();
            bundle.selectionMode = EngineSelectionMode::PrimaryWithFallback;
            bundle.scene = static_cast<int>(request.scene);
            SchedulerOutcome out;
            out.bundle = std::move(bundle);
            return out;
        }
        collected.push_back(std::move(result));
    }
    // Whole chain failed → aggregate per-engine failure summary.
    SchedulerOutcome out;
    for (const auto& r : collected) {
        out.failures.push_back({r.engine, r.displayName, r.errorText});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Compare (Swift translateCompareV2) + result cache.
// ---------------------------------------------------------------------------
SchedulerOutcome TranslationScheduler::translateCompare(const CompareRequest& request) {
    std::vector<EngineChainItem> engines = request.engines;
    if (static_cast<int>(engines.size()) > EngineSelectionLimits::compareEnginesLimit) {
        engines.resize(EngineSelectionLimits::compareEnginesLimit);
    }
    if (request.texts.empty()) {
        SchedulerOutcome out;
        TranslationResultBundle bundle;
        bundle.selectionMode = EngineSelectionMode::Parallel;
        bundle.scene = static_cast<int>(request.scene);
        bundle.primaryEngine =
            engines.empty() ? TranslationEngineType::MtranServer : engines.front().resolvedEngine();
        out.bundle = bundle;
        return out;
    }

    // Parallel fan-out: one worker per entry; results collected by entry id.
    struct WorkerOutput {
        std::string itemId;
        EngineResult result;
    };
    std::vector<WorkerOutput> outputs;
    outputs.reserve(engines.size());
    {
        // Cache snapshot under lock so cached entries are served without
        // hitting their providers again (brief FR-21 "结果缓存免重译").
        std::vector<std::pair<std::string, EngineResult>> cachedHits;
        std::vector<EngineChainItem> toRun;
        for (const auto& item : engines) {
            if (auto hit = cachedResult(item, request.sourceLanguage, request.targetLanguage,
                                        request.texts)) {
                cachedHits.emplace_back(item.id, std::move(*hit));
            } else {
                toRun.push_back(item);
            }
        }

        std::mutex collectMutex;
        std::vector<std::thread> workers;
        for (const auto& item : toRun) {
            workers.emplace_back([&, item] {
                ResolveResult resolved = resolve(item);
                EngineResult result;
                if (!resolved.resolved || !resolved.provider->isAvailable()) {
                    result = EngineResult::makeFailure(
                        item.resolvedEngine(),
                        resolved.resolved ? "engine unavailable (not configured)"
                                          : "chain entry unavailable (no provider)",
                        0.0, resolved.displayName);
                } else {
                    result = runProvider(resolved, item, request.texts, request.sourceLanguage,
                                         request.targetLanguage, request.scene,
                                         request.cancelToken);
                }
                if (result.isSuccess()) {
                    storeResult(item, request.sourceLanguage, request.targetLanguage, request.texts,
                                result);
                }
                std::lock_guard<std::mutex> lock(collectMutex);
                outputs.push_back({item.id, std::move(result)});
            });
        }
        for (auto& w : workers) w.join();
        for (auto& [itemId, result] : cachedHits) {
            outputs.push_back({itemId, std::move(result)});
        }
    }

    // Selection order preserved (Swift: collected[uuid] then compactMap).
    std::vector<EngineResult> ordered;
    ordered.reserve(engines.size());
    for (const auto& item : engines) {
        for (auto& o : outputs) {
            if (o.itemId == item.id) {
                ordered.push_back(std::move(o.result));
                break;
            }
        }
    }

    bool anySuccess = false;
    for (const auto& r : ordered) {
        if (r.isSuccess()) {
            anySuccess = true;
            break;
        }
    }
    if (!anySuccess) {
        SchedulerOutcome out;
        for (const auto& r : ordered) {
            out.failures.push_back({r.engine, r.displayName, r.errorText});
        }
        return out;
    }
    TranslationResultBundle bundle;
    bundle.results = std::move(ordered);
    bundle.primaryEngine = bundle.results.front().engine;
    bundle.selectionMode = EngineSelectionMode::Parallel;
    bundle.scene = static_cast<int>(request.scene);
    SchedulerOutcome out;
    out.bundle = std::move(bundle);
    return out;
}

std::optional<EngineResult> TranslationScheduler::cachedResult(
    const EngineChainItem& item, const std::string& sourceLanguage,
    const std::string& targetLanguage, const std::vector<std::string>& texts) const {
    CacheKey key{engineKeyFor(item), sourceLanguage, targetLanguage, hashTexts(texts)};
    std::lock_guard<std::mutex> lock(cacheMutex_);
    auto it = resultCache_.find(key);
    if (it == resultCache_.end()) return std::nullopt;
    return it->second;
}

void TranslationScheduler::storeResult(const EngineChainItem& item,
                                       const std::string& sourceLanguage,
                                       const std::string& targetLanguage,
                                       const std::vector<std::string>& texts,
                                       const EngineResult& result) {
    CacheKey key{engineKeyFor(item), sourceLanguage, targetLanguage, hashTexts(texts)};
    std::lock_guard<std::mutex> lock(cacheMutex_);
    resultCache_[key] = result;
}

void TranslationScheduler::clearResultCache() {
    std::lock_guard<std::mutex> lock(cacheMutex_);
    resultCache_.clear();
}

// ---------------------------------------------------------------------------
// Scene routing (brief FR-23).
// ---------------------------------------------------------------------------
SchedulerOutcome TranslationScheduler::translateForScene(
    const ChainRequest& globalChain, const CompareRequest& globalCompare,
    const SceneEngineBinding& binding, const std::vector<std::string>& texts,
    const std::string& sourceLanguage, const std::string& targetLanguage,
    const CancellationToken& token) {
    if (!binding.chain.empty()) {
        ChainRequest req = globalChain;
        req.chain = binding.chain;
        req.texts = texts;
        req.sourceLanguage = sourceLanguage;
        req.targetLanguage = targetLanguage;
        req.scene = binding.scene;
        req.cancelToken = token;
        return translateOrdered(req);
    }
    if (!binding.compareSet.empty()) {
        CompareRequest req = globalCompare;
        req.engines = binding.compareSet;
        req.texts = texts;
        req.sourceLanguage = sourceLanguage;
        req.targetLanguage = targetLanguage;
        req.scene = binding.scene;
        req.cancelToken = token;
        return translateCompare(req);
    }
    // Unbound scene → global chain (default V2 mode is ordered).
    ChainRequest req = globalChain;
    req.texts = texts;
    req.sourceLanguage = sourceLanguage;
    req.targetLanguage = targetLanguage;
    req.cancelToken = token;
    return translateOrdered(req);
}

// ---------------------------------------------------------------------------
// EngineRegistry
// ---------------------------------------------------------------------------
EngineRegistry::EngineRegistry(BuiltinFactory builtinFactory,
                               CompatibleFactory compatibleFactory,
                               CredentialStorePtr credentials, HttpTransportPtr transport)
    : builtinFactory_(std::move(builtinFactory)),
      compatibleFactory_(std::move(compatibleFactory)),
      credentials_(std::move(credentials)),
      transport_(std::move(transport)) {}

TranslationProviderPtr EngineRegistry::provider(TranslationEngineType engine,
                                                const EngineConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = builtin_.find(engine);
    if (it != builtin_.end() && it->second.first == config) {
        // Cached provider matches the config slice → reuse (Swift
        // matchesCachedProvider). unique_ptr cannot be shared; move a fresh
        // factory clone instead so callers keep independent ownership.
        return builtinFactory_(engine, config);
    }
    auto provider = builtinFactory_(engine, config);
    if (provider) {
        builtin_[engine] = {config, nullptr};
    }
    return provider;
}

TranslationProviderPtr EngineRegistry::compatibleProvider(const CompatibleConfig& config) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = compatible_.find(config.id);
    if (it != compatible_.end() && it->second.first == config) {
        return compatibleFactory_(config, EngineConfig::make(TranslationEngineType::Custom));
    }
    auto provider = compatibleFactory_(config, EngineConfig::make(TranslationEngineType::Custom));
    if (provider) {
        compatible_[config.id] = {config, nullptr};
    }
    return provider;
}

void EngineRegistry::clearCompatibleProviders() {
    std::lock_guard<std::mutex> lock(mutex_);
    compatible_.clear();
}

TranslationProviderPtr makeBuiltinProvider(TranslationEngineType engine,
                                           const EngineConfig& config,
                                           const CredentialStorePtr& credentials,
                                           const HttpTransportPtr& transport) {
    switch (engine) {
        case TranslationEngineType::MtranServer: {
            auto parsed = MTranServerProvider::Configuration::parse(
                config.baseURL.empty() ? "localhost:8989" : config.baseURL,
                config.effectiveTimeout());
            if (!parsed.has_value()) parsed = MTranServerProvider::Configuration::makeDefault();
            return std::make_unique<MTranServerProvider>(std::move(*parsed), transport);
        }
        case TranslationEngineType::OpenAI:
        case TranslationEngineType::Claude:
        case TranslationEngineType::Gemini:
        case TranslationEngineType::Ollama:
            return std::make_unique<LLMChatProvider>(engine, config, credentials, transport);
        case TranslationEngineType::Google:
            return std::make_unique<GoogleTranslateProvider>(config, credentials, transport);
        case TranslationEngineType::DeepL:
            return std::make_unique<DeepLProvider>(config, credentials, transport);
        case TranslationEngineType::Baidu:
            return std::make_unique<BaiduProvider>(config, credentials, transport);
        case TranslationEngineType::Custom:
            return nullptr;  // instances are created via compatibleProvider()
    }
    return nullptr;
}

}  // namespace tf
