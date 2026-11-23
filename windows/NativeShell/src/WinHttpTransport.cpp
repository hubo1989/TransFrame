// WinHttpTransport.cpp — WinHTTP 5.1 implementation (NOT RUN on this macOS
// host; build under MSVC with winhttp.lib). Error taxonomy per brief §2:
//   network  — connection refused / timeout / cancelled / DNS
//   quota    — HTTP 429 (surfaced by providers, mapped here too)
//   auth     — 401/403
//   content  — 2xx with empty body handled by providers
#include "tf/WinHttpTransport.h"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>

#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace tf {

namespace {

/// Status-code → error category (brief §2 mapping).
ProviderError classifyStatus(int status) {
    if (status == 401 || status == 403) {
        return ProviderError::auth("HTTP " + std::to_string(status));
    }
    if (status == 429) {
        return ProviderError::quota("HTTP 429 rate limited");
    }
    if (status >= 500) {
        return ProviderError::network("HTTP " + std::to_string(status));
    }
    return ProviderError::content("HTTP " + std::to_string(status));
}

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    out.resize(out.size() - 1);  // drop the NUL
    return out;
}

std::string toUtf8(const std::wstring& wide) {
    if (wide.empty()) return std::string();
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<std::size_t>(len), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, out.data(), len, nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}

}  // namespace

WinHttpTransport::WinHttpTransport() {
    hSession_ = WinHttpOpen(L"TransFrame/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
}

WinHttpTransport::~WinHttpTransport() {
    if (hSession_) WinHttpCloseHandle(hSession_);
}

HttpOutcome WinHttpTransport::send(const HttpRequest& request) {
    if (!hSession_) {
        return HttpOutcome::fail(ProviderError::network("WinHTTP session unavailable"));
    }
    auto parsed = parseUrl(request.url);
    if (!parsed.has_value()) {
        return HttpOutcome::fail(ProviderError::configuration("Invalid URL: " + request.url));
    }

    const std::wstring host = toWide(parsed->host);
    const INTERNET_PORT port = static_cast<INTERNET_PORT>(parsed->port);

    void* hConnect = WinHttpConnect(hSession_, host.c_str(), port, 0);
    if (!hConnect) {
        return HttpOutcome::fail(ProviderError::network("Connect failed: " + parsed->host));
    }

    const std::wstring object = toWide(parsed->path + parsed->query);
    const std::wstring verb = toWide(request.method);
    const BOOL isHttps = parsed->scheme == "https" ? TRUE : FALSE;

    void* hRequest = WinHttpOpenRequest(hConnect, verb.c_str(), object.c_str(), nullptr,
                                        WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        isHttps ? WINHTTP_FLAG_SECURE : 0);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        return HttpOutcome::fail(ProviderError::network("OpenRequest failed"));
    }

    // Timeout: resolve/connect/send/receive all capped at the request budget.
    const DWORD timeoutMs = static_cast<DWORD>(request.timeoutSeconds) * 1000;
    WinHttpSetTimeouts(hRequest, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

    // Headers. Content-Type belongs in the block: WinHttpSendRequest takes
    // the header string verbatim and strict servers reject JSON posts
    // without the media type (400/415).
    std::wstring headerBlock;
    if (const auto ct = request.headers.find("Content-Type");
        ct != request.headers.end()) {
        headerBlock += L"Content-Type: " + toWide(ct->second) + L"\r\n";
    }
    for (const auto& [k, v] : request.headers) {
        if (k == "Content-Type") continue;
        headerBlock += toWide(k + ": " + v) + L"\r\n";
    }

    // Body as bytes.
    const void* bodyPtr = request.body.empty() ? WINHTTP_NO_REQUEST_DATA
                                               : static_cast<const void*>(request.body.data());
    const DWORD bodyLen = static_cast<DWORD>(request.body.size());

    const BOOL sent = WinHttpSendRequest(
        hRequest, !headerBlock.empty() ? headerBlock.c_str() : WINHTTP_NO_ADDITIONAL_HEADERS,
        static_cast<DWORD>(-1), const_cast<void*>(bodyPtr), bodyLen, bodyLen, 0);
    if (!sent) {
        const DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (request.cancelToken.cancelled()) {
            return HttpOutcome::fail(ProviderError::network("cancelled"));
        }
        if (err == ERROR_WINHTTP_TIMEOUT) {
            return HttpOutcome::fail(ProviderError::network("Request timed out"));
        }
        return HttpOutcome::fail(ProviderError::network("Send failed (WinHTTP error " +
                                                         std::to_string(err) + ")"));
    }

    if (!WinHttpReceiveResponse(hRequest, nullptr)) {
        const DWORD err = GetLastError();
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        if (request.cancelToken.cancelled()) {
            return HttpOutcome::fail(ProviderError::network("cancelled"));
        }
        if (err == ERROR_WINHTTP_TIMEOUT) {
            return HttpOutcome::fail(ProviderError::network("Response timed out"));
        }
        return HttpOutcome::fail(ProviderError::network("Receive failed (WinHTTP error " +
                                                         std::to_string(err) + ")"));
    }

    // Status.
    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    HttpResponse response;
    response.status = static_cast<int>(statusCode);

    // Headers (lower-cased keys).
    DWORD size = 0;
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        WINHTTP_NO_OUTPUT_BUFFER, &size, WINHTTP_NO_HEADER_INDEX);
    if (size > 0) {
        std::vector<wchar_t> buf(size / sizeof(wchar_t) + 1, L'\0');
        if (WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_RAW_HEADERS_CRLF,
                                WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &size,
                                WINHTTP_NO_HEADER_INDEX)) {
            const std::string all = toUtf8(buf.data());
            std::size_t pos = 0;
            while (pos < all.size()) {
                const std::size_t eol = all.find('\n', pos);
                std::string line =
                    all.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
                pos = eol == std::string::npos ? all.size() : eol + 1;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                const std::size_t colon = line.find(':');
                if (colon == std::string::npos) continue;
                std::string key = line.substr(0, colon);
                for (auto& c : key) {
                    c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
                }
                std::size_t vStart = colon + 1;
                while (vStart < line.size() && line[vStart] == ' ') ++vStart;
                response.headers[key] = line.substr(vStart);
            }
        }
    }

    // Body: drain available chunks, polling cancellation between reads.
    for (;;) {
        if (request.cancelToken.cancelled()) {
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            return HttpOutcome::fail(ProviderError::network("cancelled"));
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &available)) break;
        if (available == 0) break;
        std::string chunk;
        chunk.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, chunk.data(), available, &read)) break;
        chunk.resize(read);
        response.body += chunk;
        if (read == 0) break;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    return HttpOutcome::ok(std::move(response));
}

}  // namespace tf

#endif  // _WIN32
