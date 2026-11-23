// tf/MTranServerProvider.hpp
// MTranServer engine (port of Swift MTranServerEngine.swift): strict
// host:port endpoint validation, /translate POST, /health probe ladder with
// 5s cache TTL, language normalization.
#ifndef TF_MTRAN_SERVER_PROVIDER_HPP
#define TF_MTRAN_SERVER_PROVIDER_HPP

#include "tf/TranslationProvider.hpp"

namespace tf {

class MTranServerProvider final : public ITranslationProvider {
public:
    /// Parsed endpoint (Swift MTranServerEngine.Configuration): host (hostname
    /// or IPv4, no path/query), port, timeout, autoDetect.
    struct Configuration {
        std::string serverAddress;   // host only
        int serverPort = 8989;
        double timeout = 10.0;
        bool autoDetectSourceLanguage = true;

        static Configuration makeDefault();                       // localhost:8989
        /// Parses "host", "host:port", "http://host[:port]"; rejects paths,
        /// queries, non-hostname characters (Swift isHostnameSafeCharacter).
        static std::optional<Configuration> parse(const std::string& serverUrl,
                                                  double timeout = 10.0,
                                                  bool autoDetect = true);
        bool isValid() const;
        std::string endpointUrl(const std::string& path) const;   // http://host:port/path
    };

    MTranServerProvider(Configuration config, HttpTransportPtr transport);

    std::string id() const override { return "mtranserver"; }
    std::string name() const override { return "MTransServer"; }
    TranslationEngineType engineType() const override { return TranslationEngineType::MtranServer; }
    bool isAvailable() const override { return config_.isValid(); }
    TranslationOutcome translate(const TranslateRequest& request) override;
    std::optional<ProviderError> verifyConnection(const CancellationToken& token) override;

    const Configuration& configuration() const { return config_; }

    /// Language normalization (Swift normalizeLanguageCode): zh-Hans→zh,
    /// zh-Hant→zh-TW, en-US→en, en… base-code extraction.
    static std::string normalizeLanguageCode(const std::string& code);

    struct ProbeOutcomeValue {
        enum class Kind { Reachable, Unreachable, Cancelled } kind = Kind::Unreachable;
        std::string viaEndpoint;
    };
    using ProbeOutcome = ProbeOutcomeValue;

    /// Probe ladder (Swift MTranServerChecker): GET /health, /, /translate —
    /// 2s timeout each; tolerate 401/403/404/405/429/501 on the first two.
    ProbeOutcome probeEndpoint(const Configuration& config, const CancellationToken& token);

private:
    Configuration config_;
    HttpTransportPtr transport_;
};

/// Health-check cache with 5s TTL per endpoint (Swift MTranServerChecker:
/// cachedResults + in-flight dedupe). Pure logic — tests drive the clock.
class MTranServerHealthCache {
public:
    explicit MTranServerHealthCache(double ttlSeconds = 5.0);

    struct Entry {
        bool available = false;
        double checkedAt = 0.0;  // seconds on the injected clock
    };

    std::optional<Entry> cached(const std::string& host, int port, double now) const;
    void store(const std::string& host, int port, bool available, double now);
    void invalidateAll();

private:
    double ttl_;
    std::map<std::pair<std::string, int>, Entry> cache_;
};

}  // namespace tf

#endif  // TF_MTRAN_SERVER_PROVIDER_HPP
