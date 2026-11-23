// tf/EngineHealthChecker.hpp
// Four-state health probing for settings-page engine cards (brief §4, PRD
// FR-22; port of Swift EngineHealthChecker.swift). Pure logic: probes run on
// the injected transport; the 60s cache semantics mirror macOS exactly.
#ifndef TF_ENGINE_HEALTH_CHECKER_HPP
#define TF_ENGINE_HEALTH_CHECKER_HPP

#include "tf/CoreModels.hpp"
#include "tf/HttpTransport.hpp"
#include "tf/TranslationProvider.hpp"

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace tf {

/// Four-state health (Swift EngineHealth; `checking` is a UI transition
/// state and lives in the UI layer, not here).
enum class EngineHealth {
    Available,     // green — probe succeeded
    Misconfigured, // red — probe failed; reason carries a short message
    Unconfigured,  // orange — missing credentials/required fields; no probe sent
    Unverified,    // gray — configured but no free probe endpoint exists
};

inline const char* engineHealthName(EngineHealth h) {
    switch (h) {
        case EngineHealth::Available: return "green";
        case EngineHealth::Misconfigured: return "red";
        case EngineHealth::Unconfigured: return "orange";
        case EngineHealth::Unverified: return "gray";
    }
    return "gray";
}

/// Health key: builtin engines by type; compatible instances by UUID
/// (Swift EngineHealthKey).
struct EngineHealthKey {
    bool isBuiltin = true;
    TranslationEngineType builtin = TranslationEngineType::MtranServer;
    std::string compatibleId;  // UUID when !isBuiltin

    static EngineHealthKey forBuiltin(TranslationEngineType t) {
        EngineHealthKey k;
        k.isBuiltin = true;
        k.builtin = t;
        return k;
    }
    static EngineHealthKey forCompatible(const std::string& uuid) {
        EngineHealthKey k;
        k.isBuiltin = false;
        k.compatibleId = uuid;
        return k;
    }
    std::string display() const {
        return isBuiltin ? std::string(translationEngineRaw(builtin)) : "custom:" + compatibleId;
    }
    bool operator<(const EngineHealthKey& o) const {
        if (isBuiltin != o.isBuiltin) return isBuiltin < o.isBuiltin;
        if (isBuiltin) return builtin < o.builtin;
        return compatibleId < o.compatibleId;
    }
    bool operator==(const EngineHealthKey& o) const = default;
};

/// Probe seam: one probe per key. Implementations MUST use only free
/// endpoints (MTranServer /health, LLM models list) — never billed
/// translation calls (PRD FR-22).
class IEngineProbe {
public:
    virtual ~IEngineProbe() = default;
    virtual EngineHealth probe(const EngineHealthKey& key, const CancellationToken& token) = 0;
};

/// Probe implementation over the shared transport + credential store +
/// provider factories (mirrors Swift probe(_:) switch semantics minus apple).
class DefaultEngineProbe : public IEngineProbe {
public:
    DefaultEngineProbe(CredentialStorePtr credentials, HttpTransportPtr transport,
                       std::function<std::vector<CompatibleConfig>()> compatibleConfigs);

    EngineHealth probe(const EngineHealthKey& key, const CancellationToken& token) override;

private:
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
    std::function<std::vector<CompatibleConfig>()> compatibleConfigs_;
};

/// Cache + dispatcher (Swift EngineHealthChecker actor semantics; here a
/// mutex-guarded object). TTL 60s, injectable clock for tests.
class EngineHealthChecker {
public:
    explicit EngineHealthChecker(std::shared_ptr<IEngineProbe> probe, double ttlSeconds = 60.0,
                                 std::function<double()> clock = nullptr);

    /// Cached health; refreshes when stale or absent (Swift health(for:)).
    EngineHealth health(const EngineHealthKey& key, const CancellationToken& token = {});
    /// Force a probe bypassing the cache (Swift refresh(_:)).
    EngineHealth refresh(const EngineHealthKey& key, const CancellationToken& token = {});
    /// Drop all cached entries (config save) (Swift invalidateAll()).
    void invalidateAll();

    // Test seams.
    void overrideClock(std::function<double()> clock);
    void injectHealth(const EngineHealthKey& key, EngineHealth health, double at);

private:
    std::shared_ptr<IEngineProbe> probe_;
    double ttl_;
    std::function<double()> clock_;
    std::mutex mutex_;
    struct Entry {
        EngineHealth health;
        std::string reason;
        double at;
    };
    std::map<EngineHealthKey, Entry> cache_;
};

/// Reasons ride alongside Misconfigured (Swift payload); helper for probes.
std::string shortReason(const std::optional<ProviderError>& error);

}  // namespace tf

#endif  // TF_ENGINE_HEALTH_CHECKER_HPP
