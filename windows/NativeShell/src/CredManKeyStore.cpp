// CredManKeyStore.cpp — Credential Manager CredWrite/CredRead/CredDelete
// (NOT RUN on this macOS host; MSVC + Advapi32). Target name prefix:
// "TransFrame/" per brief §5.
#include "tf/CredManKeyStore.h"
#include "tf/Json.hpp"

#ifdef _WIN32

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wincred.h>

#pragma comment(lib, "Advapi32.lib")

namespace tf {

namespace {

std::wstring toWide(const std::string& utf8) {
    if (utf8.empty()) return std::wstring();
    const int len = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    std::wstring out(static_cast<std::size_t>(len), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, out.data(), len);
    out.resize(out.size() - 1);
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

constexpr const wchar_t* kTargetPrefix = L"TransFrame/";

std::wstring targetFor(const std::string& account) {
    return kTargetPrefix + toWide(account);
}

}  // namespace

void CredManKeyStore::save(const std::string& account, const StoredCredentials& credentials) {
    Json j;
    j["apiKey"] = credentials.apiKey;
    j["appID"] = credentials.appID;
    j["additional"] = credentials.additional;
    const std::string blob = j.dump();

    const std::wstring target = targetFor(account);
    // Replace semantics: delete-then-write (CredWrite fails on existing
    // unless CRED_PERSIST is updated via CredWrite with same type).
    CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);

    CREDENTIALW cred{};
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.UserName = const_cast<LPWSTR>(L"TransFrame");
    cred.CredentialBlobSize = static_cast<DWORD>(blob.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(blob.data()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    if (!CredWriteW(&cred, 0)) {
        // Surface as a configuration failure; the settings layer surfaces it.
        return;
    }
}

std::optional<StoredCredentials> CredManKeyStore::load(const std::string& account) const {
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(targetFor(account).c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        return std::nullopt;
    }
    std::string blob;
    if (raw->CredentialBlob && raw->CredentialBlobSize > 0) {
        blob.assign(reinterpret_cast<const char*>(raw->CredentialBlob),
                    raw->CredentialBlobSize);
    }
    CredFree(raw);
    try {
        const Json j = Json::parse(blob);
        StoredCredentials out;
        if (j.contains("apiKey") && j["apiKey"].is_string()) out.apiKey = j["apiKey"];
        if (j.contains("appID") && j["appID"].is_string()) out.appID = j["appID"];
        if (j.contains("additional") && j["additional"].is_object()) {
            for (auto it = j["additional"].begin(); it != j["additional"].end(); ++it) {
                if (it.value().is_string()) out.additional[it.key()] = it.value();
            }
        }
        return out;
    } catch (const Json::parse_error&) {
        // Legacy plain-key blob: treat the whole blob as the API key.
        StoredCredentials out;
        out.apiKey = blob;
        return out;
    }
}

bool CredManKeyStore::remove(const std::string& account) {
    return CredDeleteW(targetFor(account).c_str(), CRED_TYPE_GENERIC, 0) != 0;
}

bool CredManKeyStore::has(const std::string& account) const {
    PCREDENTIALW raw = nullptr;
    if (!CredReadW(targetFor(account).c_str(), CRED_TYPE_GENERIC, 0, &raw)) {
        return false;
    }
    CredFree(raw);
    return true;
}

}  // namespace tf

#endif  // _WIN32
