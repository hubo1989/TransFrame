// ImageUploadProvider.cpp — FR-31 protocol layer (pure logic, see header).
#include "tf/ImageUploadProvider.hpp"

#include "tf/Json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <vector>

namespace tf {

// ---------------------------------------------------------------------------
// Channel model
// ---------------------------------------------------------------------------
const char* imageUploadChannelRaw(ImageUploadChannel c) {
    switch (c) {
        case ImageUploadChannel::None: return "none";
        case ImageUploadChannel::PicGo: return "picgo";
        case ImageUploadChannel::AliyunOSS: return "aliyunOSS";
        case ImageUploadChannel::TencentCOS: return "tencentCOS";
        case ImageUploadChannel::QiniuKodo: return "qiniuKodo";
    }
    return "none";
}

std::optional<ImageUploadChannel> imageUploadChannelFromRaw(const std::string& raw) {
    if (raw == "none") return ImageUploadChannel::None;
    if (raw == "picgo") return ImageUploadChannel::PicGo;
    if (raw == "aliyunOSS") return ImageUploadChannel::AliyunOSS;
    if (raw == "tencentCOS") return ImageUploadChannel::TencentCOS;
    if (raw == "qiniuKodo") return ImageUploadChannel::QiniuKodo;
    return std::nullopt;
}

const char* imageUploadChannelDisplayName(ImageUploadChannel c) {
    switch (c) {
        case ImageUploadChannel::None: return "None (Disabled)";
        case ImageUploadChannel::PicGo: return "PicGo (Local App)";
        case ImageUploadChannel::AliyunOSS: return "Aliyun OSS";
        case ImageUploadChannel::TencentCOS: return "Tencent Cloud COS";
        case ImageUploadChannel::QiniuKodo: return "Qiniu Kodo";
    }
    return "";
}

std::string uploadCredentialAccount(ImageUploadChannel c) {
    switch (c) {
        case ImageUploadChannel::AliyunOSS: return "uploads/aliyunOSS";
        case ImageUploadChannel::TencentCOS: return "uploads/tencentCOS";
        case ImageUploadChannel::QiniuKodo: return "uploads/qiniuKodo";
        default: return {};
    }
}

std::string ImageUploadSecrets::secretFor(ImageUploadChannel c) const {
    switch (c) {
        case ImageUploadChannel::AliyunOSS: return ossAccessKeySecret;
        case ImageUploadChannel::TencentCOS: return cosSecretKey;
        case ImageUploadChannel::QiniuKodo: return kodoSecretKey;
        default: return {};
    }
}

// ---------------------------------------------------------------------------
// Crypto primitives: SHA-1 (FIPS 180-1) + HMAC (RFC 2104) + base64.
// Self-contained so Core stays dependency-free; test vectors pin the math.
// ---------------------------------------------------------------------------
namespace {

struct Sha1Digest {
    std::array<std::uint8_t, 20> bytes{};
};

std::uint32_t rol32(std::uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); }

Sha1Digest sha1(const std::uint8_t* data, std::size_t len) {
    std::uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE;
    std::uint32_t h3 = 0x10325476, h4 = 0xC3D2E1F0;

    // Message padding: 0x80, zeros, 64-bit big-endian bit length.
    std::vector<std::uint8_t> msg(data, data + len);
    msg.push_back(0x80);
    while (msg.size() % 64 != 56) msg.push_back(0);
    const std::uint64_t bitLen = static_cast<std::uint64_t>(len) * 8;
    for (int i = 7; i >= 0; --i) {
        msg.push_back(static_cast<std::uint8_t>((bitLen >> (i * 8)) & 0xFF));
    }

    for (std::size_t off = 0; off < msg.size(); off += 64) {
        std::uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<std::uint32_t>(msg[off + i * 4]) << 24) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 1]) << 16) |
                   (static_cast<std::uint32_t>(msg[off + i * 4 + 2]) << 8) |
                   static_cast<std::uint32_t>(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            w[i] = rol32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        }
        std::uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            std::uint32_t f, k;
            if (i < 20) { f = (b & c) | ((~b) & d); k = 0x5A827999; }
            else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
            else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
            else { f = b ^ c ^ d; k = 0xCA62C1D6; }
            const std::uint32_t t = rol32(a, 5) + f + e + k + w[i];
            e = d; d = c; c = rol32(b, 30); b = a; a = t;
        }
        h0 += a; h1 += b; h2 += c; h3 += d; h4 += e;
    }

    Sha1Digest out;
    const std::uint32_t hs[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out.bytes[static_cast<std::size_t>(i) * 4] =
            static_cast<std::uint8_t>(hs[i] >> 24);
        out.bytes[static_cast<std::size_t>(i) * 4 + 1] =
            static_cast<std::uint8_t>(hs[i] >> 16);
        out.bytes[static_cast<std::size_t>(i) * 4 + 2] =
            static_cast<std::uint8_t>(hs[i] >> 8);
        out.bytes[static_cast<std::size_t>(i) * 4 + 3] =
            static_cast<std::uint8_t>(hs[i]);
    }
    return out;
}

