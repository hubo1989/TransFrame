// Localization.cpp — FR-37 portable string tables (see Localization.hpp).
#include "tf/Localization.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>

namespace tf::core::loc {

// ---------------------------------------------------------------------------
// UTF-8 <-> UTF-16
// ---------------------------------------------------------------------------

std::wstring utf8ToWide(std::string_view utf8) {
    std::wstring out;
    out.reserve(utf8.size());
    auto append = [&out](char32_t cp) {
        if (cp <= 0xFFFF) {
            out.push_back(static_cast<wchar_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
        }
    };
    for (std::size_t i = 0; i < utf8.size();) {
        const unsigned char c = static_cast<unsigned char>(utf8[i]);
        if (c < 0x80) {
            append(c);
            ++i;
            continue;
        }
        std::size_t len = 0;
        char32_t cp = 0;
        if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
        else { append(0xFFFD); ++i; continue; }  // stray continuation byte
        if (i + len > utf8.size()) { append(0xFFFD); break; }
        bool ok = true;
        for (std::size_t k = 1; k < len; ++k) {
            const unsigned char cc = static_cast<unsigned char>(utf8[i + k]);
            if ((cc & 0xC0) != 0x80) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (!ok || (len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) ||
            (len == 4 && cp < 0x10000) || cp > 0x10FFFF) {
            append(0xFFFD);
            ++i;
            continue;
        }
        append(cp);
        i += len;
    }
    return out;
}

std::string wideToUtf8(std::wstring_view wide) {
    std::string out;
    out.reserve(wide.size());
    auto append = [&out](char32_t cp) {
        if (cp <= 0x7F) {
            out.push_back(static_cast<char>(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    };
    for (std::size_t i = 0; i < wide.size(); ++i) {
        const wchar_t wc = wide[i];
        if (wc >= 0xD800 && wc <= 0xDBFF) {  // high surrogate
            if (i + 1 < wide.size() && wide[i + 1] >= 0xDC00 && wide[i + 1] <= 0xDFFF) {
                const char32_t cp = 0x10000 +
                    ((static_cast<char32_t>(wc) - 0xD800) << 10) +
                    (static_cast<char32_t>(wide[i + 1]) - 0xDC00);
                append(cp);
                ++i;
            } else {
                append(0xFFFD);  // unpaired
            }
        } else if (wc >= 0xDC00 && wc <= 0xDFFF) {
            append(0xFFFD);  // unpaired low surrogate
        } else {
            append(static_cast<char32_t>(wc));
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// %@ formatting (macOS .strings marker)
// ---------------------------------------------------------------------------

std::string formatArgs(std::string_view pattern, const std::vector<std::string>& args) {
    std::string out;
    std::size_t argIndex = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == '%' && i + 1 < pattern.size() && pattern[i + 1] == '@' &&
            argIndex < args.size()) {
            out += args[argIndex++];
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

std::wstring formatArgsWide(std::wstring_view pattern, const std::vector<std::wstring>& args) {
    std::wstring out;
    std::size_t argIndex = 0;
    for (std::size_t i = 0; i < pattern.size(); ++i) {
        if (pattern[i] == L'%' && i + 1 < pattern.size() && pattern[i + 1] == L'@' &&
            argIndex < args.size()) {
            out += args[argIndex++];
            ++i;
        } else {
            out += pattern[i];
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// StringTable
// ---------------------------------------------------------------------------

StringTable StringTable::fromJson(const std::string& jsonUtf8, std::string* error) {
    StringTable table;
    const auto fail = [error](const char* reason) {
        if (error) *error = reason;
        return StringTable{};
    };
    const nlohmann::json parsed = nlohmann::json::parse(jsonUtf8, nullptr, false);
    if (parsed.is_discarded()) return fail("not valid JSON");
    if (!parsed.is_object()) return fail("root is not an object");
    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        if (!it.value().is_string()) return fail("value is not a string");
        if (it.key().empty()) return fail("empty key");
        table.map_[it.key()] = it.value().get<std::string>();
    }
    return table;
}

const std::string* StringTable::find(const std::string& key) const {
    const auto it = map_.find(key);
    return it == map_.end() ? nullptr : &it->second;
}

std::vector<std::string> StringTable::keys() const {
    std::vector<std::string> out;
    out.reserve(map_.size());
    for (const auto& [k, v] : map_) {
        (void)v;
        out.push_back(k);
    }
    return out;  // std::map iteration is already sorted
}

// ---------------------------------------------------------------------------
// Localization
// ---------------------------------------------------------------------------

Localization& Localization::instance() {
    static Localization shared;
    return shared;
}

bool Localization::loadDirectory(const std::filesystem::path& dir, std::string* error) {
    std::map<std::string, StringTable> loaded;
    for (AppLanguage lang : kAllAppLanguages) {
        if (lang == AppLanguage::System) continue;
        const std::filesystem::path file = dir / (toRaw(lang) + ".json");
        std::error_code ec;
        if (!std::filesystem::exists(file, ec)) continue;  // en checked below
        std::string text;
        {
            std::ifstream in(file, std::ios::binary);
            if (!in) continue;
            char buf[4096];
            while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
                text.append(buf, static_cast<std::size_t>(in.gcount()));
                if (!in) break;  // partial final read
            }
        }
        std::string parseError;
        StringTable table = StringTable::fromJson(text, &parseError);
        if (!parseError.empty()) {
            if (error) {
                *error = toRaw(lang) + ".json: " + parseError;
                // Continue loading the rest; report the first defect only.
            }
            continue;
        }
        loaded[toRaw(lang)] = std::move(table);
    }
    if (loaded.find("en") == loaded.end()) {
        if (error) *error = "en.json missing or unparsable in " + dir.string();
        return false;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    tables_ = std::move(loaded);
    return true;
}

void Localization::installTable(const std::string& langRaw, StringTable table) {
    if (appLanguageFromRaw(langRaw) == AppLanguage::Invalid && langRaw != "en") return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (table.size() == 0) {
        tables_.erase(langRaw);
    } else {
        tables_[langRaw] = std::move(table);
    }
}

void Localization::setSystemLocaleProvider(std::function<std::string()> provider) {
    std::lock_guard<std::mutex> lock(mutex_);
    systemLocaleProvider_ = std::move(provider);
}

void Localization::setLanguage(const std::string& langRaw) {
    AppLanguage target = AppLanguage::Invalid;
    if (langRaw == "system") {
        std::function<std::string()> provider;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            provider = systemLocaleProvider_;
        }
        target = AppLanguage::English;
        if (provider) {
            if (const auto resolved = appLanguageFromLocale(provider())) {
                target = *resolved;
            }
        }
    } else {
        target = appLanguageFromRaw(langRaw);
    }
    if (target == AppLanguage::Invalid || target == AppLanguage::System) return;

    std::vector<std::function<void()>> toFire;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (target == current_) return;
        current_ = target;
        for (auto& [id, listener] : listeners_) toFire.push_back(listener);
    }
    for (auto& listener : toFire) listener();
}

std::string Localization::currentLanguageRaw() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return toRaw(current_);
}

std::string Localization::t(const char* key, const char* fallbackUtf8) const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Selected language -> en -> caller fallback (never inserts tables).
    const auto cur = tables_.find(toRaw(current_));
    if (cur != tables_.end()) {
        if (const std::string* hit = cur->second.find(key)) return *hit;
    }
    const auto en = tables_.find("en");
    if (en != tables_.end()) {
        if (const std::string* hit = en->second.find(key)) return *hit;
    }
    return std::string(fallbackUtf8 ? fallbackUtf8 : "");
}

std::wstring Localization::tw(const char* key, const wchar_t* fallbackWide) const {
    // Reuse t() for the lookup, convert once. t() takes its own lock; a
    // combined lock would duplicate the fallback logic instead.
    const std::string utf8 = t(key, "");
    if (!utf8.empty()) return utf8ToWide(utf8);
    return std::wstring(fallbackWide ? fallbackWide : L"");
}

bool Localization::hasKey(const char* key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto cur = tables_.find(toRaw(current_));
    if (cur != tables_.end() && cur->second.contains(key)) return true;
    const auto en = tables_.find("en");
    return en != tables_.end() && en->second.contains(key);
}

ListenerToken Localization::addListener(std::function<void()> listener) {
    std::lock_guard<std::mutex> lock(mutex_);
    const ListenerToken token{nextListenerId_++};
    listeners_[token.id] = std::move(listener);
    return token;
}

void Localization::removeListener(ListenerToken token) {
    if (!token) return;
    std::lock_guard<std::mutex> lock(mutex_);
    listeners_.erase(token.id);
}

std::string t(const char* key, const char* fallbackUtf8) {
    return Localization::instance().t(key, fallbackUtf8);
}

std::wstring tw(const char* key, const wchar_t* fallbackWide) {
    return Localization::instance().tw(key, fallbackWide);
}

}  // namespace tf::core::loc
