// windows/UI/ShortcutsPage/ShortcutsPage.cpp
// FR-35 快捷键页签 code-behind 实现（录制状态机/冲突标红/持久化全走 VM
// 与 Core HotkeySettingsStore；控件清单见 .h 文件头）。
#include "ShortcutsPage.h"
#include "SettingsFileStore.hpp"
#include "SettingsUiKit.hpp"

#include "tf/HotkeySettingsStore.hpp"

#include <windows.h>  // GetKeyState（修饰键实时状态；VK_* 常量）

#include <nlohmann/json.hpp>

namespace tf::ui {
namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

constexpr const char* kPickColorKey = "TransFrame.pickColorShortcut";

winrt::Microsoft::UI::Xaml::Media::SolidColorBrush RedBrush() {
    winrt::Microsoft::UI::Xaml::Media::SolidColorBrush b;
    b.Color(Windows::UI::Color{0xFF, 0xC4, 0x2B, 0x1C});
    return b;
}

}  // namespace

ShortcutsPage::ShortcutsPage() {
    IsTabStop(true);
    UseSystemFocusVisuals(false);
    KeyDown([this](auto&& sender, auto&& args) {
        HandleKeyDown(sender, args);
    });
    LoadFromDisk();
    BuildUi();
    RefreshAllRows();
}

void ShortcutsPage::LoadFromDisk() {
    const std::string json = store::ReadSettingsJson();
    vm_.LoadFromBindings(tf::core::parseHotkeyBindings(json), std::nullopt);
    if (auto pick = store::SettingsKey(kPickColorKey)) {
        if (pick->is_object() && pick->contains("vk") && pick->contains("modifiers") &&
            (*pick)["vk"].is_number_unsigned() && (*pick)["modifiers"].is_number_unsigned()) {
            tf::core::HotkeyBinding b{
                "pickColor", (*pick)["vk"].get<std::uint32_t>(),
                (*pick)["modifiers"].get<std::uint32_t>(), true};
            vm_.LoadFromBindings({}, b);
        }
    }
    vm_.DetectConflicts();
}

void ShortcutsPage::BuildUi() {
    Controls::StackPanel root = kit::MakePageRoot(*this);
    root.Children().Append(kit::MakeSectionHeader(
        kit::Lx("settings.section.shortcuts", L"快捷键").c_str()));

    kit::Card card = kit::MakeCard();
    card.panel.Children().Append(kit::MakeLabelBlock(
        kit::Lx("win.shortcuts.title", L"全局快捷键").c_str(),
        kit::Lx("win.shortcuts.hint", L"点击组合按钮后按下新组合键（需含至少一个修饰键），Esc 取消录制；冲突的组合会标红").c_str()));

    for (int row = 0; row < kShortcutRowCount; ++row) {
        Controls::StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(12.0);

        Controls::TextBlock label =
            kit::MakeText(ActionLabelForRow(row), false, 14.0);
        label.Width(96.0);

        recordButtons_[row] = Controls::Button();
        recordButtons_[row].MinWidth(150.0);
        recordButtons_[row].Click([this, row](auto&&, auto&&) { HandleRecordClick(row); });

        resetButtons_[row] = Controls::Button();
        resetButtons_[row].Content(winrt::box_value(
            kit::Lx("prompt.button.reset", L"重置")));
        resetButtons_[row].Click([this, row](auto&&, auto&&) { HandleResetClick(row); });

        line.Children().Append(label);
        line.Children().Append(recordButtons_[row]);
        line.Children().Append(resetButtons_[row]);
        card.panel.Children().Append(line);

        conflictTexts_[row] = Controls::TextBlock();
        conflictTexts_[row].Foreground(RedBrush());
        conflictTexts_[row].FontSize(12.0);
        conflictTexts_[row].Margin({108, 0, 0, 0});
        conflictTexts_[row].Visibility(Visibility::Collapsed);
        card.panel.Children().Append(conflictTexts_[row]);
    }

    root.Children().Append(card.border);

    statusText_ = Controls::TextBlock();
    statusText_.FontSize(12.0);
    statusText_.Opacity(0.7);
    statusText_.TextWrapping(TextWrapping::Wrap);
    root.Children().Append(statusText_);
}

// ---------------------------------------------------------------------------
// 录制流程
// ---------------------------------------------------------------------------
void ShortcutsPage::HandleRecordClick(int row) {
    // 同一时刻仅一行在录：VM 内部切换单一 recordingRow。
    vm_.BeginRecording(row);
    // GrabFocus：拿键盘焦点以便接收 KeyDown（UserControl IsTabStop=True）。
    Focus(FocusState::Programmatic);
    RefreshAllRows();
    RefreshStatusText();
}

