// HttpTransport.cpp — pure URL/transport helpers (platform-clean).
#include "tf/HttpTransport.hpp"

#include <algorithm>
#include <cctype>

namespace tf {

std::string ParsedUrl::authority() const {
    const bool defaultPort = (scheme == "http" && port == 80) || (scheme == "https" && port == 443);
    if (defaultPort || port == 0) return host;
    return host + ":" + std::to_string(port);
}

std::string ParsedUrl::toString() const {
    std::string out = scheme + "://" + authority();
    out += path.empty() ? "/" : path;
    out += query;
    return out;
}

std::optional<ParsedUrl> parseUrl(const std::string& url) {
    ParsedUrl p;
    std::size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos || schemeEnd == 0) return std::nullopt;
    p.scheme = url.substr(0, schemeEnd);
    std::transform(p.scheme.begin(), p.scheme.end(), p.scheme.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (p.scheme != "http" && p.scheme != "https") return std::nullopt;

    std::size_t restPos = schemeEnd + 3;
    std::size_t pathPos = url.find('/', restPos);
    std::string authority = pathPos == std::string::npos ? url.substr(restPos)
                                                         : url.substr(restPos, pathPos - restPos);
    // Split query.
    std::size_t qPos = url.find('?');
    if (qPos != std::string::npos) {
        p.query = url.substr(qPos);
    }
    std::size_t pathLimit = (qPos != std::string::npos && qPos > pathPos) ? qPos : url.size();
    p.path = pathPos == std::string::npos ? "/" : url.substr(pathPos, pathLimit - pathPos);
    if (p.path.empty()) p.path = "/";

    // Host[:port]. IPv6 [::1]:port.
    std::string hostPort = authority;
    std::size_t frag = hostPort.find('#');
    if (frag != std::string::npos) hostPort = hostPort.substr(0, frag);

    if (!hostPort.empty() && hostPort[0] == '[') {
        std::size_t close = hostPort.find(']');
        if (close == std::string::npos) return std::nullopt;
        p.host = hostPort.substr(0, close + 1);
        if (close + 1 < hostPort.size()) {
            if (hostPort[close + 1] != ':') return std::nullopt;
            int port = std::atoi(hostPort.c_str() + close + 2);
            if (port <= 0 || port > 65535) return std::nullopt;
            p.port = port;
        }
    } else {
        std::size_t colon = hostPort.find(':');
        if (colon != std::string::npos) {
            p.host = hostPort.substr(0, colon);
            int port = std::atoi(hostPort.c_str() + colon + 1);
            if (port <= 0 || port > 65535) return std::nullopt;
            p.port = port;
        } else {
            p.host = hostPort;
        }
    }
    if (p.host.empty()) return std::nullopt;
    std::transform(p.host.begin(), p.host.end(), p.host.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (p.port == 0) p.port = (p.scheme == "https") ? 443 : 80;
    return p;
}

std::string resolveLocalhost(const std::string& url) {
    auto parsed = parseUrl(url);
    if (!parsed.has_value()) return url;
    std::string lowered = parsed->host;
    if (lowered == "localhost") {
        const bool defaultPort =
            (parsed->scheme == "http" && parsed->port == 80) ||
            (parsed->scheme == "https" && parsed->port == 443);
        std::string replacement = parsed->scheme + "://127.0.0.1";
        if (!defaultPort) replacement += ":" + std::to_string(parsed->port);
        std::string tail = parsed->path.empty() ? "/" : parsed->path;
        return replacement + tail + parsed->query;
    }
    return url;
}

static std::vector<std::string> splitPath(const std::string& path) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start < path.size()) {
        std::size_t slash = path.find('/', start);
        if (slash == std::string::npos) {
            std::string seg = path.substr(start);
            if (!seg.empty()) parts.push_back(seg);
            break;
        }
        if (slash > start) parts.push_back(path.substr(start, slash - start));
        start = slash + 1;
    }
    return parts;
}

static std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string appendingApiSuffix(const std::string& baseUrl, const std::string& suffix) {
    auto parsed = parseUrl(baseUrl);
    if (!parsed.has_value()) return baseUrl;

    std::string trimmedPath = parsed->path;
    while (!trimmedPath.empty() && trimmedPath.back() == '/') trimmedPath.pop_back();
    const std::vector<std::string> baseParts = splitPath(trimmedPath);
    const std::vector<std::string> suffixParts = splitPath(suffix);

    // Longest overlapping tail-of-base == head-of-suffix (case-insensitive).
    std::vector<std::string> effectiveParts = baseParts;
    bool merged = false;
    const int maxOverlap = static_cast<int>(std::min(baseParts.size(), suffixParts.size()));
    for (int overlap = maxOverlap; overlap >= 1; --overlap) {
        bool same = true;
        for (int i = 0; i < overlap; ++i) {
            if (lower(baseParts[baseParts.size() - overlap + i]) != lower(suffixParts[i])) {
                same = false;
                break;
            }
        }
        if (same) {
            for (std::size_t i = overlap; i < suffixParts.size(); ++i) {
                effectiveParts.push_back(suffixParts[i]);
            }
            merged = true;
            break;
        }
    }
    if (!merged) {
        for (const auto& sp : suffixParts) effectiveParts.push_back(sp);
    }

    std::string newPath;
    for (const auto& part : effectiveParts) newPath += "/" + part;
    if (newPath.empty()) newPath = "/";
    ParsedUrl out = *parsed;
    out.path = newPath;
    return out.toString();
}

bool isLocalhostHost(const std::string& host) {
    std::string h = lower(host);
    if (!h.empty() && h.front() == '[' && h.back() == ']') {
        h = h.substr(1, h.size() - 2);
    }
    return h == "localhost" || h == "127.0.0.1" || h == "::1" || h == "0.0.0.0" ||
           (h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0);
}

std::optional<ProviderError> guardApiKeyTransport(const std::string& url, bool sendingApiKey) {
    if (!sendingApiKey) return std::nullopt;
    auto parsed = parseUrl(url);
    if (!parsed.has_value()) {
        return ProviderError::configuration("Invalid URL: " + url);
    }
    if (parsed->scheme != "https" && !isLocalhostHost(parsed->host)) {
        return ProviderError::auth(
            "Refusing to send API key over insecure connection (HTTP). Use HTTPS or a localhost URL.");
    }
    return std::nullopt;
}

std::string urlEncode(const std::string& value) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(value.size() * 3 / 2);
    for (unsigned char c : value) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0xF];
        }
    }
    return out;
}

}  // namespace tf