Sha1Digest sha1(std::string_view s) { return sha1(reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }

Sha1Digest hmacSha1(std::string_view key, std::string_view message) {
    constexpr std::size_t kBlock = 64;  // SHA-1 block size
    std::uint8_t keyBlock[kBlock]{};
    if (key.size() > kBlock) {
        const Sha1Digest hashed = sha1(key);
        std::memcpy(keyBlock, hashed.bytes.data(), hashed.bytes.size());
    } else {
        std::memcpy(keyBlock, key.data(), key.size());
    }
    std::uint8_t inner[kBlock], outer[kBlock];
    for (std::size_t i = 0; i < kBlock; ++i) {
        inner[i] = static_cast<std::uint8_t>(keyBlock[i] ^ 0x36);
        outer[i] = static_cast<std::uint8_t>(keyBlock[i] ^ 0x5C);
    }
    // inner = SHA1(innerPad || message); digest = SHA1(outerPad || inner)
    std::vector<std::uint8_t> innerInput(inner, inner + kBlock);
    innerInput.insert(innerInput.end(), message.begin(), message.end());
    const Sha1Digest innerHash = sha1(innerInput.data(), innerInput.size());
    std::vector<std::uint8_t> outerInput(outer, outer + kBlock);
    outerInput.insert(outerInput.end(), innerHash.bytes.begin(), innerHash.bytes.end());
    return sha1(outerInput.data(), outerInput.size());
}

std::string toHex(const std::uint8_t* bytes, std::size_t n) {
    static const char* digits = "0123456789abcdef";
    std::string out;
    out.reserve(n * 2);
    for (std::size_t i = 0; i < n; ++i) {
        out += digits[bytes[i] >> 4];
        out += digits[bytes[i] & 0xF];
    }
    return out;
}

}  // namespace

std::string sha1Hex(std::string_view message) {
    const Sha1Digest d = sha1(message);
    return toHex(d.bytes.data(), d.bytes.size());
}

std::string hmacSha1Hex(std::string_view key, std::string_view message) {
    const Sha1Digest d = hmacSha1(key, message);
    return toHex(d.bytes.data(), d.bytes.size());
}

