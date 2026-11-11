// windows/UI/LanguagePage/LanguagePage.cpp
// FR-35 语言页签 code-behind 实现（纯代码构建；控件清单见 .h 文件头）。
// 界面语言卡在通用页（mac 对齐）；本页只管翻译语言对，切换经 VM
// patch 落盘 engines.json。
#include "LanguagePage.h"
#include "SettingsFileStore.hpp"
#include "SettingsUiKit.hpp"

#include <winrt/Windows.Globalization.h>

namespace tf::ui {
namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

std::string TagOf(winrt::Windows::Foundation::IInspectable const& item) {
    if (!item) return {};
    auto box = item.try_as<winrt::Windows::Foundation::IPropertyValue>();
    if (box && box.Type() == winrt::Windows::Foundation::PropertyType::String) {
        return winrt::to_string(box.GetString());
    }
    return {};
}

void FillPicker(Controls::ComboBox const& picker,
                const std::vector<LanguageOption>& options) {
    picker.Items().Clear();
    for (const LanguageOption& option : options) {
        // 语言名跟随系统 UI 语言（mac localizedName 的对等实现）；auto/
        // 空串两个特例与解析失败时退回 VM 的 zh 表。
        std::wstring display = option.displayName;
        if (!option.raw.empty() && option.raw != "auto") {
            try {
                winrt::Windows::Globalization::Language lang{
                    winrt::to_hstring(option.raw)};
                const winrt::hstring name = lang.DisplayName();
                if (!name.empty()) display = std::wstring(name.c_str());
            } catch (...) {
            }
        }
        Controls::ComboBoxItem item;
        item.Content(winrt::box_value(display));
        item.Tag(winrt::box_value(winrt::to_hstring(option.raw)));
        picker.Items().Append(item);
    }
}

void SelectRaw(Controls::ComboBox const& picker, const std::string& raw) {
    for (std::uint32_t i = 0; i < picker.Items().Size(); ++i) {
        if (TagOf(picker.Items().GetAt(i)) == raw) {
            picker.SelectedIndex(i);
            return;
        }
    }
    // 未知持久化值：保守回退下标 0（源=自动检测 / 目标=跟随系统），
    // 原值保留在 EngineConfig 中，用户不改动就不丢失。
    picker.SelectedIndex(0);
}

/// engines.json 顶层 appLanguage（schema §2.1；缺省 "system"）。
/// 读写只属于通用页的界面语言卡；本页不再触碰（mac 对齐）。

}  // namespace

LanguagePage::LanguagePage() {
    vm_.Attach(config_);
    if (const std::string json = store::ReadEnginesJson(); !json.empty()) {
        auto parsed = tf::core::parseEngineConfig(json);
        if (parsed.ok) config_ = std::move(parsed.config);
    }
    BuildUi();
    SyncFromConfig();
}

void LanguagePage::OnShown() {
    if (const std::string json = store::ReadEnginesJson(); !json.empty()) {
        auto parsed = tf::core::parseEngineConfig(json);
        if (parsed.ok) config_ = std::move(parsed.config);
    }
    SyncFromConfig();
}

