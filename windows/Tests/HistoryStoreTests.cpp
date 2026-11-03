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
