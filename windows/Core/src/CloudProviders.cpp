// CloudProviders.cpp — Google v2 / DeepL / Baidu ports.
#include "tf/CloudProviders.hpp"
#include "tf/Json.hpp"

#include <cctype>
#include <cmath>
#include <cstring>
#include <random>

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

std::string lowerCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string upperCopy(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

/// Map an HTTP status onto the error taxonomy (brief §2: 网络/配额/鉴权/内容).
ProviderError statusError(const std::string& engine, int status, const std::string& body) {
    if (status == 401 || status == 403) {
        return ProviderError::auth(engine + " HTTP " + std::to_string(status));
    }
    if (status == 429) {
        return ProviderError::quota(engine + " rate limited (HTTP 429)");
    }
    if (status >= 500 || status == 0) {
        return ProviderError::network(engine + " HTTP " + std::to_string(status));
    }
    (void)body;
    return ProviderError::content(engine + " HTTP " + std::to_string(status));
}

}  // namespace

// ===========================================================================
// Google v2
// ===========================================================================
GoogleTranslateProvider::GoogleTranslateProvider(EngineConfig config,
                                                 CredentialStorePtr credentials,
                                                 HttpTransportPtr transport)
    : config_(std::move(config)), credentials_(std::move(credentials)),
      transport_(std::move(transport)) {}

bool GoogleTranslateProvider::isAvailable() const {
    return credentials_ && credentials_->has(credentialAccount(TranslationEngineType::Google));
}

std::string GoogleTranslateProvider::mapLanguageCode(const std::string& code) {
    const std::string l = lowerCopy(code);
    if (l == "zh-hant" || l == "zh-tw") return "zh-TW";
    if (l.empty() || l == "auto") return "auto";
    // Google v2 accepts base codes; strip region.
    const std::size_t dash = l.find('-');
    return dash == std::string::npos ? l : l.substr(0, dash);
}

TranslationOutcome GoogleTranslateProvider::translate(const TranslateRequest& request) {
    if (request.texts.empty()) return TranslationOutcome::ok({});
    for (const auto& t : request.texts) {
        if (trimCopy(t).empty()) {
            return TranslationOutcome::fail(ProviderError::content("Cannot translate empty text"));
        }
    }
    auto creds = credentials_->load(credentialAccount(TranslationEngineType::Google));
    if (!creds.has_value() || creds->apiKey.empty()) {
        return TranslationOutcome::fail(ProviderError::configuration("API key not configured"));
    }

    const std::string base = config_.baseURL.empty()
                                 ? translationEngineDefaultBaseUrl(TranslationEngineType::Google)
                                 : config_.baseURL;
    const std::string url =
        resolveLocalhost(base) + "?key=" + urlEncode(creds->apiKey);

    // v2 body: q[] array, format, source (omit when auto), target.
    Json body;
    body["q"] = request.texts;
    body["format"] = "text";
    const std::string source = mapLanguageCode(request.sourceLanguage);
    if (source != "auto" && !source.empty()) body["source"] = source;
    body["target"] = mapLanguageCode(request.targetLanguage);

    HttpRequest req;
    req.method = "POST";
    req.url = url;
    req.timeoutSeconds = static_cast<int>(config_.effectiveTimeout());
    req.cancelToken = request.cancelToken;
    req.headers["Content-Type"] = "application/json";
    req.body = body.dump();

    auto outcome = transport_->send(req);
    if (outcome.error.has_value()) return TranslationOutcome::fail(*outcome.error);
    const HttpResponse& resp = *outcome.response;
    if (!resp.ok()) {
        return TranslationOutcome::fail(statusError("Google", resp.status, resp.body));
    }
    Json json;
    try {
        json = Json::parse(resp.body);
    } catch (const Json::parse_error&) {
        return TranslationOutcome::fail(ProviderError::content("Invalid Google response"));
    }
    if (!json.contains("data") || !json["data"].is_object() ||
        !json["data"].contains("translations") || !json["data"]["translations"].is_array()) {
        return TranslationOutcome::fail(ProviderError::content("Invalid Google response"));
    }
    const auto& translations = json["data"]["translations"];
    std::vector<TranslationResult> results;
    for (std::size_t i = 0; i < request.texts.size(); ++i) {
        std::string translated;
        std::string detected;
        if (i < translations.size() && translations[i].is_object()) {
            if (translations[i].contains("translatedText") &&
                translations[i]["translatedText"].is_string()) {
                translated = translations[i]["translatedText"].get<std::string>();
            }
            if (translations[i].contains("detectedSourceLanguage") &&
                translations[i]["detectedSourceLanguage"].is_string()) {
                detected = translations[i]["detectedSourceLanguage"].get<std::string>();
            }
        }
        results.emplace_back(request.texts[i], std::move(translated),
                             detected.empty() ? (request.sourceLanguage.empty()
                                                     ? "Auto"
                                                     : request.sourceLanguage)
                                              : std::move(detected),
                             request.targetLanguage);
    }
    return TranslationOutcome::ok(std::move(results));
}

