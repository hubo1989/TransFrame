// tf/HttpTransport.hpp
// HTTP contract shared by every translation/OCR/VLM provider on Windows.
// The interface lives in Core (no UI dependency, per brief: "HTTP 层不依赖
// UI 框架，可被测试直接驱动"). The WinHTTP implementation lives in
// NativeShell/src/WinHttpTransport.cpp and implements the same header on real
// Win32; tests inject MockHttpTransport.
//
// Semantics ported from the Swift layer:
//   - default timeout 10s (TranslationEngineConfig.timeout, MTranServer 10s)
//   - cooperative cancellation via CancellationToken (Swift Task.cancel /
//     PRD FR-62 "引擎请求全部可取消")
//   - error classification: network / quota / auth / content (brief §2)
#ifndef TF_HTTP_TRANSPORT_HPP
#define TF_HTTP_TRANSPORT_HPP

#include "tf/CoreModels.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf {

/// Cooperative cancellation: one shared flag; cancelled() is polled by the
/// transport between WinHTTP waits (Swift Task.isCancelled analogue).
class CancellationToken {
public:
    struct State {
        std::atomic<bool> cancelled{false};
    };

    CancellationToken() : state_(std::make_shared<State>()) {}

    void cancel() { state_->cancelled.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return state_->cancelled.load(std::memory_order_relaxed); }

    /// Allows "already cancelled" tokens to be constructed in tests.
    static CancellationToken alreadyCancelled() {
        CancellationToken t;
        t.cancel();
        return t;
    }

private:
    std::shared_ptr<State> state_;
};

struct HttpRequest {
    std::string method = "GET";
    std::string url;               // absolute http(s) URL
    std::map<std::string, std::string> headers;
    std::string body;              // raw bytes (UTF-8 JSON)
    int timeoutSeconds = 10;       // brief: 超时默认 10s
    CancellationToken cancelToken;
};

struct HttpResponse {
    int status = 0;                // HTTP status; 0 = transport-level failure
    std::map<std::string, std::string> headers;  // lower-cased keys
    std::string body;

    bool ok() const { return status >= 200 && status < 300; }
};

/// Transport-level outcome: response OR ProviderError. Cancelled requests map
/// to ErrorCategory::Network with message "cancelled" — callers that need to
/// distinguish (ordered chain must not grind on) compare the message, matching
/// Swift's CancellationError rethrow semantics.
struct HttpOutcome {
    std::optional<HttpResponse> response;
    std::optional<ProviderError> error;

    static HttpOutcome ok(HttpResponse r) { return HttpOutcome{std::move(r), std::nullopt}; }
    static HttpOutcome fail(ProviderError e) { return HttpOutcome{std::nullopt, std::move(e)}; }
    bool isCancelled() const {
        return error.has_value() && error->category == ErrorCategory::Network &&
               error->message == "cancelled";
    }
};

/// Abstract HTTP facade. WinHttpTransport (NativeShell) is the production
/// implementation; tests provide mocks. All calls are blocking with internal
/// cancellation polling — providers run them on worker threads.
class IHttpTransport {
public:
    virtual ~IHttpTransport() = default;
    virtual HttpOutcome send(const HttpRequest& request) = 0;
};

/// Shared pointer alias used by providers and the registry.
using HttpTransportPtr = std::shared_ptr<IHttpTransport>;

// ---------------------------------------------------------------------------
// URL helpers (pure logic, unit-tested here on every platform).
// ---------------------------------------------------------------------------

/// Parsed absolute http(s) URL. Handles what the providers actually emit:
/// scheme, host (no userinfo), optional port, path, no query parsing beyond
/// retaining the raw query string.
struct ParsedUrl {
    std::string scheme;   // "http" | "https" (lower-cased)
    std::string host;     // lower-cased; IPv6 brackets kept
    int port = 0;         // explicit or scheme default (80/443)
    std::string path;     // begins with '/'
    std::string query;    // begins with '?' or empty

    bool valid() const { return !scheme.empty() && !host.empty(); }
    std::string authority() const;   // host[:port] when non-default
    std::string toString() const;
};

std::optional<ParsedUrl> parseUrl(const std::string& url);

/// localhost → 127.0.0.1 rewrite (Swift URL.resolvingLocalhost): avoids IPv6
/// ::1 resolution failures on Windows.
std::string resolveLocalhost(const std::string& url);

/// Swift LLMTranslationProvider.appendingAPISuffix: appends path segments
/// without duplicating an overlapping version tail (e.g. base ".../v1" +
/// suffix "chat/completions" → ".../v1/chat/completions"; base
/// ".../v1/chat/completions" + same suffix → unchanged; case-insensitive).
std::string appendingApiSuffix(const std::string& baseUrl, const std::string& suffix);

/// Host is localhost-family (localhost / 127.0.0.1 / ::1 / 0.0.0.0 / *.local)
/// — gates the "refuse API key over plain HTTP" rule (Swift isLocalhost).
bool isLocalhostHost(const std::string& host);

/// Security rule shared by OpenAI/Claude/Gemini/custom: an API key must never
/// ride a non-HTTPS transport unless the host is localhost-family.
/// Returns an Auth ProviderError when the combination is unsafe.
std::optional<ProviderError> guardApiKeyTransport(const std::string& url, bool sendingApiKey);

/// Percent-encode a query component (Baidu q= / Google key=).
std::string urlEncode(const std::string& value);

}  // namespace tf

#endif  // TF_HTTP_TRANSPORT_HPP