void LanguagePage::BuildUi() {
    Controls::StackPanel root = kit::MakePageRoot(*this);
    root.Children().Append(kit::MakeSectionHeader(kit::Lx("settings.section.languages", L"语言").c_str()));

    // 界面语言卡在通用页（mac 对齐：AppLanguagePicker 只在
    // GeneralSettingsTab.swift:21）；本页只保留翻译语言对。
    kit::Card card = kit::MakeCard();
    card.panel.Children().Append(kit::MakeLabelBlock(
        kit::Lx("win.language.translation.title", L"翻译语言").c_str(),
        kit::Lx("win.language.translation.hint", L"全局翻译与翻译插入可分别设置语言对").c_str()));

    auto makeLanguageRow = [](winrt::hstring const& title, Controls::ComboBox& source,
                              Controls::ComboBox& target) {
        Controls::StackPanel row;
        row.Spacing(12.0);
        Controls::TextBlock titleText = kit::MakeText(title.c_str(), true);
        row.Children().Append(titleText);

        Controls::StackPanel pickers;
        pickers.Orientation(Controls::Orientation::Horizontal);
        pickers.Spacing(8.0);
        source.MinWidth(200.0);
        source.MaxWidth(240.0);
        target.MinWidth(200.0);
        target.MaxWidth(240.0);
        Controls::TextBlock arrow = kit::MakeText(L"→");
        arrow.VerticalAlignment(VerticalAlignment::Bottom);
        arrow.Margin({0, 0, 0, 8});
        pickers.Children().Append(source);
        pickers.Children().Append(arrow);
        pickers.Children().Append(target);
        row.Children().Append(pickers);
        return row;
    };

    card.panel.Children().Append(makeLanguageRow(kit::Lx("win.language.global", L"全局翻译"),
                                                 globalSourcePicker_,
                                                 globalTargetPicker_));
    card.panel.Children().Append(
        makeLanguageRow(kit::Lx("win.language.translateInsert", L"翻译插入"),
                        insertSourcePicker_, insertTargetPicker_));

    root.Children().Append(card.border);

    statusText_ = Controls::TextBlock();
    statusText_.FontSize(12.0);
    statusText_.Opacity(0.7);
    root.Children().Append(statusText_);

    auto wire = [this](Controls::ComboBox& combo, PickerRole role) {
        combo.SelectionChanged(
            [this, role](winrt::Windows::Foundation::IInspectable const& sender, auto&&) {
                if (updating_) return;  // 回填初始选中不落库
                auto picker = sender.as<Controls::ComboBox>();
                ApplySelection(picker, static_cast<int>(role));
            });
    };
    wire(globalSourcePicker_, PickerRole::GlobalSource);
    wire(globalTargetPicker_, PickerRole::GlobalTarget);
    wire(insertSourcePicker_, PickerRole::InsertSource);
    wire(insertTargetPicker_, PickerRole::InsertTarget);
}

void LanguagePage::SyncFromConfig() {
    updating_ = true;
    FillPicker(globalSourcePicker_, vm_.SourceLanguageOptions());
    FillPicker(globalTargetPicker_, vm_.TargetLanguageOptions());
    FillPicker(insertSourcePicker_, vm_.SourceLanguageOptions());
    FillPicker(insertTargetPicker_, vm_.TargetLanguageOptions());
    SelectRaw(globalSourcePicker_, vm_.GlobalSourceLanguage());
    SelectRaw(globalTargetPicker_, vm_.GlobalTargetLanguage());
    SelectRaw(insertSourcePicker_, vm_.InsertSourceLanguage());
    SelectRaw(insertTargetPicker_, vm_.InsertTargetLanguage());
    updating_ = false;
}

void LanguagePage::ApplySelection(Controls::ComboBox const& combo, int role) {
    if (!vm_.IsAttached()) return;
    auto item = combo.SelectedItem().try_as<Controls::ComboBoxItem>();
    if (!item) return;
    const std::string raw = TagOf(item);

    switch (static_cast<PickerRole>(role)) {
        case PickerRole::GlobalSource: vm_.SetGlobalSourceLanguage(raw); break;
        case PickerRole::GlobalTarget: vm_.SetGlobalTargetLanguage(raw); break;
        case PickerRole::InsertSource: vm_.SetInsertSourceLanguage(raw); break;
        case PickerRole::InsertTarget: vm_.SetInsertTargetLanguage(raw); break;
    }
    Persist();
}

void LanguagePage::Persist() {
    // 值已写入本页持有的 EngineConfig；整份 patch 合并回 engines.json
    //（serializeEngineConfigPatch 以磁盘文本为基，未知键全保留）。
    const std::string merged =
        tf::core::serializeEngineConfigPatch(store::ReadEnginesJson(), config_);
    if (store::WriteEnginesJsonAtomic(merged)) {
        statusText_.Text(kit::Lx("win.common.saved", L"已保存"));
    } else {
        statusText_.Text(kit::Lx("win.common.saveFailedEngines", L"保存失败（engines.json 不可写）"));
    }
}

}  // namespace tf::ui
