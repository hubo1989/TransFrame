// MTranServerProvider.cpp — MTranServer engine port (Swift MTranServerEngine).
#include "tf/MTranServerProvider.hpp"
#include "tf/Json.hpp"

#include <cctype>

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

bool isHostnameSafeCharacter(char c) {
    unsigned char u = static_cast<unsigned char>(c);
    if ((u >= '0' && u <= '9') || (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z')) return true;
    return c == '.' || c == '-' || c == '_';
}

}  // namespace

MTranServerProvider::Configuration MTranServerProvider::Configuration::makeDefault() {
    Configuration c;
    c.serverAddress = "localhost";
    c.serverPort = 8989;
    return c;
}

std::optional<MTranServerProvider::Configuration> MTranServerProvider::Configuration::parse(
    const std::string& serverUrl, double timeout, bool autoDetect) {
    Configuration out;
    out.timeout = timeout;
    out.autoDetectSourceLanguage = autoDetect;

    std::string trimmed = trimCopy(serverUrl);
    if (trimmed.empty()) return std::nullopt;
    std::string host = trimmed;
    std::string portStr;

    const std::size_t scheme = host.find("://");
    if (scheme != std::string::npos) {
        const std::string schemePart = host.substr(0, scheme);
        if (schemePart != "http") return std::nullopt;  // MTranServer is plain HTTP
        host = host.substr(scheme + 3);
    }
    // Reject embedded path/query (Swift restricts to host[:port]).
    if (host.find('/') != std::string::npos || host.find('?') != std::string::npos ||
        host.find('#') != std::string::npos) {
        return std::nullopt;
    }
    const std::size_t colon = host.rfind(':');
    if (colon != std::string::npos) {
        portStr = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (portStr.empty()) return std::nullopt;
        for (char c : portStr) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt;
        }
        out.serverPort = std::stoi(portStr);
        if (out.serverPort <= 0 || out.serverPort > 65535) return std::nullopt;
    }
    if (host.empty()) return std::nullopt;
    for (char c : host) {
        if (!isHostnameSafeCharacter(c)) return std::nullopt;
    }
    out.serverAddress = host;
    return out;
}

bool MTranServerProvider::Configuration::isValid() const {
    return !serverAddress.empty() &&
           endpointUrl("/translate").rfind("http://", 0) == 0 &&
           serverPort > 0 && serverPort <= 65535;
}

std::string MTranServerProvider::Configuration::endpointUrl(const std::string& path) const {
    return "http://" + serverAddress + ":" + std::to_string(serverPort) + path;
}

MTranServerProvider::MTranServerProvider(Configuration config, HttpTransportPtr transport)
    : config_(std::move(config)), transport_(std::move(transport)) {}

