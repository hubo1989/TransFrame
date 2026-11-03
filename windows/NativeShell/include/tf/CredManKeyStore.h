// tf/CredManKeyStore.h — Windows Credential Manager implementation of
// ICredentialStore (brief §5: CredWrite/CredRead, "TransFrame/<engine>/<instance>").
#ifndef TF_CRED_MAN_KEY_STORE_H
#define TF_CRED_MAN_KEY_STORE_H

#ifdef _WIN32

#include "tf/TranslationProvider.hpp"

namespace tf {

/// Generic-credential storage. Blob = UTF-8 JSON of StoredCredentials so the
/// Baidu appID + additional fields survive the CredMan blob unchanged.
class CredManKeyStore final : public ICredentialStore {
public:
    CredManKeyStore() = default;

    void save(const std::string& account, const StoredCredentials& credentials) override;
    std::optional<StoredCredentials> load(const std::string& account) const override;
    bool remove(const std::string& account) override;
    bool has(const std::string& account) const override;
};

}  // namespace tf

#endif  // _WIN32
#endif  // TF_CRED_MAN_KEY_STORE_H
