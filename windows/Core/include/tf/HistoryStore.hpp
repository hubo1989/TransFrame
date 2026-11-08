#pragma once
// tf/HistoryStore.hpp — translation history persistence (PRD FR-30).
//
// SQLite (WAL) at %LOCALAPPDATA%\TransFrame\history.db; schema v1 + migration
// table (PRD §4). LIKE search on source/translated text (PRD decision 2: no
// FTS5). Model = tf::core::TranslationHistory (field parity with macOS).
//
// Pure C++ over the sqlite3 C API; sqlite3.h is included only in the .cpp so
// the amalgamation stays a private build detail. Thread-safe: all entry
// points serialize on an internal mutex (worker threads insert while the
// history window queries on the main thread).
#ifndef TF_HISTORY_STORE_HPP
#define TF_HISTORY_STORE_HPP

#include "TranslationHistory.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

struct HistoryQuery {
    std::string searchText;                   // LIKE substring, ASCII case-insensitive
    std::optional<HistoryCategory> category;  // nullopt = all categories
    bool favoritesOnly = false;
    int limit = 200;                          // max rows returned (newest first)
};

class HistoryStore {
public:
    /// Opens (creating if needed) the db at dbPath (UTF-8), applies WAL and
    /// schema v1. Returns nullptr and fills *error on failure (caller decides
    /// degradation — recording is disabled, app keeps running).
    static std::unique_ptr<HistoryStore> open(const std::string& dbPath,
                                              std::string* error = nullptr);
    ~HistoryStore();

    HistoryStore(const HistoryStore&) = delete;
    HistoryStore& operator=(const HistoryStore&) = delete;

    /// Insert or replace (id is the primary key). Favorite flag preserved.
    bool insert(const TranslationHistory& entry, std::string* error = nullptr);

    /// Toggle favorite by id. Missing id -> false with error set.
    bool setFavorite(const std::string& id, bool favorite, std::string* error = nullptr);

    /// Delete one entry. Missing id -> false with error set.
    bool remove(const std::string& id, std::string* error = nullptr);

    /// Newest first. searchText uses LIKE '%q%' on source_text/translated_text
    /// (ASCII case-insensitive; % and _ escaped). 0..N rows, at most limit.
    std::vector<TranslationHistory> query(const HistoryQuery& q) const;

    /// Total row count (no filters).
    std::int64_t totalEntries() const;

    // --- additive queries (FR-30 full history + FR-34 tray recent-5) ---

    /// Newest `count` screenshot-category entries (newest first; the tray
    /// recent-shots strip and the history screenshot tab read this).
    std::vector<TranslationHistory> recentScreenshots(int count) const;

    /// One entry by id string (Uuid::toString). Nullopt when missing.
    std::optional<TranslationHistory> entryById(const std::string& id) const;

    /// Favorite row count (history window capacity line).
    std::int64_t favoriteCount() const;

    /// Current schema version recorded in the migration table.
    static constexpr int kSchemaVersion = 1;

    /// Use open(). Public only so std::make_unique can construct the PIMPL
    /// holder; a default-built object has no db and must not be used.
    HistoryStore() = default;

 private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tf::core

#endif  // TF_HISTORY_STORE_HPP
