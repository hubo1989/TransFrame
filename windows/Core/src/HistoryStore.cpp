// HistoryStore.cpp — SQLite-backed implementation of tf/HistoryStore.hpp.
#include "tf/HistoryStore.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <algorithm>
#include <cctype>
#include <mutex>

namespace tf::core {
namespace {

constexpr int kSqliteOk = SQLITE_OK;

// Swift reference-date seconds (same encoding as the settings.json history
// JSON codec, Json.h): unixSeconds - 978307200.
constexpr double kSwiftReferenceDateUnix = 978307200.0;

double toSwiftSeconds(const TimePoint& t) {
    using namespace std::chrono;
    const auto unixNow = duration_cast<duration<double>>(t.time_since_epoch()).count();
    return unixNow - kSwiftReferenceDateUnix;
}

TimePoint fromSwiftSeconds(double s) {
    using namespace std::chrono;
    return TimePoint{duration_cast<system_clock::duration>(duration<double>(s + kSwiftReferenceDateUnix))};
}

std::string lowercased(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// LIKE escapes: % _ and the escape char itself, per SQLite ESCAPE '\\'.
std::string likeEscaped(const std::string& text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (char c : text) {
        if (c == '\\' || c == '%' || c == '_') out += '\\';
        out += c;
    }
    return out;
}

// ---------------------------------------------------------------------------
// meta_json (thumbnailData lives here as base64; PRD column thumbnail_path
// stays empty until the screenshot pipeline stores files)
// ---------------------------------------------------------------------------
constexpr char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

std::string base64Encode(const std::vector<std::uint8_t>& data) {
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    std::size_t i = 0;
    while (i + 2 < data.size()) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8) |
                                static_cast<std::uint32_t>(data[i + 2]);
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += kBase64Chars[(n >> 6) & 63];
        out += kBase64Chars[n & 63];
        i += 3;
    }
    if (i + 1 == data.size()) {
        const std::uint32_t n = static_cast<std::uint32_t>(data[i]) << 16;
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += "==";
    } else if (i + 2 == data.size()) {
        const std::uint32_t n = (static_cast<std::uint32_t>(data[i]) << 16) |
                                (static_cast<std::uint32_t>(data[i + 1]) << 8);
        out += kBase64Chars[(n >> 18) & 63];
        out += kBase64Chars[(n >> 12) & 63];
        out += kBase64Chars[(n >> 6) & 63];
        out += '=';
    }
    return out;
}

std::optional<std::vector<std::uint8_t>> base64Decode(const std::string& text) {
    auto val = [](char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1;
    };
    std::vector<std::uint8_t> out;
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : text) {
        if (c == '=') break;
        const int v = val(c);
        if (v < 0) return std::nullopt;  // corrupt meta -> caller keeps entry without thumbnail
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFF));
        }
    }
    return out;
}

std::string metaJsonFor(const TranslationHistory& entry) {
    if (!entry.thumbnailData || entry.thumbnailData->empty()) return "{}";
    nlohmann::json m = nlohmann::json::object();
    m["thumbnailData"] = base64Encode(*entry.thumbnailData);
    return m.dump();
}

std::vector<std::uint8_t> thumbnailFromMeta(const char* metaJson) {
    std::vector<std::uint8_t> out;
    if (!metaJson || !*metaJson) return out;
    const nlohmann::json m = nlohmann::json::parse(metaJson, nullptr, false);
    if (m.is_discarded() || !m.is_object() || !m.contains("thumbnailData")) return out;
    if (!m.at("thumbnailData").is_string()) return out;
    auto bytes = base64Decode(m.at("thumbnailData").get<std::string>());
    if (bytes) return *bytes;
    return out;
}

}  // namespace

// ---------------------------------------------------------------------------
// Impl
// ---------------------------------------------------------------------------
struct HistoryStore::Impl {
    sqlite3* db = nullptr;
    mutable std::mutex mutex;

    bool exec(const char* sql, std::string* error = nullptr) {
        char* msg = nullptr;
        const int rc = ::sqlite3_exec(db, sql, nullptr, nullptr, &msg);
        if (msg) {
            if (error && error->empty()) *error = msg;
            ::sqlite3_free(msg);
        }
        return rc == kSqliteOk;
    }