// ===========================================================================
// DeepL
// ===========================================================================
DeepLProvider::DeepLProvider(EngineConfig config, CredentialStorePtr credentials,
                             HttpTransportPtr transport)
    : config_(std::move(config)), credentials_(std::move(credentials)),
      transport_(std::move(transport)) {}

bool DeepLProvider::isAvailable() const {
    return credentials_ && credentials_->has(credentialAccount(TranslationEngineType::DeepL));
}

std::string DeepLProvider::normalizeEndpoint(const std::string& baseURL) {
    std::string raw = baseURL.empty()
                          ? translationEngineDefaultBaseUrl(TranslationEngineType::DeepL)
                          : baseURL;
    while (!raw.empty() && raw.back() == '/') raw.pop_back();
    const std::string lowered = lowerCopy(raw);
    if (lowered.size() >= 10 &&
        lowered.compare(lowered.size() - 10, 10, "/translate") == 0) {
        return raw;
    }
    return raw + "/translate";
}

std::string DeepLProvider::sourceCode(const std::string& code) {
    if (code.empty() || lowerCopy(code) == "auto") return "";  // omit → auto-detect
    return lowerCopy(code);
}

std::string DeepLProvider::targetCode(const std::string& code) {
    std::string c = upperCopy(code);
    if (c == "ZH-HANS" || c == "ZH") return "ZH";
    if (c == "ZH-HANT" || c == "ZH-TW") return "ZH-HANT";
    if (c == "EN-US" || c == "EN-GB" || c == "EN") return "EN";
    const std::size_t dash = c.find('-');
    return dash == std::string::npos ? c : c.substr(0, dash);
}

TranslationOutcome DeepLProvider::translate(const TranslateRequest& request) {
    if (request.texts.empty()) return TranslationOutcome::ok({});
    for (const auto& t : request.texts) {
        if (trimCopy(t).empty()) {
            return TranslationOutcome::fail(ProviderError::content("Cannot translate empty text"));
        }
    }
    auto creds = credentials_->load(credentialAccount(TranslationEngineType::DeepL));
    if (!creds.has_value() || creds->apiKey.empty()) {
        return TranslationOutcome::fail(
            ProviderError::configuration("DeepL API key not configured"));
    }

    Json body;
    body["text"] = request.texts;
    const std::string src = sourceCode(request.sourceLanguage);
    if (!src.empty()) body["source_lang"] = src;
    body["target_lang"] = targetCode(request.targetLanguage);

    HttpRequest req;
    req.method = "POST";
    req.url = resolveLocalhost(normalizeEndpoint(config_.baseURL));
    req.timeoutSeconds = static_cast<int>(config_.effectiveTimeout());
    req.cancelToken = request.cancelToken;
    req.headers["Content-Type"] = "application/json";
    req.headers["Authorization"] = "DeepL-Auth-Key " + creds->apiKey;
    req.body = body.dump();

    auto outcome = transport_->send(req);
    if (outcome.error.has_value()) return TranslationOutcome::fail(*outcome.error);
    const HttpResponse& resp = *outcome.response;
    if (!resp.ok()) {
        if (resp.status == 403) {
            return TranslationOutcome::fail(ProviderError::auth("DeepL: invalid API key"));
        }
        if (resp.status == 456) {
            return TranslationOutcome::fail(
                ProviderError::quota("DeepL: quota exceeded (HTTP 456)"));
        }
        return TranslationOutcome::fail(statusError("DeepL", resp.status, resp.body));
    }
    Json json;
    try {
        json = Json::parse(resp.body);
    } catch (const Json::parse_error&) {
        return TranslationOutcome::fail(ProviderError::content("Invalid DeepL response"));
    }
    if (!json.contains("translations") || !json["translations"].is_array()) {
        return TranslationOutcome::fail(ProviderError::content("Invalid DeepL response"));
    }
    const auto& translations = json["translations"];
    std::vector<TranslationResult> results;
    for (std::size_t i = 0; i < request.texts.size(); ++i) {
        std::string translated;
        std::string detected;
        if (i < translations.size() && translations[i].is_object()) {
            if (translations[i].contains("text") && translations[i]["text"].is_string()) {
                translated = translations[i]["text"].get<std::string>();
            }
            if (translations[i].contains("detected_source_language") &&
                translations[i]["detected_source_language"].is_string()) {
                detected = translations[i]["detected_source_language"].get<std::string>();
            }
        }
        results.emplace_back(request.texts[i], std::move(translated),
                             detected.empty() ? (request.sourceLanguage.empty() ? "Auto"
                                                                                : request.sourceLanguage)
                                              : std::move(detected),
                             request.targetLanguage);
    }
    return TranslationOutcome::ok(std::move(results));
}

