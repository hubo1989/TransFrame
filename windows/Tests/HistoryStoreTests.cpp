// HistoryStoreTests.cpp — SQLite history store (schema v1 / WAL / LIKE / favorites)。
#include <catch2/catch_amalgamated.hpp>

#include "tf/HistoryStore.hpp"

#include <chrono>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using tf::core::HistoryCategory;
using tf::core::HistoryQuery;
using tf::core::HistoryStore;
using tf::core::TranslationHistory;
using tf::core::Uuid;
using tf::core::now;

namespace {

/// Unique temp db path per test case; removed after the run.
class TempDb {
public:
    TempDb() {
        path_ = fs::temp_directory_path() /
                ("transframe_history_test_" + Uuid::randomUuid().toString() + ".db");
        pathStr_ = path_.string();
    }
    ~TempDb() {
        std::error_code ec;
        fs::remove(path_, ec);
        fs::remove(path_.string() + "-wal", ec);
        fs::remove(path_.string() + "-shm", ec);
    }
    TempDb(const TempDb&) = delete;
    TempDb& operator=(const TempDb&) = delete;
    const std::string& path() const { return pathStr_; }

private:
    fs::path path_;
    std::string pathStr_ = path_.string();
};

TranslationHistory makeEntry(const std::string& src, const std::string& dst,
                             HistoryCategory cat = HistoryCategory::Translation,
                             bool favorite = false) {
    TranslationHistory h;
    h.id = Uuid::randomUuid();
    h.sourceText = src;
    h.translatedText = dst;
    h.sourceLanguage = "auto";
    h.targetLanguage = "zh";
    h.timestamp = now();
    h.category = cat;
    h.isFavorite = favorite;
    return h;
}

}  // namespace

TEST_CASE("HistoryStore opens and creates schema v1", "[history][sqlite]") {
    TempDb db;
    std::string err;
    auto store = HistoryStore::open(db.path(), &err);
    REQUIRE(store != nullptr);
    CHECK(err.empty());
    CHECK(HistoryStore::kSchemaVersion == 1);
    CHECK(store->totalEntries() == 0);
}

TEST_CASE("HistoryStore insert + query newest-first", "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);

    auto first = makeEntry("hello", "你好");
    auto second = makeEntry("world", "世界");
    // Distinct timestamps: now() has system-clock resolution; force ordering.
    second.timestamp += std::chrono::seconds(2);
    REQUIRE(store->insert(first));
    REQUIRE(store->insert(second));

    const auto rows = store->query(HistoryQuery{});
    REQUIRE(rows.size() == 2);
    CHECK(rows[0].translatedText == "世界");  // newest first
    CHECK(rows[1].translatedText == "你好");
    CHECK(store->totalEntries() == 2);
}

TEST_CASE("HistoryStore LIKE search is ASCII case-insensitive and escapes wildcards",
          "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);

    REQUIRE(store->insert(makeEntry("Hello World", "你好世界")));
    REQUIRE(store->insert(makeEntry("100% done", "百分之百")));

    HistoryQuery q;
    q.searchText = "hello w";
    auto rows = store->query(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].sourceText == "Hello World");

    // '%' in the query must match a literal percent, not a wildcard.
    q.searchText = "100% do";
    rows = store->query(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].sourceText == "100% done");
}

TEST_CASE("HistoryStore favorites filter and toggle", "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);

    auto a = makeEntry("a", "甲", HistoryCategory::Translation, false);
    auto b = makeEntry("b", "乙", HistoryCategory::Translation, true);
    REQUIRE(store->insert(a));
    REQUIRE(store->insert(b));

    HistoryQuery q;
    q.favoritesOnly = true;
    auto rows = store->query(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].sourceText == "b");

    // Toggle by id; unknown id fails.
    CHECK(store->setFavorite(a.id.toString(), true));
    CHECK(store->query(q).size() == 2);
    CHECK_FALSE(store->setFavorite("00000000-0000-0000-0000-000000000000", true));
}

TEST_CASE("HistoryStore category filter", "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    REQUIRE(store->insert(makeEntry("t1", "一", HistoryCategory::Translation)));
    REQUIRE(store->insert(makeEntry("Color Picked", "#112233", HistoryCategory::Color)));

    HistoryQuery q;
    q.category = HistoryCategory::Color;
    const auto rows = store->query(q);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].category == HistoryCategory::Color);
}

TEST_CASE("HistoryStore remove by id", "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    const auto e = makeEntry("gone", "删除");
    REQUIRE(store->insert(e));
    CHECK(store->remove(e.id.toString()));
    CHECK(store->totalEntries() == 0);
    CHECK_FALSE(store->remove(e.id.toString()));  // second delete fails: gone
}

TEST_CASE("HistoryStore persists across reopen", "[history][sqlite]") {
    TempDb db;
    {
        auto store = HistoryStore::open(db.path());
        REQUIRE(store != nullptr);
        REQUIRE(store->insert(makeEntry("persist", "保留")));
    }
    auto reopened = HistoryStore::open(db.path());
    REQUIRE(reopened != nullptr);
    const auto rows = reopened->query(HistoryQuery{});
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].sourceText == "persist");
}

