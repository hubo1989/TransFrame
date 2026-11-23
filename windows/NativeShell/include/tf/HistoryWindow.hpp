#pragma once
// tf/HistoryWindow.hpp — translation history list window (FR-30 minimal UI).
//
// ListView (report) + search box + filter combo + favorite/delete buttons.
// Data comes from tf::core::HistoryStore (non-owning pointer); the window is
// a thin shell so the SQLite semantics stay in Core where they are testable.
#ifndef TF_HISTORY_WINDOW_HPP
#define TF_HISTORY_WINDOW_HPP

#include "tf/HistoryStore.hpp"

#include <memory>

namespace tf {

class HistoryWindowController {
public:
    /// store is non-owning and must outlive the controller (AppMain owns it).
    explicit HistoryWindowController(core::HistoryStore* store);
    ~HistoryWindowController();

    HistoryWindowController(const HistoryWindowController&) = delete;
    HistoryWindowController& operator=(const HistoryWindowController&) = delete;

    /// Register the window class (once per process).
    static bool registerClass(HINSTANCE instance);

    /// Create (or raise) the window and refresh the list.
    void show();

    void close();

    bool isVisible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tf

#endif  // TF_HISTORY_WINDOW_HPP
