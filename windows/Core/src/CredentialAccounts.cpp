// CredentialAccounts.cpp — credential account naming + key masking + config
// factories (brief §5: "TransFrame/<engine>/<instance>"; Swift
// KeychainService.serviceName / maskApiKey).
#include "tf/TranslationProvider.hpp"

namespace tf {

std::string credentialAccount(TranslationEngineType engine) {
    // Builtin engines: the raw engine string ("openai", "mtran", ...).
    return translationEngineRaw(engine);
}

std::string credentialAccountForCompatible(const std::string& instanceUuid) {
    // Compatible instances: "custom:<uuid>" (Swift compatibleServiceName —
    // stays stable across displayName edits).
    return "custom:" + instanceUuid;
}

std::string maskApiKey(const std::string& apiKey) {
    // "sk-abcdefghijklmnop" → "sk-****mnop" (first 3 + **** + last 4); short
    // keys collapse to "****" so nothing usable leaks.
    const std::size_t n = apiKey.size();
    if (n < 8) return "****";
    return apiKey.substr(0, 3) + "****" + apiKey.substr(n - 4);
}

EngineConfig EngineConfig::make(TranslationEngineType t) {
    EngineConfig c;
    c.engine = t;
    c.baseURL = translationEngineDefaultBaseUrl(t);
    c.modelName = translationEngineDefaultModelName(t);
    return c;
}

}  // namespace tf
