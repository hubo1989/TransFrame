// tf/ColorHistoryWindow.hpp — FR-41 independent color history window.
//
// Win32 grid shell over tf::core::HistoryStore color entries (spec
// docs/specs/color-history-window.md): >=48px swatch grid + HEX (mono) +
// pick time; single click copies in the current format with an instant
// "已复制" flash; right-click menu copies HEX/RGB, toggles favorite, deletes;
// top bar = HEX-fragment search + favorites-only toggle + copy-format toggle
// (HEX/RGB, persisted) + clear-all (confirm dialog; only color entries).
// The store semantics stay in Core; this TU only renders and hit-tests.
#ifndef TF_COLOR_HISTORY_WINDOW_HPP
#define TF_COLOR_HISTORY_WINDOW_HPP

#include "tf/HistoryStore.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tf {

/// Pure layout math (unit-testable): how many columns/cells fit and each
/// cell's size for a given client width and DPI. Swatch is >= 48 logical px.
struct ColorHistoryLayout {
    int columns = 1;
    int cellW = 72;   // logical px incl. padding
    int cellH = 84;
    int swatch = 48;  // logical px (>= spec minimum)
    int gridX = 8;    // left/top origin of the grid area
    int gridY = 8;
    int rows = 0;     // visible rows (0 when height fits none)
};
ColorHistoryLayout colorHistoryGridLayout(int clientW, int clientH, int dpi96);

class ColorHistoryWindowController {
public:
    /// store is non-owning and must outlive the controller (AppMain owns it).
    explicit ColorHistoryWindowController(core::HistoryStore* store);
    ~ColorHistoryWindowController();

    ColorHistoryWindowController(const ColorHistoryWindowController&) = delete;
    ColorHistoryWindowController& operator=(const ColorHistoryWindowController&) = delete;

    static bool registerClass(void* hinstance);

    /// Create (or raise) the window and refresh the grid. Default 560x420,
    /// resizable, position/size remembered across opens in this process.
    void show();
    void close();
    bool isVisible() const;

    /// AppMain calls this after a completed pick so a visible window shows
    /// the new color immediately (spec §4.5: live insert + highlight).
    void notifyColorPicked();

    // --- smoke-test seams (same code paths as the UI, no fakes) ---
    int rowCount() const { return static_cast<int>(rows_.size()); }
    std::string hexAt(int index) const;
    const std::vector<core::TranslationHistory>& visibleRows() const { return rows_; }
    void applySearchForTest(const std::wstring& text);
    void toggleFavoriteAtForTest(int index);
    void deleteAtForTest(int index);
    void clearAllForTest();  // the confirm-free core of 清空全部
    bool copyFormatRgb() const { return copyRgb_; }
    void setCopyFormatRgbForTest(bool rgb);

private:
    /// PIMPL holding the Win32 plumbing; public declaration so the wndProc's
    /// file-scope helpers can name the type (same pattern as Impl in cpp).
public:
    struct Impl;
    Impl* impl() { return impl_; }

private:
    Impl* impl_ = nullptr;
    std::vector<core::TranslationHistory> rows_;
    core::HistoryStore* store_ = nullptr;
    bool copyRgb_ = false;  // HEX default (spec §4.3), persisted below
};

}  // namespace tf

#endif  // TF_COLOR_HISTORY_WINDOW_HPP
