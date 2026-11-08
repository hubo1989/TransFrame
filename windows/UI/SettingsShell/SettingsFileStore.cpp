// windows/UI/SettingsShell/SettingsFileStore.cpp
// FR-35 设置页持久化粘合层实现（详见 .hpp 文件头）。
#include "SettingsFileStore.hpp"

#include "tf/EngineConfigStore.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace tf::ui::store {
namespace {

std::wstring EnvDir(const wchar_t* var, bool create) {
    const DWORD cap = ::GetEnvironmentVariableW(var, nullptr, 0);
    if (cap == 0) return {};
    std::wstring dir(cap, L'\0');
    const DWORD n = ::GetEnvironmentVariableW(var, dir.data(), cap);
    if (n == 0 || n >= cap) return {};
    dir.resize(n);
    dir += L"\\TransFrame";
    if (create) {
        std::error_code ec;
        std::filesystem::create_directories(dir, ec);  // 已存在不是错误
    }
    return dir;
}

std::wstring FilePath(const std::wstring& dir, const wchar_t* file) {
    if (dir.empty()) return {};
    return dir + L"\\" + file;
}

std::string ReadFileText(const std::wstring& path) {
    if (path.empty()) return {};
    std::ifstream in(std::filesystem::path(path), std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    if (in.bad()) return {};
    std::string out = ss.str();
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF &&
        static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);  // UTF-8 BOM（AppMain readTextFileUtf8 同语义）
    }
    return out;
}

nlohmann::json ParseObjectOrEmpty(const std::string& text) {
    nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) return nlohmann::json::object();
    return parsed;
}

std::string MergeTopKey(const std::string& existingUtf8, const std::string& key,
                        const nlohmann::json& value) {
    nlohmann::json root = ParseObjectOrEmpty(existingUtf8);
    root[key] = value;
    return root.dump(2) + "\n";
}

}  // namespace

std::wstring RoamingDir() {
    return EnvDir(L"APPDATA", /*create=*/true);
}

std::wstring LocalDir() {
    return EnvDir(L"LOCALAPPDATA", /*create=*/false);
}

std::string ReadEnginesJson() {
    return ReadFileText(FilePath(RoamingDir(), L"engines.json"));
}

std::string ReadSettingsJson() {
    return ReadFileText(FilePath(RoamingDir(), L"settings.json"));
}

bool WriteEnginesJsonAtomic(const std::string& jsonUtf8) {
    const std::wstring path = FilePath(RoamingDir(), L"engines.json");
    if (path.empty()) return false;
    return tf::core::writeEngineConfigFileAtomic(std::filesystem::path(path), jsonUtf8).ok;
}

bool WriteSettingsJsonAtomic(const std::string& jsonUtf8) {
    const std::wstring path = FilePath(RoamingDir(), L"settings.json");
    if (path.empty()) return false;
    return tf::core::writeEngineConfigFileAtomic(std::filesystem::path(path), jsonUtf8).ok;
}

std::optional<nlohmann::json> EnginesKey(const std::string& key) {
    nlohmann::json root = ParseObjectOrEmpty(ReadEnginesJson());
    if (!root.contains(key)) return std::nullopt;
    return root.at(key);
}

std::optional<nlohmann::json> SettingsKey(const std::string& key) {
    nlohmann::json root = ParseObjectOrEmpty(ReadSettingsJson());
    if (!root.contains(key)) return std::nullopt;
    return root.at(key);
}

std::string MergeEnginesKey(const std::string& enginesJsonUtf8, const std::string& key,
                            const nlohmann::json& value) {
    return MergeTopKey(enginesJsonUtf8, key, value);
}

std::string MergeSettingsKey(const std::string& settingsJsonUtf8, const std::string& key,
                             const nlohmann::json& value) {
    return MergeTopKey(settingsJsonUtf8, key, value);
}

}  // namespace tf::ui::store