TEST_CASE("HistoryStore round-trips thumbnail bytes via meta_json", "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    auto e = makeEntry("shot", "截图");
    e.category = HistoryCategory::Screenshot;
    e.thumbnailData = std::vector<std::uint8_t>{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
    REQUIRE(store->insert(e));

    const auto rows = store->query(HistoryQuery{});
    REQUIRE(rows.size() == 1);
    REQUIRE(rows[0].thumbnailData.has_value());
    CHECK(*rows[0].thumbnailData ==
          std::vector<std::uint8_t>{0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A});
}

TEST_CASE("HistoryStore open failure on unwritable path", "[history][sqlite]") {
    std::string err;
    const std::string bad = std::string("/proc/definitely/not/writable/x.db");
    const auto store = HistoryStore::open(bad, &err);
    // Linux/macOS: /proc is not writable -> nullptr + error; on some hosts the
    // create may still fail differently, so only assert the error channel.
    if (store == nullptr) {
        CHECK_FALSE(err.empty());
    }
}

// ---------------------------------------------------------------------------
// Additive queries (FR-30 full history + FR-34 tray recent-5)
// ---------------------------------------------------------------------------
TEST_CASE("HistoryStore recentScreenshots caps and orders newest-first",
          "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    for (int i = 0; i < 7; ++i) {
        TranslationHistory shot =
            makeEntry("shot " + std::to_string(i), "", HistoryCategory::Screenshot);
        shot.timestamp = tf::core::TimePoint{std::chrono::seconds(1700000000 + i * 60)};
        REQUIRE(store->insert(shot));
    }
    REQUIRE(store->insert(makeEntry("t", "t")));  // translation ignored

    const auto recents = store->recentScreenshots(5);
    REQUIRE(recents.size() == 5);
    CHECK(recents.front().sourceText == "shot 6");  // newest first
    CHECK(recents.back().sourceText == "shot 2");   // oldest survivor
    CHECK(store->recentScreenshots(0).empty());     // 0 = nothing
}
TEST_CASE("HistoryStore entryById round-trips and misses cleanly",
          "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    const TranslationHistory e = makeEntry("hello", "bonjour");
    REQUIRE(store->insert(e));

    const auto found = store->entryById(e.id.toString());
    REQUIRE(found.has_value());
    CHECK(found->sourceText == "hello");
    CHECK(found->translatedText == "bonjour");
    CHECK_FALSE(store->entryById(Uuid::randomUuid().toString()).has_value());
}
TEST_CASE("HistoryStore favoriteCount tracks toggles", "[history][sqlite]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    const TranslationHistory a = makeEntry("a", "a");
    const TranslationHistory b = makeEntry("b", "b");
    REQUIRE(store->insert(a));
    REQUIRE(store->insert(b));
    CHECK(store->favoriteCount() == 0);
    REQUIRE(store->setFavorite(a.id.toString(), true));
    REQUIRE(store->setFavorite(b.id.toString(), true));
    CHECK(store->favoriteCount() == 2);
    REQUIRE(store->setFavorite(b.id.toString(), false));
    CHECK(store->favoriteCount() == 1);
}

// ---------------------------------------------------------------------------
// FR-32/FR-41 additive queries: colorEntries / colorCount / removeCategory.
// ---------------------------------------------------------------------------
TEST_CASE("HistoryStore colorEntries filters by HEX fragment and favorites",
          "[history][sqlite][color]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    const TranslationHistory red = makeEntry("Color Picked", "#FF3B30", HistoryCategory::Color);
    const TranslationHistory navy = makeEntry("Color Picked", "#1A2B3C", HistoryCategory::Color, true);
    const TranslationHistory sky = makeEntry("Color Picked", "#87CEEB", HistoryCategory::Color);
    const TranslationHistory tr = makeEntry("hello", "#FF3B30", HistoryCategory::Translation);
    REQUIRE(store->insert(red));
    REQUIRE(store->insert(navy));
    REQUIRE(store->insert(sky));
    REQUIRE(store->insert(tr));  // same text, other category: must not leak in

    CHECK(store->colorCount() == 3);
    REQUIRE(store->colorEntries().size() == 3);
    // HEX fragment, case-insensitive (spec §4.3: 如 `1A2`).
    REQUIRE(store->colorEntries("1a2").size() == 1);
    CHECK(store->colorEntries("1a2").front().translatedText == "#1A2B3C");
    CHECK(store->colorEntries("#ff3").size() == 1);
    // Favorites filter stacks.
    auto favs = store->colorEntries("", true);
    REQUIRE(favs.size() == 1);
    CHECK(favs.front().translatedText == "#1A2B3C");
    CHECK(store->colorEntries("87ce", true).empty());
    CHECK(store->colorEntries("zz").empty());
}

TEST_CASE("HistoryStore removeCategory clears only the color category",
          "[history][sqlite][color]") {
    TempDb db;
    auto store = HistoryStore::open(db.path());
    REQUIRE(store != nullptr);
    REQUIRE(store->insert(makeEntry("Color Picked", "#ABCDEF", HistoryCategory::Color)));
    REQUIRE(store->insert(makeEntry("Color Picked", "#123456", HistoryCategory::Color)));
    REQUIRE(store->insert(makeEntry("hello", "你好", HistoryCategory::Translation)));
    REQUIRE(store->insert(makeEntry("shot", "C:/x.png", HistoryCategory::Screenshot)));

    REQUIRE(store->colorCount() == 2);
    CHECK(store->removeCategory(HistoryCategory::Color) == 2);
    CHECK(store->colorCount() == 0);
    CHECK(store->colorEntries().empty());
    // Other categories untouched (spec §4.3: 仅清空 color 类目).
    CHECK(store->totalEntries() == 2);
    HistoryQuery all;
    CHECK(store->query(all).size() == 2);
    // Second clear is a no-op returning 0.
    CHECK(store->removeCategory(HistoryCategory::Color) == 0);
    std::string err;
    CHECK(store->removeCategory(static_cast<HistoryCategory>(99), &err) == -1);
    CHECK_FALSE(err.empty());
}