void ShortcutsPage::HandleResetClick(int row) {
    vm_.ResetRow(row);
    vm_.DetectConflicts();
    RefreshAllRows();
    RefreshStatusText();
    PersistBindings();
}

void ShortcutsPage::HandleKeyDown(winrt::Windows::Foundation::IInspectable const&,
                                  Input::KeyRoutedEventArgs const& e) {
    if (!vm_.IsRecording()) return;

    // VirtualKey → Win32 VK：字母/数字/F 键区枚举数值与 VK 一致
    //（VirtualKey::A=0x41…, Number0=0x30…, F1=0x70…），直接取数值。
    const std::uint32_t vk = static_cast<std::uint32_t>(e.Key());

    // 修饰位：CoreTypes.hpp 语义 MOD_ALT=1/CONTROL=2/SHIFT=4/WIN=8。
    // WinUI 3 的 KeyRoutedEventArgs 无 KeyModifiers 属性（UWP 专有），用
    // Win32 GetKeyState 取实时修饰键状态（高位 = 按下）。
    std::uint32_t mods = 0;
    if (::GetKeyState(VK_CONTROL) < 0) mods |= kModControl;
    if (::GetKeyState(VK_MENU) < 0) mods |= kModAlt;
    if (::GetKeyState(VK_SHIFT) < 0) mods |= kModShift;
    if (::GetKeyState(VK_LWIN) < 0 || ::GetKeyState(VK_RWIN) < 0) mods |= kModWin;

    // 纯修饰键按下不构成组合（等待非修饰键）。
    if (vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5B || vk == 0x5C) return;

    const auto result = vm_.HandleRecordInput(vk, mods);
    e.Handled(true);  // 录制期间吞掉按键（含 Esc），不向 shell 冒泡。

    if (!result.finished) {
        RefreshStatusText();  // 无修饰键提示，保持录制
        return;
    }
    vm_.DetectConflicts();
    RefreshAllRows();
    RefreshStatusText();
    if (!result.cancelled) PersistBindings();
}

// ---------------------------------------------------------------------------
// 刷新
// ---------------------------------------------------------------------------
void ShortcutsPage::RefreshRow(int row) {
    const bool recording = vm_.RecordingRow() == row;
    recordButtons_[row].Content(winrt::box_value(
        recording ? kit::Lx("win.shortcuts.recording", L"录制中…")
                  : winrt::to_hstring(vm_.ComboTextForRow(row))));
    recordButtons_[row].FontWeight(recording ? Windows::UI::Text::FontWeights::Bold()
                                             : Windows::UI::Text::FontWeights::Normal());
    // 冲突标红：组合按钮红字 + 行下红色说明
    recordButtons_[row].Foreground(
        vm_.RowHasConflict(row)
            ? RedBrush()
            : kit::ThemeBrush(L"TextFillColorPrimaryBrush", Windows::UI::Color{0xFF, 0x00, 0x00, 0x00}));
    if (vm_.RowHasConflict(row)) {
        conflictTexts_[row].Text(winrt::to_hstring(vm_.ConflictMessageForRow(row)));
        conflictTexts_[row].Visibility(Visibility::Visible);
    } else {
        conflictTexts_[row].Visibility(Visibility::Collapsed);
    }
}

void ShortcutsPage::RefreshAllRows() {
    for (int i = 0; i < kShortcutRowCount; ++i) RefreshRow(i);
}

void ShortcutsPage::RefreshStatusText() {
    if (vm_.HasStatus()) {
        statusText_.Text(vm_.StatusMessage());
        statusText_.Visibility(Visibility::Visible);
    } else {
        statusText_.Visibility(Visibility::Collapsed);
    }
}

// ---------------------------------------------------------------------------
// 持久化：前 5 行 → HotkeySettingsStore；第 6 行（pickColor）→ schema §1
// 键 "TransFrame.pickColorShortcut" 单独合并（store 保留未知键，共存安全）。
// ---------------------------------------------------------------------------
void ShortcutsPage::PersistBindings() {
    std::string json = tf::core::serializeHotkeyBindings(store::ReadSettingsJson(),
                                                         vm_.BindingsForStore());
    const auto pick = vm_.PickColorBinding();
    json = store::MergeSettingsKey(
        json, kPickColorKey,
        nlohmann::json{{"vk", pick.vk}, {"modifiers", pick.modifiers}});
    if (store::WriteSettingsJsonAtomic(json)) {
        statusText_.Text(kit::Lx("win.shortcuts.saved", L"快捷键已保存"));
    } else {
        statusText_.Text(kit::Lx("win.common.saveFailedSettings", L"保存失败（settings.json 不可写）"));
    }
    statusText_.Visibility(Visibility::Visible);
}

}  // namespace tf::ui
