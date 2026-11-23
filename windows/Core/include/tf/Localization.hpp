#pragma once
// tf/Localization.hpp — portable UI-string table + lookup (FR-37).
//
// PRD constraint: Build Tools has no XAML compiler / PRI pipeline, so WinUI
// pages are code-built and cannot use .resw. This module sidesteps PRI
// entirely: every language is a flat JSON object {key: "utf-8 value"}
// (windows/UI/Strings/<lang>.json, migrated from the macOS
// Localizable.strings by scripts/migrate_localizable.py; key names follow the
// macOS tables). At runtime the app loads the directory once, resolves the
// UI language (AppLanguage.h raw values, "system" deferred to a host-provided
// locale probe) and serves t(key) / tw(key) with the fallback chain
//   selected language -> English -> caller-supplied default.
// The caller-supplied default keeps every existing call site's current
// behavior when no tables are loaded (tests, smoke tools).
//
// Platform-clean: std + nlohmann only, no Win32/WinRT. Thread-safe: tables
// swap under a mutex; change listeners fire on the switching thread with no
// lock held (hosts hop to their own UI thread inside the listener).
#ifndef TF_LOCALIZATION_HPP
#define TF_LOCALIZATION_HPP

#include "AppLanguage.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace tf::core::loc {

/// UTF-8 <-> UTF-16 (surrogate-correct). Exported because Win32/WinUI call
/// sites need wchar_t while the tables (and macOS parity) are UTF-8.
std::wstring utf8ToWide(std::string_view utf8);
std::string wideToUtf8(std::wstring_view wide);

/// Replace occurrences of "%@" in order with `args` (macOS .strings format
/// marker, e.g. save.success.message = "Saved to %@").
std::string formatArgs(std::string_view pattern, const std::vector<std::string>& args);
std::wstring formatArgsWide(std::wstring_view pattern, const std::vector<std::wstring>& args);

/// One flat key -> UTF-8 value table parsed from a single <lang>.json.
class StringTable {
public:
    /// Parse a JSON object of string -> string. `error` (optional) receives a
    /// reason when the text is not JSON, not an object, or hold non-string
    /// values / empty keys; the table stays empty in that case.
    static StringTable fromJson(const std::string& jsonUtf8, std::string* error = nullptr);

    const std::string* find(const std::string& key) const;
    bool contains(const std::string& key) const { return map_.count(key) != 0; }
    std::size_t size() const { return map_.size(); }
    /// Sorted key list (schema comparison across languages).
    std::vector<std::string> keys() const;

private:
    std::map<std::string, std::string> map_;
};

/// Listener registration token (addListener / removeListener).
struct ListenerToken {
    std::uint64_t id = 0;
    explicit operator bool() const { return id != 0; }
};

/// Language registry + translator. Constructible standalone for tests;
/// the app uses instance().
class Localization {
public:
    static Localization& instance();

    Localization() = default;
    Localization(const Localization&) = delete;
    Localization& operator=(const Localization&) = delete;

    /// Load <raw>.json for every AppLanguage (en/zh-Hans/... — 10 files) from
    /// `dir`. Missing files other than en leave that language's table empty
    /// (lookup falls through to en); a missing/unparsable en.json returns
    /// false with `error` set (English is the fallback chain's middle hop).
    /// Existing tables for languages absent from the directory are cleared.
    bool loadDirectory(const std::filesystem::path& dir, std::string* error = nullptr);

    /// Test/app seam: install (or replace, empty table = clear) one language's
    /// table directly.
    void installTable(const std::string& langRaw, StringTable table);

    /// Host hook resolving "system": returns a BCL-47-ish locale id
    /// ("zh-CN", "de-DE"...) consumed by appLanguageFromLocale. Unset -> the
    /// resolved system language is English.
    void setSystemLocaleProvider(std::function<std::string()> provider);

    /// Select the UI language. "system" resolves through the provider;
    /// unknown raws are ignored (current language kept). Fires listeners when
    /// the resolved language changed.
    void setLanguage(const std::string& langRaw);

    /// Currently resolved language raw (never "system"; "en" before the first
    /// successful setLanguage).
    std::string currentLanguageRaw() const;

    /// Translate `key`: selected table -> en table -> `fallbackUtf8`.
    std::string t(const char* key, const char* fallbackUtf8 = "") const;

    /// Wide variant of t() (fallback converted the same way).
    std::wstring tw(const char* key, const wchar_t* fallbackWide = L"") const;

    /// True when `key` resolves from a loaded table (not the fallback).
    bool hasKey(const char* key) const;

    /// Language-change notifications. Fired on the setLanguage thread, no
    /// lock held, after the new tables are live.
    ListenerToken addListener(std::function<void()> listener);
    void removeListener(ListenerToken token);

private:
    mutable std::mutex mutex_;
    std::map<std::string, StringTable> tables_;  // key: language raw
    AppLanguage current_ = AppLanguage::English;
    std::function<std::string()> systemLocaleProvider_;
    std::map<std::uint64_t, std::function<void()>> listeners_;
    std::uint64_t nextListenerId_ = 1;
};

/// Convenience free function over the singleton (call sites read
/// loc::t("key", "默认文案")).
std::string t(const char* key, const char* fallbackUtf8 = "");
std::wstring tw(const char* key, const wchar_t* fallbackWide = L"");

}  // namespace tf::core::loc

#endif  // TF_LOCALIZATION_HPP
