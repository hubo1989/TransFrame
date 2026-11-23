// tests/TestSupport.hpp — mocks shared across the Catch2 suites.
#ifndef TF_TESTS_TEST_SUPPORT_HPP
#define TF_TESTS_TEST_SUPPORT_HPP

#include "tf/CloudProviders.hpp"
#include "tf/CompatibleChatProvider.hpp"
#include "tf/EngineHealthChecker.hpp"
#include "tf/HttpTransport.hpp"
#include "tf/LLMChatProvider.hpp"
#include "tf/MTranServerProvider.hpp"
#include "tf/TextSelectionInsertion.hpp"
#include "tf/TranslationProvider.hpp"
#include "tf/TranslationScheduler.hpp"

#include <atomic>
#include <chrono>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace tf {
namespace tests {

// ---------------------------------------------------------------------------
/// In-memory credential store (brief: "KeyStore mock").
class MockCredentialStore final : public ICredentialStore {
public:
    void save(const std::string& account, const StoredCredentials& credentials) override {
        std::lock_guard<std::mutex> lock(mutex_);
        store_[account] = credentials;
    }
    std::optional<StoredCredentials> load(const std::string& account) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = store_.find(account);
        if (it == store_.end()) return std::nullopt;
        return it->second;
    }
    bool remove(const std::string& account) override {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.erase(account) > 0;
    }
    bool has(const std::string& account) const override {
        std::lock_guard<std::mutex> lock(mutex_);
        return store_.count(account) > 0;
    }

private:
    mutable std::mutex mutex_;
    std::map<std::string, StoredCredentials> store_;
};

// ---------------------------------------------------------------------------
/// Transport mock: canned per-URL responses + failure injection + latency +
/// request recording.
class MockHttpTransport final : public IHttpTransport {
public:
    struct ResponseSpec {
        int status = 200;
        std::string body;
        std::optional<ProviderError> error;  // transport-level failure
        int latencyMs = 0;
    };

