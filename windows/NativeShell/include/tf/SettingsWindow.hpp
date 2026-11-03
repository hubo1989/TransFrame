#pragma once
// tf/SettingsWindow.hpp — settings window (FR-35 minimal: 通用 + 快捷键).
//
// Win32 common controls (tab + combo + edit + button) — the WinUI 3 decision
// and its revisit triggers live in windows/docs/winui3-migration.md. The
// window is a thin shell: every read/write goes through the callbacks, which
// are owned by AppMain (engines.json / settings.json / live pipeline apply).
#ifndef TF_SETTINGS_WINDOW_HPP
#define TF_SETTINGS_WINDOW_HPP

#include "tf/EngineConfigStore.hpp"
#include "tf/HotkeyManager.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace tf {

struct SettingsWindowCallbacks {
    /// Read the current engines.json values (initial field content).
    std::function<core::EngineConfig()> loadEngineConfig;
    /// Validate + persist engines.json + apply to the live pipeline.
    /// Returns empty on success, else a user-facing error (zh) shown inline.
    std::function<std::wstring(const core::EngineConfig&)> saveEngineConfig;
    /// Initial hotkey set for display (specs currently applied).
    std::function<std::vector<HotkeySpec>()> currentSpecs;
    /// Validate + re-register + persist the edited spec set. The report's
    /// conflicts drive the per-row red hint (FR-33).
    std::function<HotkeyRegistrationReport(std::vector<HotkeySpec>)> applySpecs;
    /// Conflicts from the most recent apply (startup included) so the page
    /// marks already-conflicting rows on open.
    std::function<HotkeyRegistrationReport()> lastReport;
};

class SettingsWindowController {
public:
    explicit SettingsWindowController(SettingsWindowCallbacks callbacks);
    ~SettingsWindowController();

    SettingsWindowController(const SettingsWindowController&) = delete;
    SettingsWindowController& operator=(const SettingsWindowController&) = delete;

    /// Register the window class (once per process).
    static bool registerClass(HINSTANCE instance);

    /// Create (or raise) the window; fields reload from the callbacks.
    void show();

    void close();

    bool isVisible() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tf

#endif  // TF_SETTINGS_WINDOW_HPP
