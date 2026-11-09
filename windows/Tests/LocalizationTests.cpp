// LocalizationTests.cpp — FR-37 Core Localization: parse / fallback chain /
// switch listeners / system-locale resolution / shipped-table schema.
//
// The free t()/tw() helpers go through the singleton: tests that touch it
// restore language + tables on scope exit so sibling test binaries are
// unaffected. Standalone Localization instances cover the rest.
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS  // std::getenv in candidateStringDirs()
#endif
#include <catch2/catch_amalgamated.hpp>

#include "tf/Localization.hpp"

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace fs = std::filesystem;
using tf::core::loc::Localization;
using tf::core::loc::StringTable;

namespace {

/// Directory holding the 10 shipped <lang>.json tables (test output copies
/// them next to the binary as Strings\*.json via the test vcxproj). macOS /
/// Linux CMake runs without the copy skip the schema section.
std::vector<fs::path> candidateStringDirs() {
    std::vector<fs::path> dirs;
    if (const char* env = std::getenv("TRANSFRAME_STRINGS_DIR")) {
        dirs.emplace_back(env);
    }
    std::error_code ec;
    // Content-copied next to the binary (vcxproj), cwd itself, or the source
    // tree (Windows test output lives under windows/build/…).
    dirs.emplace_back(fs::current_path() / "Strings");
    dirs.emplace_back(fs::current_path() / ".." / "Strings");
    dirs.emplace_back(fs::current_path() / ".." / ".." / ".." / "UI" / "Strings");
    std::vector<fs::path> existing;
    for (const auto& d : dirs) {
        if (fs::is_directory(d, ec)) existing.push_back(fs::weakly_canonical(d, ec));
    }
    return existing;
}

std::optional<std::string> readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::string data((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
    return data;
}

/// RAII: snapshot + restore singleton language/tables around a test body.
class SingletonGuard {
public:
    SingletonGuard() : raw_(Localization::instance().currentLanguageRaw()) {}
    ~SingletonGuard() {
        auto& loc = Localization::instance();
        // Clear every installable language, then restore the entry language.
        for (const char* lang : {"en", "zh-Hans", "de", "es", "fr", "it",
                                 "ja", "ko", "pt", "ru"}) {
            loc.installTable(lang, StringTable{});
        }
        loc.setSystemLocaleProvider(nullptr);
        loc.setLanguage(raw_);
    }

private:
    std::string raw_;
};

constexpr const char* kEn = R"({"k.a":"A","k.b":"B %@"})";
constexpr const char* kDe = R"({"k.a":"Ä"})";

}  // namespace

// ---------------------------------------------------------------------------
// codecs + format marker
// ---------------------------------------------------------------------------
TEST_CASE("utf8 <-> wide roundtrip covers CJK and surrogate pairs",
          "[loc][codec]") {
    const std::string mixed = "截图翻译\xF0\x9F\x98\x80 end";  // U+1F600
    const std::wstring wide = tf::core::loc::utf8ToWide(mixed);
    CHECK(wide.size() == 4 + 1 + 2 + 3);  // 4 CJK + space + surrogate pair + "end"
    CHECK(tf::core::loc::wideToUtf8(wide) == mixed);
    // Lone/incomplete bytes decode to U+FFFD, never throw.
    CHECK(tf::core::loc::utf8ToWide("\xC3").size() == 1);
}

TEST_CASE("formatArgs substitutes %@ in order and leaves extras alone",
          "[loc][format]") {
    using tf::core::loc::formatArgs;
    CHECK(formatArgs("no marker", {"x"}) == "no marker");
    CHECK(formatArgs("%@ + %@", {"a", "b"}) == "a + b");
    CHECK(formatArgs("%@ only", {"a", "unused"}) == "a only");
    CHECK(formatArgs("", {}) == "");
    CHECK(tf::core::loc::formatArgsWide(L"%@！", {L"值"}) == L"值！");
}

// ---------------------------------------------------------------------------
// table parsing
// ---------------------------------------------------------------------------
TEST_CASE("StringTable::fromJson accepts flat string objects", "[loc][table]") {
    std::string err;
    StringTable t = StringTable::fromJson(kEn, &err);
    CHECK(err.empty());
    CHECK(t.size() == 2);
    REQUIRE(t.find("k.a") != nullptr);
    CHECK(*t.find("k.a") == "A");
    CHECK(t.contains("k.b"));
    CHECK(t.keys() == std::vector<std::string>{"k.a", "k.b"});  // sorted
}

TEST_CASE("StringTable::fromJson rejects malformed inputs", "[loc][table]") {
    std::string err;
    CHECK(StringTable::fromJson("not json", &err).size() == 0);
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK(StringTable::fromJson(R"(["array"])", &err).size() == 0);  // not an object
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK(StringTable::fromJson(R"({"k": 42})", &err).size() == 0);  // non-string value
    CHECK_FALSE(err.empty());
    err.clear();
    CHECK(StringTable::fromJson(R"({"": "v"})", &err).size() == 0);  // empty key
    CHECK_FALSE(err.empty());
}

// ---------------------------------------------------------------------------
// fallback chain on a standalone instance
// ---------------------------------------------------------------------------
TEST_CASE("lookup falls back selected -> en -> caller default", "[loc][chain]") {
    Localization loc;
    std::string err;
    loc.installTable("en", StringTable::fromJson(kEn, &err));
    REQUIRE(err.empty());
    loc.installTable("de", StringTable::fromJson(kDe, &err));
    REQUIRE(err.empty());

    loc.setLanguage("de");
    CHECK(loc.t("k.a", "fb") == "Ä");        // de hit
    CHECK(loc.t("k.b", "fb") == "B %@");     // de miss -> en
    CHECK(loc.t("k.missing", "默认值") == "默认值");  // en miss -> default

    loc.setLanguage("fr");  // no table installed -> en + defaults
    CHECK(loc.t("k.a", "fb") == "A");
    CHECK(loc.t("k.missing", "fb") == "fb");

    CHECK(loc.hasKey("k.a"));
    CHECK_FALSE(loc.hasKey("k.missing"));
}

TEST_CASE("setLanguage ignores unknown raws (keeps current language)",
          "[loc][chain]") {
    Localization loc;
    loc.installTable("en", StringTable::fromJson(kEn));
    loc.installTable("zh-Hans", StringTable::fromJson(R"({"k.a":"中"})"));
    loc.setLanguage("zh-Hans");
    CHECK(loc.currentLanguageRaw() == "zh-Hans");
    CHECK(loc.t("k.a", "x") == "中");
    loc.setLanguage("klingon");  // unknown raw: ignored (keeps current language)
    CHECK(loc.currentLanguageRaw() == "zh-Hans");
    CHECK(loc.t("k.a", "x") == "中");
}

TEST_CASE("empty installed table clears a language (falls through to en)",
          "[loc][chain]") {
    Localization loc;
    loc.installTable("de", StringTable::fromJson(kDe));
    loc.installTable("en", StringTable::fromJson(kEn));
    loc.setLanguage("de");
    CHECK(loc.t("k.a", "fb") == "Ä");
    loc.installTable("de", StringTable{});  // clear
    CHECK(loc.t("k.a", "fb") == "A");       // now resolves via en
}

// ---------------------------------------------------------------------------
// switch listeners
// ---------------------------------------------------------------------------
TEST_CASE("change listeners fire on switch, not on same-language set",
          "[loc][listener]") {
    Localization loc;
    loc.installTable("en", StringTable::fromJson(kEn));
    loc.setLanguage("en");

    std::vector<std::string> fired;
    auto token = loc.addListener(
        [&] { fired.push_back(loc.currentLanguageRaw()); });
    REQUIRE(token);

    loc.setLanguage("en");
    CHECK(fired.empty());  // same language: no-op, no fire
    loc.setLanguage("de");
    REQUIRE(fired.size() == 1);
    CHECK(fired[0] == "de");
    loc.removeListener(token);
    loc.setLanguage("en");
    CHECK(fired.size() == 1);  // removed: silent
}

TEST_CASE("system language resolves through the provider hook", "[loc][system]") {
    SingletonGuard guard;
    auto& loc = Localization::instance();
    // Provider absent: "system" resolves to English (documented default).
    loc.setLanguage("de");
    loc.setLanguage("system");
    CHECK(loc.currentLanguageRaw() == "en");

    loc.setSystemLocaleProvider([] { return std::string("ja-JP"); });
    loc.setLanguage("system");
    CHECK(loc.currentLanguageRaw() == "ja");
    loc.setSystemLocaleProvider([] { return std::string("zh-CN"); });
    loc.setLanguage("system");
    CHECK(loc.currentLanguageRaw() == "zh-Hans");
    // Locale the raw table can't map -> en.
    loc.setSystemLocaleProvider([] { return std::string("xx-XX"); });
    loc.setLanguage("system");
    CHECK(loc.currentLanguageRaw() == "en");
}

// ---------------------------------------------------------------------------
// loadDirectory
// ---------------------------------------------------------------------------
TEST_CASE("loadDirectory requires en.json and reports missing dirs",
          "[loc][dir]") {
    Localization loc;
    std::string err;
    CHECK_FALSE(loc.loadDirectory(fs::path("Z:/definitely/not/here"), &err));
    CHECK_FALSE(err.empty());

    fs::path dir = fs::temp_directory_path() /
                   ("tf_loc_test_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    CHECK_FALSE(loc.loadDirectory(dir, &err));  // empty dir: no en.json
    CHECK_FALSE(err.empty());

    { std::ofstream(dir / "en.json") << kEn; }
    err.clear();
    CHECK(loc.loadDirectory(dir, &err));
    CHECK(err.empty());
    loc.setLanguage("en");
    CHECK(loc.t("k.a", "fb") == "A");

    { std::ofstream(dir / "de.json") << kDe; }
    CHECK(loc.loadDirectory(dir));
    loc.setLanguage("de");
    CHECK(loc.t("k.a", "fb") == "Ä");
    // Removing de.json from the dir clears the table (falls back to en).
    fs::remove(dir / "de.json");
    CHECK(loc.loadDirectory(dir));
    CHECK(loc.t("k.a", "fb") == "A");
    fs::remove_all(dir);
}

// ---------------------------------------------------------------------------
// singleton helpers keep working with no tables (existing-call-site contract)
// ---------------------------------------------------------------------------
TEST_CASE("free t()/tw() return the fallback when no tables are loaded",
          "[loc][singleton]") {
    SingletonGuard guard;
    auto& loc = Localization::instance();
    loc.setLanguage("de");
    CHECK(tf::core::loc::t("whatever.key", "兜底") == "兜底");
    CHECK(tf::core::loc::tw("whatever.key", L"兜底宽") == L"兜底宽");
    loc.installTable("de", StringTable::fromJson(kDe));
    CHECK(tf::core::loc::t("k.a", "fb") == "Ä");
    CHECK(tf::core::loc::tw("k.a", L"fb") == L"Ä");
}

// ---------------------------------------------------------------------------
// shipped-table schema: 10 files, identical key sets, non-empty values
// ---------------------------------------------------------------------------
TEST_CASE("shipped tables: 10 languages, identical key sets", "[loc][schema]") {
    const std::vector<fs::path> dirs = candidateStringDirs();
    if (dirs.empty()) {
        WARN("no Strings directory found next to the test binary — skipping "
             "shipped-table schema check (set TRANSFRAME_STRINGS_DIR to run)");
        return;
    }
    // Cross-check every candidate dir while we are at it (they should all be
    // the same generation); the schema holds for each.
    for (const fs::path& dir : dirs) {
        Localization loc;
        std::string err;
        INFO("dir: " << dir.string());
        REQUIRE(loc.loadDirectory(dir, &err));
        CHECK(err.empty());

        const std::vector<std::string> langs = {
            "en", "zh-Hans", "de", "es", "fr", "it", "ja", "ko", "pt", "ru"};
        std::optional<std::vector<std::string>> referenceKeys;
        for (const std::string& lang : langs) {
            const fs::path file = dir / (lang + ".json");
            INFO("file: " << file.string());
            const auto text = readFile(file);
            REQUIRE(text.has_value());
            std::string parseErr;
            const StringTable table = StringTable::fromJson(*text, &parseErr);
            CHECK(parseErr.empty());
            CHECK(table.size() >= 100);  // regression floor: 257 shipped today
            const std::vector<std::string> keys = table.keys();
            if (!referenceKeys) {
                referenceKeys = keys;
            } else {
                CHECK(keys == *referenceKeys);  // identical key sets across langs
            }
            // Every value is non-empty (schema rule from the migration script).
            for (const std::string& key : keys) {
                REQUIRE(table.find(key) != nullptr);
                CHECK_FALSE(table.find(key)->empty());
            }
        }
        // Spot values (en/ja) pin the macOS key-name reuse contract.
        loc.setLanguage("ja");
        CHECK(loc.t("win.tray.exit", "?") != "?");
        CHECK(loc.hasKey("settings.section.general"));
        CHECK(loc.hasKey("win.onboarding.applied"));
    }
}