std::string MTranServerProvider::normalizeLanguageCode(const std::string& code) {
    std::string lower = code;
    for (auto& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (lower == "zh-hans" || lower == "zh-cn" || lower == "zh_hans" || lower == "zh_cn") return "zh";
    if (lower == "zh-hant" || lower == "zh-tw" || lower == "zh_hant" || lower == "zh_tw")
        return "zh-TW";
    if (lower == "en-us" || lower == "en-gb" || lower == "en_us" || lower == "en_gb") return "en";
    if (lower == "ja-jp" || lower == "ja_jp") return "ja";
    if (lower == "ko-kr" || lower == "ko_kr") return "ko";
    std::size_t sep = lower.find('-');
    if (sep != std::string::npos) return lower.substr(0, sep);
    sep = lower.find('_');
    if (sep != std::string::npos) return lower.substr(0, sep);
    return lower;
}

TranslationOutcome MTranServerProvider::translate(const TranslateRequest& request) {
    if (request.texts.empty()) return TranslationOutcome::ok({});
    if (!config_.isValid()) {
        return TranslationOutcome::fail(ProviderError::configuration("Invalid MTranServer URL"));
    }

    // Auto-detect source language when enabled and caller passed nothing.
    std::string source = request.sourceLanguage;
    if (source.empty() && !config_.autoDetectSourceLanguage) source = "auto";
    if (source.empty()) source = "auto";
    const std::string target = normalizeLanguageCode(request.targetLanguage);

    std::vector<TranslationResult> results;
    results.reserve(request.texts.size());
    for (const auto& text : request.texts) {
        if (trimCopy(text).empty()) {
            return TranslationOutcome::fail(ProviderError::content("Cannot translate empty text"));
        }
        HttpRequest req;
        req.method = "POST";
        req.url = config_.endpointUrl("/translate");
        req.timeoutSeconds = static_cast<int>(config_.timeout);
        req.cancelToken = request.cancelToken;
        req.headers["Content-Type"] = "application/json";
        req.body = Json{{"text", text}, {"from", source}, {"to", target}}.dump();

        auto outcome = transport_->send(req);
        if (outcome.error.has_value()) {
            return TranslationOutcome::fail(*outcome.error);
        }
        const HttpResponse& resp = *outcome.response;
        if (resp.status == 503) {
            return TranslationOutcome::fail(
                ProviderError::network("MTranServer service unavailable"));
        }
        if (resp.status != 200) {
            return TranslationOutcome::fail(ProviderError::network(
                "MTranServer HTTP " + std::to_string(resp.status)));
        }
        Json json;
        try {
            json = Json::parse(resp.body);
        } catch (const Json::parse_error&) {
            return TranslationOutcome::fail(ProviderError::content("Invalid MTranServer response"));
        }
        if (!json.is_object() || !json.contains("result") || !json["result"].is_string()) {
            return TranslationOutcome::fail(ProviderError::content("Invalid MTranServer response"));
        }
        const std::string translated = json["result"].get<std::string>();
        std::string detected;
        if (json.contains("detected_language") && json["detected_language"].is_string()) {
            detected = json["detected_language"].get<std::string>();
        }
        results.emplace_back(
            text, translated,
            detected.empty() ? (request.sourceLanguage.empty() ? "auto" : request.sourceLanguage)
                             : detected,
            request.targetLanguage);
    }
    return TranslationOutcome::ok(std::move(results));
}

MTranServerProvider::ProbeOutcome MTranServerProvider::probeEndpoint(
    const Configuration& config, const CancellationToken& token) {
    static const char* endpoints[] = {"/health", "/", "/translate"};
    for (const char* endpoint : endpoints) {
        if (token.cancelled()) return {ProbeOutcomeValue::Kind::Cancelled, ""};
        HttpRequest req;
        req.method = "GET";
        req.url = config.endpointUrl(endpoint);
        req.timeoutSeconds =
            static_cast<int>(std::min<double>(config.timeout, 2.0));
        req.cancelToken = token;
        auto outcome = transport_->send(req);
        if (outcome.error.has_value()) {
            if (outcome.isCancelled()) return {ProbeOutcomeValue::Kind::Cancelled, ""};
            return {ProbeOutcomeValue::Kind::Unreachable, endpoint};
        }
        const int status = outcome.response->status;
        if (status >= 200 && status < 300) {
            return {ProbeOutcomeValue::Kind::Reachable, endpoint};
        }
        if (std::string(endpoint) != "/translate") {
            // 404/405/501: endpoint missing; 401/403/429: a gateway may only
            // guard some paths while /translate works; other 5xx fail fast
            // (Swift comment).
            if (status == 401 || status == 403 || status == 404 || status == 405 ||
                status == 429 || status == 501) {
                continue;
            }
        }
        return {ProbeOutcomeValue::Kind::Unreachable, endpoint};
    }
    return {ProbeOutcomeValue::Kind::Unreachable, ""};
}

std::optional<ProviderError> MTranServerProvider::verifyConnection(const CancellationToken& token) {
    auto probe = probeEndpoint(config_, token);
    if (probe.kind == ProbeOutcomeValue::Kind::Cancelled) {
        return ProviderError::network("cancelled");
    }
    if (probe.kind == ProbeOutcomeValue::Kind::Reachable) return std::nullopt;
    return ProviderError::network("MTranServer endpoint unreachable");
}

// ---------------------------------------------------------------------------
// Health cache (5s TTL, Swift MTranServerChecker semantics).
// ---------------------------------------------------------------------------
MTranServerHealthCache::MTranServerHealthCache(double ttlSeconds) : ttl_(ttlSeconds) {}

std::optional<MTranServerHealthCache::Entry> MTranServerHealthCache::cached(
    const std::string& host, int port, double now) const {
    auto it = cache_.find({host, port});
    if (it == cache_.end()) return std::nullopt;
    if (now - it->second.checkedAt >= ttl_) return std::nullopt;  // stale
    return it->second;
}

void MTranServerHealthCache::store(const std::string& host, int port, bool available, double now) {
    cache_[{host, port}] = Entry{available, now};
}

void MTranServerHealthCache::invalidateAll() { cache_.clear(); }

}  // namespace tf