std::string base64Encode(std::string_view bytes) {
    static const char* chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((bytes.size() + 2) / 3 * 4);
    std::size_t i = 0;
    while (i + 2 < bytes.size()) {
        const std::uint32_t n = (static_cast<std::uint8_t>(bytes[i]) << 16) |
                                (static_cast<std::uint8_t>(bytes[i + 1]) << 8) |
                                static_cast<std::uint8_t>(bytes[i + 2]);
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        out += chars[(n >> 6) & 63];
        out += chars[n & 63];
        i += 3;
    }
    if (i + 1 == bytes.size()) {
        const std::uint32_t n = static_cast<std::uint8_t>(bytes[i]) << 16;
        out += chars[(n >> 12) & 63];
        out += chars[(n >> 6) & 63];
        out += "==";
    } else if (i + 2 == bytes.size()) {
        const std::uint32_t n = (static_cast<std::uint8_t>(bytes[i]) << 16) |
                                (static_cast<std::uint8_t>(bytes[i + 1]) << 8);
        out += chars[(n >> 18) & 63];
        out += chars[(n >> 12) & 63];
        out += chars[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::string hmacSha1Base64(std::string_view key, std::string_view message) {
    const Sha1Digest d = hmacSha1(key, message);
    return base64Encode(std::string_view(reinterpret_cast<const char*>(d.bytes.data()),
                                         d.bytes.size()));
}

std::string urlSafeBase64(std::string_view bytes) {
    std::string out = base64Encode(bytes);
    std::replace(out.begin(), out.end(), '+', '-');
    std::replace(out.begin(), out.end(), '/', '_');
    return out;
}

std::string rfc1123Gmt(std::int64_t epochSeconds) {
    // kDays[0] = "Thu": 1970-01-01 (epoch day 0) was a Thursday.
    static const char* const kDays[] = {"Thu", "Fri", "Sat", "Sun", "Mon", "Tue", "Wed"};
    static const char* const kMonths[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                          "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const std::int64_t days = epochSeconds / 86400;
    const std::int64_t rem = epochSeconds % 86400;
    const std::int64_t weekday = ((days % 7) + 7) % 7;
    // Convert days since 1970-01-01 to a civil date (Howard Hinnant's
    // algorithm; March-based so leap days fall at the end of the year).
    const std::int64_t z = days + 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    const std::int64_t year = (m <= 2) ? y + 1 : y;
    char buf[40];
    std::snprintf(buf, sizeof(buf), "%s, %02d %s %04d %02d:%02d:%02d GMT",
                  kDays[static_cast<std::size_t>(weekday)], static_cast<int>(d),
                  kMonths[static_cast<std::size_t>(m - 1)], static_cast<int>(year),
                  static_cast<int>(rem / 3600), static_cast<int>((rem / 60) % 60),
                  static_cast<int>(rem % 60));
    return buf;
}

std::string uploadFilename(std::int64_t epochSeconds, const std::string& tag) {
    return "TF_" + std::to_string(epochSeconds) + "_" + tag + ".png";
}

// ---------------------------------------------------------------------------
// Settings codec (settings.json section, key = macOS UserDefaults key)
// ---------------------------------------------------------------------------
namespace {

constexpr const char* kSettingsKey = "TransFrame_ImageHostSettings";

std::string trimmedSlashes(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == '/' || s[b] == '\\')) ++b;
    while (e > b && (s[e - 1] == '/' || s[e - 1] == '\\')) --e;
    return s.substr(b, e - b);
}

std::string stringField(const Json& node, const char* field, const std::string& fallback) {
    if (!node.is_object() || !node.contains(field)) return fallback;
    const Json& v = node.at(field);
    return v.is_string() ? v.get<std::string>() : fallback;
}

bool boolField(const Json& node, const char* field, bool fallback) {
    if (!node.is_object() || !node.contains(field)) return fallback;
    const Json& v = node.at(field);
    return v.is_boolean() ? v.get<bool>() : fallback;
}

void writeIfChanged(Json& node, const char* field, const std::string& value) {
    node[field] = value;
}

}  // namespace

ImageUploadConfig parseImageUploadConfig(const std::string& settingsJsonUtf8,
                                         ImageUploadSecrets* legacySecrets) {
    ImageUploadConfig config;
    const Json root = Json::parse(settingsJsonUtf8, nullptr, false);
    if (root.is_discarded() || !root.is_object() || !root.contains(kSettingsKey)) {
        return config;
    }
    const Json& node = root.at(kSettingsKey);
    if (!node.is_object()) return config;

    if (const auto host = imageUploadChannelFromRaw(stringField(node, "activeHost", "none"))) {
        config.activeHost = *host;
    }
    config.picgoEndpoint = stringField(node, "picgoEndpoint", config.picgoEndpoint);
    config.copyMarkdownFormat = boolField(node, "copyMarkdownFormat", true);

    config.ossBucket = stringField(node, "ossBucket", "");
    config.ossEndpoint = stringField(node, "ossEndpoint", "");
    config.ossAccessKeyId = stringField(node, "ossAccessKeyId", "");
    config.ossPathPrefix = stringField(node, "ossPathPrefix", config.ossPathPrefix);

    config.cosBucket = stringField(node, "cosBucket", "");
    config.cosRegion = stringField(node, "cosRegion", "");
    config.cosSecretId = stringField(node, "cosSecretId", "");
    config.cosPathPrefix = stringField(node, "cosPathPrefix", config.cosPathPrefix);

    config.kodoBucket = stringField(node, "kodoBucket", "");
    config.kodoDomain = stringField(node, "kodoDomain", "");
    config.kodoAccessKey = stringField(node, "kodoAccessKey", "");
    config.kodoPathPrefix = stringField(node, "kodoPathPrefix", config.kodoPathPrefix);

    // Legacy plaintext secrets (pre-CredMan builds / hand-edited files) are
    // surfaced once so the App can migrate them; never a requirement.
    if (legacySecrets) {
        legacySecrets->ossAccessKeySecret = stringField(node, "ossAccessKeySecret", "");
        legacySecrets->cosSecretKey = stringField(node, "cosSecretKey", "");
        legacySecrets->kodoSecretKey = stringField(node, "kodoSecretKey", "");
    }
    return config;
}

std::string serializeImageUploadConfig(const std::string& existingJsonUtf8,
                                       const ImageUploadConfig& config) {
    Json root = Json::object();
    if (!existingJsonUtf8.empty()) {
        const Json parsed = Json::parse(existingJsonUtf8, nullptr, false);
        if (!parsed.is_discarded() && parsed.is_object()) root = parsed;
    }
    Json node = Json::object();
    if (root.contains(kSettingsKey) && root.at(kSettingsKey).is_object()) {
        node = root.at(kSettingsKey);
    }
    node["activeHost"] = imageUploadChannelRaw(config.activeHost);
    node["picgoEndpoint"] = config.picgoEndpoint;
    node["copyMarkdownFormat"] = config.copyMarkdownFormat;
    node["ossBucket"] = config.ossBucket;
    node["ossEndpoint"] = config.ossEndpoint;
    node["ossAccessKeyId"] = config.ossAccessKeyId;
    node["ossPathPrefix"] = config.ossPathPrefix;
    node["cosBucket"] = config.cosBucket;
    node["cosRegion"] = config.cosRegion;
    node["cosSecretId"] = config.cosSecretId;
    node["cosPathPrefix"] = config.cosPathPrefix;
    node["kodoBucket"] = config.kodoBucket;
    node["kodoDomain"] = config.kodoDomain;
    node["kodoAccessKey"] = config.kodoAccessKey;
    node["kodoPathPrefix"] = config.kodoPathPrefix;
    // Sanitized: secrets never persist (Swift blanks them in saveSettings).
    node["ossAccessKeySecret"] = "";
    node["cosSecretKey"] = "";
    node["kodoSecretKey"] = "";
    root[kSettingsKey] = node;
    return root.dump(2) + "\n";
}

// ---------------------------------------------------------------------------
// Object keys / URLs
// ---------------------------------------------------------------------------
std::string uploadObjectKey(const ImageUploadConfig& config, ImageUploadChannel channel,
                            const std::string& filename) {
    std::string prefix;
    switch (channel) {
        case ImageUploadChannel::AliyunOSS: prefix = config.ossPathPrefix; break;
        case ImageUploadChannel::TencentCOS: prefix = config.cosPathPrefix; break;
        case ImageUploadChannel::QiniuKodo: prefix = config.kodoPathPrefix; break;
        default: break;
    }
    prefix = trimmedSlashes(prefix);
    return prefix.empty() ? filename : prefix + "/" + filename;
}

std::string uploadPublicUrl(const ImageUploadConfig& config, ImageUploadChannel channel,
                            const std::string& filename) {
    switch (channel) {
        case ImageUploadChannel::AliyunOSS: {
            const std::string endpoint = trimmedSlashes(config.ossEndpoint);
            if (config.ossBucket.empty() || endpoint.empty()) return {};
            return "https://" + config.ossBucket + "." + endpoint + "/" +
                   uploadObjectKey(config, channel, filename);
        }
        case ImageUploadChannel::TencentCOS: {
            if (config.cosBucket.empty() || config.cosRegion.empty()) return {};
            return "https://" + config.cosBucket + ".cos." + config.cosRegion +
                   ".myqcloud.com/" + uploadObjectKey(config, channel, filename);
        }
        case ImageUploadChannel::QiniuKodo: {
            std::string domain = trimmedSlashes(config.kodoDomain);
            if (domain.empty()) return {};
            if (domain.rfind("http://", 0) != 0 && domain.rfind("https://", 0) != 0) {
                domain = "https://" + domain;
            }
            return domain + "/" + uploadObjectKey(config, channel, filename);
        }
        default: return {};
    }
}

// ---------------------------------------------------------------------------
// Request builders
// ---------------------------------------------------------------------------
HttpRequest buildPicGoUploadRequest(const std::string& endpoint,
                                    const std::string& tempFilePathUtf8) {
    // resolveLocalhost keeps 127.0.0.1 fast on IPv6-first stacks (Swift
    // URL.resolvingLocalhost analogue), mirroring the provider layer.
    std::string url = endpoint;
    if (auto parsed = parseUrl(url)) {
        if (parsed->host == "localhost") url = resolveLocalhost(url);
    }
    HttpRequest request;
    request.method = "POST";
    request.url = url;
    request.headers["Content-Type"] = "application/json";
    request.body = Json{{"list", Json::array({tempFilePathUtf8})}}.dump();
    request.timeoutSeconds = 30;  // local app but may be slow on big files
    return request;
}

HttpRequest buildOssPutObjectRequest(const ImageUploadConfig& config,
                                     const std::string& accessKeySecret,
                                     std::string_view imageBytes,
                                     const std::string& filename,
                                     std::int64_t nowEpochSeconds) {
    const std::string endpoint = trimmedSlashes(config.ossEndpoint);
    const std::string host = config.ossBucket + "." + endpoint;
    const std::string key = uploadObjectKey(config, ImageUploadChannel::AliyunOSS, filename);
    const std::string date = rfc1123Gmt(nowEpochSeconds);
    const std::string contentType = "image/png";

    // Signature v1: "PUT\n\n<contentType>\n<date>\n/<bucket>/<key>" (no
    // Content-MD5, no canonicalized OSS headers — the Swift shape).
    const std::string stringToSign =
        "PUT\n\n" + contentType + "\n" + date + "\n/" + config.ossBucket + "/" + key;
    const std::string signature = hmacSha1Base64(accessKeySecret, stringToSign);

    HttpRequest request;
    request.method = "PUT";
    request.url = "https://" + host + "/" + key;
    request.headers["Host"] = host;
    request.headers["Date"] = date;
    request.headers["Content-Type"] = contentType;
    request.headers["Authorization"] =
        "OSS " + config.ossAccessKeyId + ":" + signature;
    request.body.assign(imageBytes.begin(), imageBytes.end());
    request.timeoutSeconds = 60;  // large PNG uploads
    return request;
}

HttpRequest buildCosPutObjectRequest(const ImageUploadConfig& config,
                                     const std::string& secretKey,
                                     std::string_view imageBytes,
                                     const std::string& filename,
                                     std::int64_t nowEpochSeconds) {
    const std::string host =
        config.cosBucket + ".cos." + config.cosRegion + ".myqcloud.com";
    const std::string key = uploadObjectKey(config, ImageUploadChannel::TencentCOS, filename);
    const std::int64_t expire = nowEpochSeconds + 3600;
    const std::string keyTime =
        std::to_string(nowEpochSeconds) + ";" + std::to_string(expire);

    // q-sign-algorithm=sha1 chain (XML API signature):
    //   signKey     = hex(hmac_sha1(secretKey, keyTime))
    //   formatString = "put\n/<key>\n\nhost=<host>\n"
    //   stringToSign = "sha1\n<keyTime>\n" + hex(sha1(formatString)) + "\n"
    //   signature   = hex(hmac_sha1(signKey, stringToSign))
    const std::string signKey = hmacSha1Hex(secretKey, keyTime);
    const std::string formatString = "put\n/" + key + "\n\nhost=" + host + "\n";
    const std::string stringToSign =
        "sha1\n" + keyTime + "\n" + sha1Hex(formatString) + "\n";
    const std::string signature = hmacSha1Hex(signKey, stringToSign);

    HttpRequest request;
    request.method = "PUT";
    request.url = "https://" + host + "/" + key;
    request.headers["Host"] = host;
    request.headers["Content-Type"] = "image/png";
    request.headers["Authorization"] =
        "q-sign-algorithm=sha1&q-ak=" + config.cosSecretId +
        "&q-sign-time=" + keyTime + "&q-key-time=" + keyTime +
        "&q-header-list=host&q-url-param-list=&q-signature=" + signature;
    request.body.assign(imageBytes.begin(), imageBytes.end());
    request.timeoutSeconds = 60;
    return request;
}

HttpRequest buildKodoFormUploadRequest(const ImageUploadConfig& config,
                                       const std::string& secretKey,
                                       std::string_view imageBytes,
                                       const std::string& filename,
                                       std::int64_t nowEpochSeconds) {
    const std::string key =
        uploadObjectKey(config, ImageUploadChannel::QiniuKodo, filename);
    const std::int64_t deadline = nowEpochSeconds + 3600;

    // Upload token: ak:urlsafe_b64(hmac_sha1(sk, urlsafe_b64(policy))) — the
    // signature covers exactly the encodedPolicy bytes sent in the form.
    const std::string policy = "{\"deadline\":" + std::to_string(deadline) +
                               ",\"scope\":\"" + config.kodoBucket + ":" + key + "\"}";
    const std::string encodedPolicy = urlSafeBase64(policy);
    const std::string sign =
        urlSafeBase64(hmacSha1Base64(secretKey, encodedPolicy));
    const std::string token = config.kodoAccessKey + ":" + sign + ":" + encodedPolicy;

    // Deterministic boundary from the request identity (stable for tests).
    const std::string boundary =
        "TransFrameForm" + sha1Hex(key + std::to_string(deadline)).substr(0, 16);
    const std::string fileField = filename;
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"token\"\r\n\r\n";
    body += token + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"key\"\r\n\r\n";
    body += key + "\r\n";
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"" + fileField +
            "\"\r\n";
    body += "Content-Type: image/png\r\n\r\n";
    body.append(imageBytes.begin(), imageBytes.end());
    body += "\r\n";
    body += "--" + boundary + "--\r\n";

    HttpRequest request;
    request.method = "POST";
    request.url = "https://upload.qiniup.com";
    request.headers["Content-Type"] = "multipart/form-data; boundary=" + boundary;
    request.body = std::move(body);
    request.timeoutSeconds = 60;
    return request;
}

// ---------------------------------------------------------------------------
// Response parsing / error classification
// ---------------------------------------------------------------------------
PicGoParseOutcome parsePicGoResponse(const HttpResponse& response) {
    PicGoParseOutcome out;
    if (response.status < 200 || response.status >= 300) {
        if (response.status >= 500) {
            out.error = ProviderError::network("PicGo HTTP " +
                                               std::to_string(response.status));
        } else {
            out.error = ProviderError::content("PicGo HTTP " +
                                               std::to_string(response.status));
        }
        return out;
    }
    const Json body = Json::parse(response.body, nullptr, false);
    if (!body.is_discarded() && body.is_object() && body.contains("result") &&
        body.at("result").is_array() && !body.at("result").empty()) {
        const Json& first = body.at("result").front();
        if (first.is_string() && !first.get<std::string>().empty()) {
            out.url = first.get<std::string>();
            return out;
        }
    }
    out.error = ProviderError::content("unexpected PicGo response payload");
    return out;
}

std::string cloudXmlErrorCode(const std::string& body) {
    // Best-effort "<Error><Code>x</Code>" extraction; returns "" when absent.
    const std::size_t codeOpen = body.find("<Code>");
    if (codeOpen == std::string::npos) return {};
    const std::size_t start = codeOpen + 6;
    const std::size_t close = body.find("</Code>", start);
    if (close == std::string::npos) return {};
    return body.substr(start, close - start);
}

ProviderError classifyUploadResponse(const HttpResponse& response, const char* provider) {
    const std::string code = cloudXmlErrorCode(response.body);
    std::string message = std::string(provider) + " HTTP " + std::to_string(response.status);
    if (!code.empty()) message += " " + code;
    if (response.status == 401 || response.status == 403) {
        return ProviderError::auth(message);
    }
    if (response.status == 429) {
        return ProviderError::quota(message);
    }
    if (response.status >= 500) {
        return ProviderError::network(message);
    }
    // Qiniu errors ride JSON {"error": "..."} — fold into the message too.
    const Json body = Json::parse(response.body, nullptr, false);
    if (!body.is_discarded() && body.is_object() && body.contains("error") &&
        body.at("error").is_string()) {
        message += " " + body.at("error").get<std::string>();
    }
    return ProviderError::content(message);
}

// ---------------------------------------------------------------------------
// Service
// ---------------------------------------------------------------------------
ImageUploadService::ImageUploadService(HttpTransportPtr transport, TempFileProvider tempFiles,
                                       TempFileRemover tempRemover)
    : transport_(std::move(transport)), tempFiles_(std::move(tempFiles)),
      tempRemover_(std::move(tempRemover)) {}

namespace {

std::optional<ProviderError> missingFieldError(const char* channel, const char* field) {
    return ProviderError::configuration(std::string(channel) + " is missing " + field +
                                        " — configure it in Settings first");
}

}  // namespace

std::optional<ProviderError> validateUploadConfig(const ImageUploadConfig& config,
                                                  const ImageUploadSecrets& secrets) {
    switch (config.activeHost) {
        case ImageUploadChannel::None:
            return ProviderError::configuration("no image host configured");
        case ImageUploadChannel::PicGo:
            if (config.picgoEndpoint.empty()) {
                return missingFieldError("PicGo", "endpoint");
            }
            if (auto parsed = parseUrl(config.picgoEndpoint); !parsed.has_value()) {
                return ProviderError::configuration("invalid PicGo endpoint");
            }
            return std::nullopt;
        case ImageUploadChannel::AliyunOSS:
            if (config.ossBucket.empty()) return missingFieldError("Aliyun OSS", "bucket");
            if (config.ossEndpoint.empty()) return missingFieldError("Aliyun OSS", "endpoint");
            if (config.ossAccessKeyId.empty()) return missingFieldError("Aliyun OSS", "AccessKeyId");
            if (secrets.ossAccessKeySecret.empty()) {
                return ProviderError::auth("missing API key for Aliyun OSS; save one in Settings first");
            }
            return std::nullopt;
        case ImageUploadChannel::TencentCOS:
            if (config.cosBucket.empty()) return missingFieldError("Tencent COS", "bucket");
            if (config.cosRegion.empty()) return missingFieldError("Tencent COS", "region");
            if (config.cosSecretId.empty()) return missingFieldError("Tencent COS", "SecretId");
            if (secrets.cosSecretKey.empty()) {
                return ProviderError::auth("missing API key for Tencent Cloud COS; save one in Settings first");
            }
            return std::nullopt;
        case ImageUploadChannel::QiniuKodo:
            if (config.kodoBucket.empty()) return missingFieldError("Qiniu Kodo", "bucket");
            if (config.kodoDomain.empty()) return missingFieldError("Qiniu Kodo", "domain");
            if (config.kodoAccessKey.empty()) return missingFieldError("Qiniu Kodo", "AccessKey");
            if (secrets.kodoSecretKey.empty()) {
                return ProviderError::auth("missing API key for Qiniu Kodo; save one in Settings first");
            }
            return std::nullopt;
    }
    return ProviderError::configuration("unknown image host");
}

ImageUploadOutcome ImageUploadService::upload(const ImageUploadConfig& config,
                                              const ImageUploadSecrets& secrets,
                                              std::string_view pngBytes,
                                              const std::string& filename,
                                              std::int64_t nowEpochSeconds,
                                              const CancellationToken& cancelToken) {
    if (auto invalid = validateUploadConfig(config, secrets)) {
        return ImageUploadOutcome::fail(*invalid);
    }
    if (!transport_) {
        return ImageUploadOutcome::fail(ProviderError::configuration("no HTTP transport"));
    }

    HttpRequest request;
    std::string tempFile;
    switch (config.activeHost) {
        case ImageUploadChannel::PicGo: {
            if (!tempFiles_) {
                return ImageUploadOutcome::fail(
                    ProviderError::configuration("PicGo needs a temp-file provider"));
            }
            tempFile = tempFiles_(filename, pngBytes);
            if (tempFile.empty()) {
                return ImageUploadOutcome::fail(
                    ProviderError::network("PicGo temp file write failed"));
            }
            request = buildPicGoUploadRequest(config.picgoEndpoint, tempFile);
            break;
        }
        case ImageUploadChannel::AliyunOSS:
            request = buildOssPutObjectRequest(config, secrets.ossAccessKeySecret,
                                               pngBytes, filename, nowEpochSeconds);
            break;
        case ImageUploadChannel::TencentCOS:
            request = buildCosPutObjectRequest(config, secrets.cosSecretKey, pngBytes,
                                               filename, nowEpochSeconds);
            break;
        case ImageUploadChannel::QiniuKodo:
            request = buildKodoFormUploadRequest(config, secrets.kodoSecretKey, pngBytes,
                                                 filename, nowEpochSeconds);
            break;
        default:
            return ImageUploadOutcome::fail(ProviderError::configuration("no image host configured"));
    }
    request.cancelToken = cancelToken;

    const HttpOutcome outcome = transport_->send(request);
    if (!tempFile.empty() && tempRemover_) {
        tempRemover_(tempFile);  // Swift defer { removeItem } parity
    }
    if (outcome.error.has_value()) {
        return ImageUploadOutcome::fail(*outcome.error);
    }
    const HttpResponse& response = *outcome.response;

    if (config.activeHost == ImageUploadChannel::PicGo) {
        const PicGoParseOutcome parsed = parsePicGoResponse(response);
        if (parsed.error.has_value()) {
            return ImageUploadOutcome::fail(*parsed.error);
        }
        ImageUploadResult result;
        result.url = *parsed.url;
        result.filename = filename;
        result.provider = config.activeHost;
        result.markdown = "![" + filename + "](" + result.url + ")";
        return ImageUploadOutcome::ok(std::move(result));
    }

    if (!response.ok()) {
        return ImageUploadOutcome::fail(
            classifyUploadResponse(response, imageUploadChannelDisplayName(config.activeHost)));
    }

    ImageUploadResult result;
    result.url = uploadPublicUrl(config, config.activeHost, filename);
    result.filename = filename;
    result.provider = config.activeHost;
    result.markdown = "![" + filename + "](" + result.url + ")";
    return ImageUploadOutcome::ok(std::move(result));
}

std::string ImageUploadService::clipboardText(const ImageUploadConfig& config,
                                              const ImageUploadResult& result) {
    return config.copyMarkdownFormat ? result.markdown : result.url;
}

}  // namespace tf