    HttpOutcome send(const HttpRequest& request) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests.push_back(request);
        }
        if (request.cancelToken.cancelled()) {
            return HttpOutcome::fail(ProviderError::network("cancelled"));
        }
        auto it = routes.find(request.url);
        if (it == routes.end()) {
            return HttpOutcome::fail(ProviderError::network("no route for " + request.url));
        }
        if (it->second.latencyMs > 0) {
            const auto deadline = std::chrono::steady_clock::now() +
                                  std::chrono::milliseconds(it->second.latencyMs);
            while (std::chrono::steady_clock::now() < deadline) {
                if (request.cancelToken.cancelled()) {
                    return HttpOutcome::fail(ProviderError::network("cancelled"));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        if (it->second.error.has_value()) {
            return HttpOutcome::fail(*it->second.error);
        }
        HttpResponse resp;
        resp.status = it->second.status;
        resp.body = it->second.body;
        return HttpOutcome::ok(std::move(resp));
    }

    std::map<std::string, ResponseSpec> routes;
    std::vector<HttpRequest> requests;
    mutable std::mutex mutex_;
};

// ---------------------------------------------------------------------------
/// Provider mock for scheduler tests (brief §7: 注入延迟/失败).
class MockTranslationProvider final : public ITranslationProvider {
public:
    struct Behavior {
        bool available = true;
        std::string translateError;  // non-empty → fail with content error
        std::optional<ProviderError> error;
        int delayMs = 0;
        std::string resultPrefix = "T:";
        bool cancelAware = true;
    };

    MockTranslationProvider(std::string providerId, TranslationEngineType engine,
                            Behavior behavior)
        : id_(std::move(providerId)), engine_(engine), behavior_(std::move(behavior)) {}

    std::string id() const override { return id_; }
    std::string name() const override { return id_; }
    TranslationEngineType engineType() const override { return engine_; }
    bool isAvailable() const override { return behavior_.available; }

    TranslationOutcome translate(const TranslateRequest& request) override {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++callCount;
            lastTexts = request.texts;
        }
        if (behavior_.delayMs > 0) {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(behavior_.delayMs);
            while (std::chrono::steady_clock::now() < deadline) {
                if (behavior_.cancelAware && request.cancelToken.cancelled()) {
                    return TranslationOutcome::fail(ProviderError::network("cancelled"));
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
        }
        if (behavior_.error.has_value()) return TranslationOutcome::fail(*behavior_.error);
        if (!behavior_.translateError.empty()) {
            return TranslationOutcome::fail(
                ProviderError::content(behavior_.translateError));
        }
        std::vector<TranslationResult> results;
        for (const auto& t : request.texts) {
            results.emplace_back(t, behavior_.resultPrefix + t,
                                 request.sourceLanguage.empty() ? "Auto" : request.sourceLanguage,
                                 request.targetLanguage);
        }
        return TranslationOutcome::ok(std::move(results));
    }

    std::optional<ProviderError> verifyConnection(const CancellationToken&) override {
        return std::nullopt;  // probes succeed by default
    }

    int calls() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return callCount;
    }

private:
    std::string id_;
    TranslationEngineType engine_;
    Behavior behavior_;
    mutable std::mutex mutex_;
    int callCount = 0;

public:
    std::vector<std::string> lastTexts;
};

inline CredentialStorePtr credStoreWith(const std::string& account, const std::string& apiKey,
                                        const std::string& appID = "") {
    auto store = std::make_shared<MockCredentialStore>();
    StoredCredentials c;
    c.apiKey = apiKey;
    c.appID = appID;
    store->save(account, c);
    return store;
}

/// Scheduler dependencies wired to a map of mock providers keyed by engine.
inline SchedulerDependencies mockDeps(
    const std::map<TranslationEngineType, ITranslationProvider*>& providers,
    const std::vector<CompatibleConfig>& compatibleConfigs = {},
    const std::map<std::string, ITranslationProvider*>& compatibleProviders = {}) {
    SchedulerDependencies deps;
    deps.builtinProvider = [providers](TranslationEngineType engine) -> TranslationProviderPtr {
        auto it = providers.find(engine);
        if (it == providers.end()) return nullptr;
        // The scheduler only needs the interface; the mock is shared here via
        // a non-owning alias implemented as a thin wrapper.
        struct Alias final : ITranslationProvider {
            ITranslationProvider* inner;
            std::string id() const override { return inner->id(); }
            std::string name() const override { return inner->name(); }
            TranslationEngineType engineType() const override { return inner->engineType(); }
            bool isAvailable() const override { return inner->isAvailable(); }
            TranslationOutcome translate(const TranslateRequest& r) override {
                return inner->translate(r);
            }
            std::optional<ProviderError> verifyConnection(const CancellationToken& t) override {
                return inner->verifyConnection(t);
            }
        };
        TranslationProviderPtr p = std::make_unique<Alias>();
        static_cast<Alias*>(p.get())->inner = it->second;
        return p;
    };
    deps.compatibleProvider =
        [compatibleProviders](const std::string& uuid) -> TranslationProviderPtr {
        auto it = compatibleProviders.find(uuid);
        if (it == compatibleProviders.end()) return nullptr;
        struct Alias final : ITranslationProvider {
            ITranslationProvider* inner;
            std::string id() const override { return inner->id(); }
            std::string name() const override { return inner->name(); }
            TranslationEngineType engineType() const override { return inner->engineType(); }
            bool isAvailable() const override { return inner->isAvailable(); }
            TranslationOutcome translate(const TranslateRequest& r) override {
                return inner->translate(r);
            }
            std::optional<ProviderError> verifyConnection(const CancellationToken& t) override {
                return inner->verifyConnection(t);
            }
        };
        TranslationProviderPtr p = std::make_unique<Alias>();
        static_cast<Alias*>(p.get())->inner = it->second;
        return p;
    };
    deps.compatibleConfigs = [compatibleConfigs] { return compatibleConfigs; };
    return deps;
}

/// Language display helper used in fixtures.
inline std::string fixedLangName(const std::string& code) {
    return promptLanguageDisplayName(code);
}

}  // namespace tests
}  // namespace tf

#endif  // TF_TESTS_TEST_SUPPORT_HPP