    bool beginSchema(std::string* error) {
        // WAL keeps concurrent worker inserts + window reads consistent
        // (PRD §4: SQLite WAL at %LOCALAPPDATA%\TransFrame\history.db).
        if (!exec("PRAGMA journal_mode=WAL;", error)) return false;
        exec("CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL);");
        exec("CREATE TABLE IF NOT EXISTS entries ("
             "id TEXT PRIMARY KEY, "
             "category TEXT NOT NULL, "
             "source_text TEXT NOT NULL, "
             "translated_text TEXT NOT NULL, "
             "source_lang TEXT NOT NULL, "
             "target_lang TEXT NOT NULL, "
             "timestamp REAL NOT NULL, "
             "thumbnail_path TEXT NOT NULL, "
             "is_favorite INTEGER NOT NULL, "
             "meta_json TEXT NOT NULL);");
        exec("CREATE INDEX IF NOT EXISTS idx_entries_timestamp ON entries(timestamp DESC);");
        exec("CREATE INDEX IF NOT EXISTS idx_entries_favorite ON entries(is_favorite);");
        exec("INSERT OR REPLACE INTO schema_version(version) VALUES(1);");
        return true;
    }
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
std::unique_ptr<HistoryStore> HistoryStore::open(const std::string& dbPath,
                                                 std::string* error) {
    auto impl = std::make_unique<Impl>();
    const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
    char* msg = nullptr;
    int rc = ::sqlite3_open_v2(dbPath.c_str(), &impl->db, flags, nullptr);
    if (rc != kSqliteOk) {
        if (msg) ::sqlite3_free(msg);
        if (impl->db) ::sqlite3_close(impl->db);
        if (error && error->empty()) {
            *error = "sqlite3_open_v2 failed (" + std::to_string(rc) + ")";
        }
        return nullptr;
    }
    std::string schemaError;
    if (!impl->beginSchema(&schemaError)) {
        ::sqlite3_close(impl->db);
        if (error && error->empty()) *error = schemaError;
        return nullptr;
    }
    auto store = std::make_unique<HistoryStore>();
    store->impl_ = std::move(impl);
    return store;
}

HistoryStore::~HistoryStore() {
    if (impl_ && impl_->db) ::sqlite3_close(impl_->db);
}

// ---------------------------------------------------------------------------
// Row mapping
// ---------------------------------------------------------------------------
namespace {

TranslationHistory rowToEntry(sqlite3_stmt* stmt) {
    TranslationHistory h;
    const auto textAt = [&stmt](int col) {
        const unsigned char* p = sqlite3_column_text(stmt, col);
        return p ? reinterpret_cast<const char*>(p) : "";
    };
    Uuid id;
    if (!Uuid::parse(textAt(0), id)) return h;
    h.id = id;
    h.sourceText = textAt(2);
    h.translatedText = textAt(3);
    h.sourceLanguage = textAt(4);
    h.targetLanguage = textAt(5);
    h.timestamp = fromSwiftSeconds(sqlite3_column_double(stmt, 6));
    h.category = historyCategoryFromRaw(textAt(1));
    h.isFavorite = sqlite3_column_int(stmt, 8) != 0;
    const unsigned char* meta = sqlite3_column_text(stmt, 9);
    const auto thumb = thumbnailFromMeta(reinterpret_cast<const char*>(meta));
    if (!thumb.empty()) h.thumbnailData = thumb;
    return h;
}

}  // namespace

// ---------------------------------------------------------------------------
// CRUD
// ---------------------------------------------------------------------------
bool HistoryStore::insert(const TranslationHistory& entry, std::string* error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    const char* sql =
        "INSERT OR REPLACE INTO entries(id, category, source_text, translated_text, "
        "source_lang, target_lang, timestamp, thumbnail_path, is_favorite, meta_json) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9, ?10);";
    sqlite3_stmt* stmt = nullptr;
    if (::sqlite3_prepare_v2(impl_->db, sql, -1, &stmt, nullptr) != kSqliteOk) {
        if (error) *error = ::sqlite3_errmsg(impl_->db);
        return false;
    }
    const std::string metaJson = metaJsonFor(entry);
    const std::string idText = entry.id.toString();
    ::sqlite3_bind_text(stmt, 1, idText.c_str(), -1, SQLITE_TRANSIENT);
    ::sqlite3_bind_text(stmt, 2, toRaw(entry.category).c_str(), -1, SQLITE_TRANSIENT);
    ::sqlite3_bind_text(stmt, 3, entry.sourceText.c_str(), -1, SQLITE_TRANSIENT);
    ::sqlite3_bind_text(stmt, 4, entry.translatedText.c_str(), -1, SQLITE_TRANSIENT);
    ::sqlite3_bind_text(stmt, 5, entry.sourceLanguage.c_str(), -1, SQLITE_TRANSIENT);
    ::sqlite3_bind_text(stmt, 6, entry.targetLanguage.c_str(), -1, SQLITE_TRANSIENT);
    ::sqlite3_bind_double(stmt, 7, toSwiftSeconds(entry.timestamp));
    ::sqlite3_bind_text(stmt, 8, "", -1, SQLITE_TRANSIENT);  // thumbnail_path: unused until screenshot thumbs land on disk
    ::sqlite3_bind_int(stmt, 9, entry.isFavorite ? 1 : 0);
    ::sqlite3_bind_text(stmt, 10, metaJson.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = ::sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok && error) *error = ::sqlite3_errmsg(impl_->db);
    ::sqlite3_finalize(stmt);
    return ok;
}

bool HistoryStore::setFavorite(const std::string& id, bool favorite, std::string* error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    sqlite3_stmt* stmt = nullptr;
    if (::sqlite3_prepare_v2(impl_->db,
                             "UPDATE entries SET is_favorite=?1 WHERE id=?2;",
                             -1, &stmt, nullptr) != kSqliteOk) {
        if (error) *error = ::sqlite3_errmsg(impl_->db);
        return false;
    }
    ::sqlite3_bind_int(stmt, 1, favorite ? 1 : 0);
    ::sqlite3_bind_text(stmt, 2, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = ::sqlite3_step(stmt) == SQLITE_DONE && ::sqlite3_changes(impl_->db) > 0;
    if (!ok && error) {
        *error = ::sqlite3_changes(impl_->db) == 0 ? "id not found" : ::sqlite3_errmsg(impl_->db);
    }
    ::sqlite3_finalize(stmt);
    return ok;
}

bool HistoryStore::remove(const std::string& id, std::string* error) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    sqlite3_stmt* stmt = nullptr;
    if (::sqlite3_prepare_v2(impl_->db, "DELETE FROM entries WHERE id=?1;",
                             -1, &stmt, nullptr) != kSqliteOk) {
        if (error) *error = ::sqlite3_errmsg(impl_->db);
        return false;
    }
    ::sqlite3_bind_text(stmt, 1, id.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = ::sqlite3_step(stmt) == SQLITE_DONE && ::sqlite3_changes(impl_->db) > 0;
    if (!ok && error) {
        *error = ::sqlite3_changes(impl_->db) == 0 ? "id not found" : ::sqlite3_errmsg(impl_->db);
    }
    ::sqlite3_finalize(stmt);
    return ok;
}

namespace {

std::string join_clauses(const std::vector<std::string>& parts) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out += " AND ";
        out += parts[i];
    }
    return out;
}

}  // namespace

