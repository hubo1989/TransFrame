// tf/WinHttpTransport.h — WinHTTP implementation of IHttpTransport.
// Windows-only (link winhttp.lib). Everything above this header is platform-
// clean; tests compile the Core sources with a mock transport instead.
#ifndef TF_WIN_HTTP_TRANSPORT_H
#define TF_WIN_HTTP_TRANSPORT_H

#ifdef _WIN32

#include "tf/HttpTransport.hpp"

#include <string>

namespace tf {

/// Production transport: WinHTTP with per-request timeout and cooperative
/// cancellation (brief §2: 超时 10s、取消 token、错误分类).
///
/// Cancellation: WinHTTP is synchronous here; the token is polled between
/// progress callbacks so a cancelled request aborts within ~100ms.
class WinHttpTransport final : public IHttpTransport {
public:
    WinHttpTransport();
    ~WinHttpTransport() override;

    HttpOutcome send(const HttpRequest& request) override;

private:
    void* hSession_ = nullptr;  // HINTERNET
};

}  // namespace tf

#endif  // _WIN32
#endif  // TF_WIN_HTTP_TRANSPORT_H
