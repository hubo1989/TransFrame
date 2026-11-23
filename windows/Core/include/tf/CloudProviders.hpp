// tf/CloudProviders.hpp
// Google Cloud Translation v2 / DeepL / Baidu providers (ports of Swift
// GoogleTranslationProvider.swift, DeepLTranslationProvider.swift,
// BaiduTranslationProvider.swift).
#ifndef TF_CLOUD_PROVIDERS_HPP
#define TF_CLOUD_PROVIDERS_HPP

#include "tf/TranslationProvider.hpp"

namespace tf {

// ---------------------------------------------------------------------------
/// Google Cloud Translation API v2: POST with ?key= query param; response
/// data.translations[0].translatedText. No free probe endpoint (Swift).
class GoogleTranslateProvider final : public ITranslationProvider {
public:
    GoogleTranslateProvider(EngineConfig config, CredentialStorePtr credentials,
                            HttpTransportPtr transport);

    std::string id() const override { return "google"; }
    std::string name() const override { return "Google Translate"; }
    TranslationEngineType engineType() const override { return TranslationEngineType::Google; }
    bool isAvailable() const override;
    TranslationOutcome translate(const TranslateRequest& request) override;
    std::optional<ProviderError> verifyConnection(const CancellationToken&) override {
        return std::nullopt;  // no free probe (Swift health policy)
    }

    static std::string mapLanguageCode(const std::string& code);  // zh-Hant→zh-TW etc.

private:
    EngineConfig config_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
};

// ---------------------------------------------------------------------------
/// DeepL: POST /v2/translate (free tier via custom baseURL), Authorization:
/// DeepL-Auth-Key <key>; response translations[0].text. Paid verify — no free
/// probe (Swift policy).
class DeepLProvider final : public ITranslationProvider {
public:
    DeepLProvider(EngineConfig config, CredentialStorePtr credentials, HttpTransportPtr transport);

    std::string id() const override { return "deepl"; }
    std::string name() const override { return "DeepL"; }
    TranslationEngineType engineType() const override { return TranslationEngineType::DeepL; }
    bool isAvailable() const override;
    TranslationOutcome translate(const TranslateRequest& request) override;
    std::optional<ProviderError> verifyConnection(const CancellationToken&) override {
        return std::nullopt;  // verify would translate "1" (paid) — Swift policy
    }

    /// Endpoint normalization: append /translate unless already present.
    static std::string normalizeEndpoint(const std::string& baseURL);
    /// DeepL language casing: source lower-case, target UPPER-case (API rule).
    static std::string sourceCode(const std::string& code);
    static std::string targetCode(const std::string& code);

private:
    EngineConfig config_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
};

// ---------------------------------------------------------------------------
/// Baidu: GET with q/from/to/appid/salt/sign (MD5(appid+q+salt+key));
/// response trans_result[].dst. Error codes: 54003 → quota (rate limit),
/// 54004 → auth. No free probe (Swift policy).
class BaiduProvider final : public ITranslationProvider {
public:
    BaiduProvider(EngineConfig config, CredentialStorePtr credentials, HttpTransportPtr transport);

    std::string id() const override { return "baidu"; }
    std::string name() const override { return "Baidu Translate"; }
    TranslationEngineType engineType() const override { return TranslationEngineType::Baidu; }
    bool isAvailable() const override;
    TranslationOutcome translate(const TranslateRequest& request) override;
    std::optional<ProviderError> verifyConnection(const CancellationToken&) override {
        return std::nullopt;  // no free probe
    }

    /// Language mapping (Swift mapLanguageCode): zh-Hant→cht, ja→jp, ko→kor,
    /// fr→fra, es→spa.
    static std::string mapLanguageCode(const std::string& code);
    /// MD5 hex digest of the UTF-8 bytes (Baidu sign input string).
    static std::string md5Hex(const std::string& input);
    /// Salt generator surface for tests: sign = md5(appid + q + salt + key).
    static std::string makeSign(const std::string& appid, const std::string& q,
                                const std::string& salt, const std::string& secretKey);

private:
    EngineConfig config_;
    CredentialStorePtr credentials_;
    HttpTransportPtr transport_;
};

}  // namespace tf

#endif  // TF_CLOUD_PROVIDERS_HPP