std::vector<TranslationHistory> HistoryStore::query(const HistoryQuery& q) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    std::string sql =
        "SELECT id, category, source_text, translated_text, source_lang, "
        "target_lang, timestamp, thumbnail_path, is_favorite, meta_json "
        "FROM entries";
    const bool hasSearch = !q.searchText.empty();
    const bool hasCategory = q.category.has_value();
    const bool whereNeeded = hasSearch || hasCategory || q.favoritesOnly;
    std::vector<std::string> clauses;
    std::string likePattern;
    if (hasSearch) {
        likePattern = "%" + likeEscaped(q.searchText) + "%";
        clauses.push_back("(source_text LIKE ?1 ESCAPE '\\' OR translated_text LIKE ?1 ESCAPE '\\')");
    }
    if (hasCategory) clauses.push_back("category = ?2");
    if (q.favoritesOnly) clauses.push_back("is_favorite = 1");
    if (whereNeeded) sql += " WHERE " + join_clauses(clauses);
    sql += " ORDER BY timestamp DESC LIMIT ?3;";

    sqlite3_stmt* stmt = nullptr;
    std::vector<TranslationHistory> rows;
    if (::sqlite3_prepare_v2(impl_->db, sql.c_str(), -1, &stmt, nullptr) != kSqliteOk) {
        return rows;
    }
    // Parameter numbers are fixed by the SQL text (?1 search, ?2 category,
    // ?3 limit); absent filters simply leave their slot unbound (NULL-free
    // because the corresponding clause is absent too).
    if (hasSearch) {
        ::sqlite3_bind_text(stmt, 1, likePattern.c_str(), -1, SQLITE_TRANSIENT);
    }
    if (hasCategory) {
        ::sqlite3_bind_text(stmt, 2, toRaw(*q.category).c_str(), -1, SQLITE_TRANSIENT);
    }
    ::sqlite3_bind_int(stmt, 3, q.limit > 0 ? q.limit : 1);
    while (::sqlite3_step(stmt) == SQLITE_ROW) rows.push_back(rowToEntry(stmt));
    ::sqlite3_finalize(stmt);
    return rows;
}

std::int64_t HistoryStore::totalEntries() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    sqlite3_stmt* stmt = nullptr;
    std::int64_t total = 0;
    if (::sqlite3_prepare_v2(impl_->db, "SELECT COUNT(*) FROM entries;",
                             -1, &stmt, nullptr) == kSqliteOk) {
        if (::sqlite3_step(stmt) == SQLITE_ROW) total = sqlite3_column_int64(stmt, 0);
        ::sqlite3_finalize(stmt);
    }
    return total;
}

}  // namespace tf::core