// ===========================================================================
// Baidu
// ===========================================================================

// --- Minimal MD5 (RFC 1321) — public-domain style implementation, compact
// but complete; Baidu's sign is MD5 over UTF-8 bytes so no normalization.
namespace md5detail {

struct Context {
    std::uint32_t a{0x67452301}, b{0xefcdab89}, c{0x98badcfe}, d{0x10325476};
    std::uint64_t length = 0;
    std::uint8_t buffer[64]{};
    std::size_t buffered = 0;
};

inline std::uint32_t leftRotate(std::uint32_t x, int c) {
    return (x << c) | (x >> (32 - c));
}

inline void processBlock(Context& ctx, const std::uint8_t* p) {
    std::uint32_t m[16];
    for (int i = 0; i < 16; ++i) {
        m[i] = static_cast<std::uint32_t>(p[i * 4]) |
               (static_cast<std::uint32_t>(p[i * 4 + 1]) << 8) |
               (static_cast<std::uint32_t>(p[i * 4 + 2]) << 16) |
               (static_cast<std::uint32_t>(p[i * 4 + 3]) << 24);
    }
    const std::uint32_t S[64] = {
        7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20,  5, 9, 14, 20,  5, 9, 14, 20,  5, 9, 14, 20,
        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    const std::uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a,
        0xa8304613, 0xfd469501, 0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be,
        0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821, 0xf61e2562, 0xc040b340,
        0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8,
        0x676f02d9, 0x8d2a4c8a, 0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c,
        0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70, 0x289b7ec6, 0xeaa127fa,
        0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92,
        0xffeff47d, 0x85845dd1, 0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1,
        0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    std::uint32_t A = ctx.a, B = ctx.b, C = ctx.c, D = ctx.d;
    for (int i = 0; i < 64; ++i) {
        std::uint32_t F;
        int g;
        if (i < 16) {
            F = (B & C) | (~B & D);
            g = i;
        } else if (i < 32) {
            F = (D & B) | (~D & C);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            F = B ^ C ^ D;
            g = (3 * i + 5) % 16;
        } else {
            F = C ^ (B | ~D);
            g = (7 * i) % 16;
        }
        F = F + A + K[i] + m[g];
        A = D;
        D = C;
        C = B;
        B = B + leftRotate(F, S[i]);
    }
    ctx.a += A;
    ctx.b += B;
    ctx.c += C;
    ctx.d += D;
}

inline std::string digest(const std::string& input) {
    Context ctx;
    ctx.length = input.size() * 8;
    const std::uint8_t* data = reinterpret_cast<const std::uint8_t*>(input.data());

    std::size_t offset = 0;
    while (offset + 64 <= input.size()) {
        processBlock(ctx, data + offset);
        offset += 64;
    }
    std::uint8_t tail[128]{};
    const std::size_t rem = input.size() - offset;
    std::memcpy(tail, data + offset, rem);
    tail[rem] = 0x80;
    const bool needsExtra = rem >= 56;
    if (needsExtra) {
        processBlock(ctx, tail);
        std::memset(tail, 0, 64);
    }
    for (int i = 0; i < 8; ++i) {
        tail[56 + i] = static_cast<std::uint8_t>((ctx.length >> (8 * i)) & 0xFF);
    }
    processBlock(ctx, tail);

    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    const std::uint32_t words[4] = {ctx.a, ctx.b, ctx.c, ctx.d};
    for (std::uint32_t w : words) {
        for (int byte = 0; byte < 4; ++byte) {
            const std::uint8_t v = static_cast<std::uint8_t>((w >> (8 * byte)) & 0xFF);
            out += hex[v >> 4];
            out += hex[v & 0xF];
        }
    }
    return out;
}

}  // namespace md5detail

BaiduProvider::BaiduProvider(EngineConfig config, CredentialStorePtr credentials,
                             HttpTransportPtr transport)
    : config_(std::move(config)), credentials_(std::move(credentials)),
      transport_(std::move(transport)) {}

bool BaiduProvider::isAvailable() const {
    return credentials_ && credentials_->has(credentialAccount(TranslationEngineType::Baidu));
}

std::string BaiduProvider::mapLanguageCode(const std::string& code) {
    if (code.empty()) return "auto";
    const std::string l = lowerCopy(code);
    static const std::vector<std::pair<std::string, std::string>> kMap = {
        {"auto", "auto"},   {"en", "en"},         {"zh", "zh"},       {"zh-hans", "zh"},
        {"zh-cn", "zh"},    {"zh-hant", "cht"},   {"zh-tw", "cht"},   {"ja", "jp"},
        {"ko", "kor"},      {"fr", "fra"},        {"de", "de"},       {"es", "spa"},
        {"pt", "pt"},       {"ru", "ru"},         {"it", "it"},
    };
    for (const auto& [k, v] : kMap) {
        if (l == k) return v;
    }
    return code;
}

std::string BaiduProvider::md5Hex(const std::string& input) { return md5detail::digest(input); }

std::string BaiduProvider::makeSign(const std::string& appid, const std::string& q,
                                    const std::string& salt, const std::string& secretKey) {
    return md5Hex(appid + q + salt + secretKey);
}

TranslationOutcome BaiduProvider::translate(const TranslateRequest& request) {
    if (request.texts.empty()) return TranslationOutcome::ok({});
    auto creds = credentials_->load(credentialAccount(TranslationEngineType::Baidu));
    if (!creds.has_value() || creds->apiKey.empty() ||
        (creds->appID.empty() && creds->additional.count("appID") == 0)) {
        return TranslationOutcome::fail(
            ProviderError::configuration("AppID or Secret Key not configured"));
    }
    const std::string appID =
        !creds->appID.empty() ? creds->appID : creds->additional.at("appID");
    const std::string& secretKey = creds->apiKey;

    const std::string base = config_.baseURL.empty()
                                 ? translationEngineDefaultBaseUrl(TranslationEngineType::Baidu)
                                 : config_.baseURL;

    // One GET per text (Baidu q is single-string; Swift does the same).
    std::vector<TranslationResult> results;
    for (const auto& text : request.texts) {
        if (trimCopy(text).empty()) {
            return TranslationOutcome::fail(ProviderError::content("Cannot translate empty text"));
        }
        // Salt: 6-digit random (Swift Int.random(100000...999999)).
        thread_local std::mt19937 rng{std::random_device{}()};
        const std::string salt = std::to_string(100000 + rng() % 900000);
        const std::string sign = makeSign(appID, text, salt, secretKey);

        std::string url = resolveLocalhost(base);
        url += "?q=" + urlEncode(text) + "&from=" +
               urlEncode(mapLanguageCode(request.sourceLanguage)) + "&to=" +
               urlEncode(mapLanguageCode(request.targetLanguage)) + "&appid=" + urlEncode(appID) +
               "&salt=" + salt + "&sign=" + sign;

        HttpRequest req;
        req.method = "GET";
        req.url = url;
        req.timeoutSeconds = static_cast<int>(config_.effectiveTimeout());
        req.cancelToken = request.cancelToken;

        auto outcome = transport_->send(req);
        if (outcome.error.has_value()) return TranslationOutcome::fail(*outcome.error);
        const HttpResponse& resp = *outcome.response;
        Json json;
        try {
            json = Json::parse(resp.body);
        } catch (const Json::parse_error&) {
            return TranslationOutcome::fail(ProviderError::content("Invalid Baidu response"));
        }
        if (json.is_object() && json.contains("error_code") && json["error_code"].is_string()) {
            const std::string errorCode = json["error_code"].get<std::string>();
            std::string errorMsg =
                json.contains("error_msg") && json["error_msg"].is_string()
                    ? json["error_msg"].get<std::string>()
                    : "";
            if (errorCode == "54003") {
                return TranslationOutcome::fail(ProviderError::quota("Baidu rate limited"));
            }
            if (errorCode == "54004") {
                return TranslationOutcome::fail(
                    ProviderError::auth("Invalid Baidu AppID or Secret Key"));
            }
            return TranslationOutcome::fail(ProviderError::content("Baidu error: " + errorMsg));
        }
        if (!json.contains("trans_result") || !json["trans_result"].is_array() ||
            json["trans_result"].empty()) {
            return TranslationOutcome::fail(ProviderError::content("Invalid Baidu response"));
        }
        // Concatenate dst lines of this request (Swift joins trans_result).
        std::string translated;
        for (const auto& item : json["trans_result"]) {
            if (item.is_object() && item.contains("dst") && item["dst"].is_string()) {
                if (!translated.empty()) translated += "\n";
                translated += item["dst"].get<std::string>();
            }
        }
        results.emplace_back(text, std::move(translated),
                             request.sourceLanguage.empty() ? "Auto" : request.sourceLanguage,
                             request.targetLanguage);
    }
    return TranslationOutcome::ok(std::move(results));
}

}  // namespace tf
