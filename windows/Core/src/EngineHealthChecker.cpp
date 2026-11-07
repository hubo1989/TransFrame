// EngineHealthChecker.cpp — green/red/orange/gray probe + 60s cache.
#include "tf/EngineHealthChecker.hpp"
#include "tf/CompatibleChatProvider.hpp"
#include "tf/LLMChatProvider.hpp"
#include "tf/MTranServerProvider.hpp"
#include "tf/TranslationScheduler.hpp"

#include <algorithm>
#include <chrono>

namespace tf {

std::string shortReason(const std::optional<ProviderError>& error) {
    if (!error.has_value()) return "";
    std::string text = error->message;
    if (text.size() > 120) text = text.substr(0, 120);
    return text;
}

namespace {
double defaultClock() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
}  // namespace

DefaultEngineProbe::DefaultEngineProbe(
    CredentialStorePtr credentials, HttpTransportPtr transport,
    std::function<std::vector<CompatibleConfig>()> compatibleConfigs)
    : credentials_(std::move(credentials)),
      transport_(std::move(transport)),
      compatibleConfigs_(std::move(compatibleConfigs)) {}

EngineHealth DefaultEngineProbe::probe(const EngineHealthKey& key,
                                       const CancellationToken& token) {
    if (!key.isBuiltin) {
        // Compatible instance: dangling UUID (deleted) → red.
        std::vector<CompatibleConfig> configs =
            compatibleConfigs_ ? compatibleConfigs_() : std::vector<CompatibleConfig>{};
        auto it = std::find_if(configs.begin(), configs.end(),
                               [&](const CompatibleConfig& c) { return c.id == key.compatibleId; });
        if (it == configs.end()) {
            return EngineHealth::Misconfigured;  // instance deleted
        }
        // Credential gate only when the instance declares a key.
        if (it->hasAPIKey) {
            if (!credentials_ || !credentials_->has(credentialAccountForCompatible(it->id))) {
                return EngineHealth::Unconfigured;
            }
        }
        // Free models-list probe on the injected transport.
        CompatibleChatProvider provider(*it, EngineConfig::make(TranslationEngineType::Custom),
                                        credentials_, transport_);
        auto err = provider.verifyConnection(token);
        if (!err.has_value()) return EngineHealth::Available;
        return EngineHealth::Misconfigured;
    }

    switch (key.builtin) {
        case TranslationEngineType::MtranServer: {
            // Probe the configured endpoint (remote server must not be judged
            // by localhost's fate — Swift PR #94).
            MTranServerProvider provider(MTranServerProvider::Configuration::makeDefault(),
                                         transport_);
            auto err = provider.verifyConnection(token);
            if (!err.has_value()) return EngineHealth::Available;
            return EngineHealth::Misconfigured;
        }
        case TranslationEngineType::Google:
        case TranslationEngineType::Baidu:
            // No free probe endpoint: orange without keys, gray with.
            if (credentials_ && credentials_->has(credentialAccount(key.builtin))) {
                return EngineHealth::Unverified;
            }
            return EngineHealth::Unconfigured;
        case TranslationEngineType::Claude:
        case TranslationEngineType::Gemini:
        case TranslationEngineType::DeepL:
            // Their verifyConnection actually translates "1" (paid call) —
            // automatic probes must not consume quota (Swift policy).
            if (credentials_ && credentials_->has(credentialAccount(key.builtin))) {
                return EngineHealth::Unverified;
            }
            return EngineHealth::Unconfigured;
        case TranslationEngineType::OpenAI:
        case TranslationEngineType::Ollama: {
            // Free models-list probe behind a credentials gate (openai only).
            if (key.builtin == TranslationEngineType::OpenAI) {
                if (!credentials_ || !credentials_->has(credentialAccount(key.builtin))) {
                    return EngineHealth::Unconfigured;
                }
            }
            LLMChatProvider provider(key.builtin,
                                     EngineConfig::make(key.builtin), credentials_, transport_);
            auto err = provider.verifyConnection(token);
            if (!err.has_value()) return EngineHealth::Available;
            return EngineHealth::Misconfigured;
        }
        case TranslationEngineType::Custom:
            // Builtin "custom" slot is the dynamic instance list; probing it
            // directly has no endpoint. Gray (Swift: no case for it — the UI
            // only lists instances).
            return EngineHealth::Unverified;
    }
    return EngineHealth::Unverified;
}

EngineHealthChecker::EngineHealthChecker(std::shared_ptr<IEngineProbe> probe, double ttlSeconds,
                                         std::function<double()> clock)
    : probe_(std::move(probe)), ttl_(ttlSeconds), clock_(clock ? std::move(clock) : defaultClock) {}

EngineHealth EngineHealthChecker::health(const EngineHealthKey& key, const CancellationToken& token) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = cache_.find(key);
        if (it != cache_.end() && clock_() - it->second.at < ttl_) {
            return it->second.health;
        }
    }
    return refresh(key, token);
}

EngineHealth EngineHealthChecker::refresh(const EngineHealthKey& key, const CancellationToken& token) {
    EngineHealth result = probe_ ? probe_->probe(key, token) : EngineHealth::Unverified;
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[key] = Entry{result, "", clock_()};
    return result;
}

void EngineHealthChecker::invalidateAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_.clear();
}

void EngineHealthChecker::overrideClock(std::function<double()> clock) {
    std::lock_guard<std::mutex> lock(mutex_);
    clock_ = clock ? std::move(clock) : defaultClock;
}

void EngineHealthChecker::injectHealth(const EngineHealthKey& key, EngineHealth health, double at) {
    std::lock_guard<std::mutex> lock(mutex_);
    cache_[key] = Entry{health, "", at};
}

}  // namespace tf
