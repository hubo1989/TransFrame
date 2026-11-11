// windows/UI/AdvancedPage/AdvancedPage.cpp
// FR-35 高级页签 code-behind 实现（控件清单见 .h 文件头）。
#include "AdvancedPage.h"
#include "SettingsFileStore.hpp"
#include "SettingsUiKit.hpp"

#include "tf/EngineConfigStore.hpp"

#include <windows.h>
#include <shellapi.h>

#include <chrono>
#include <cstdio>
#include <filesystem>

#pragma comment(lib, "shell32.lib")

namespace tf::ui {
namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;
namespace vmns = winrt::TransFrame::UI;

// ColorConfig(0..1 double) ↔ Windows::UI::Color(0..255)。
Windows::UI::Color ToUiColor(const tf::core::ColorConfig& c) {
    auto clamp01 = [](double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
    return Windows::UI::Color{
        static_cast<std::uint8_t>(clamp01(c.opacity) * 255.0 + 0.5),
        static_cast<std::uint8_t>(clamp01(c.red) * 255.0 + 0.5),
        static_cast<std::uint8_t>(clamp01(c.green) * 255.0 + 0.5),
        static_cast<std::uint8_t>(clamp01(c.blue) * 255.0 + 0.5),
    };
}

void OpenInExplorer(const std::wstring& dir) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);  // 首次打开时建立
    ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::wstring TimestampedName(const wchar_t* prefix) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t buf[64]{};
    wcsftime(buf, 64, L"%Y%m%d-%H%M%S", &local);
    return std::wstring(prefix) + L"-" + buf + L".json";
}

}  // namespace

AdvancedPage::AdvancedPage()
    : vm_(vmns::AdvancedPageViewModel(
          tf::core::parseEngineConfig(store::ReadEnginesJson()).config)) {
    BuildUi();
    SyncFromViewModel();
}

void AdvancedPage::OnShown() {
    vm_ = vmns::AdvancedPageViewModel(
        tf::core::parseEngineConfig(store::ReadEnginesJson()).config);
    SyncFromViewModel();
}

void AdvancedPage::BuildUi() {
    Controls::StackPanel root = kit::MakePageRoot(*this);
    root.Children().Append(kit::MakeSectionHeader(
        kit::Lx("win.settings.section.advanced", L"高级").c_str()));

    // ---------------- 卡片 1：标注默认样式（spec §6） ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("win.advanced.annotations.title", L"标注默认样式").c_str(),
            kit::Lx("win.advanced.annotations.hint", L"新标注的描边颜色、宽度与文本大小").c_str()));

        Controls::StackPanel colors;
        colors.Orientation(Controls::Orientation::Horizontal);
        colors.Spacing(8.0);
        colorPanel_ = colors;
        card.panel.Children().Append(colors);

        strokeWidthSlider_ = Controls::Slider();
        strokeWidthSlider_.Header(winrt::box_value(
            kit::Lx("settings.stroke.width", L"描边宽度")));
        strokeWidthSlider_.Minimum(1.0);
        strokeWidthSlider_.Maximum(20.0);
        strokeWidthSlider_.StepFrequency(0.5);
        strokeWidthSlider_.Width(280.0);
        strokeWidthValueText_ = Controls::TextBlock();
        strokeWidthValueText_.VerticalAlignment(VerticalAlignment::Center);
        strokeWidthValueText_.MinWidth(40.0);
        Controls::StackPanel widthRow;
        widthRow.Spacing(12.0);
        widthRow.Children().Append(strokeWidthSlider_);
        widthRow.Children().Append(strokeWidthValueText_);
        card.panel.Children().Append(widthRow);
        strokeWidthSlider_.ValueChanged([this](auto&&, auto&& args) {
            if (updating_) return;
            vm_.setStrokeWidth(args.NewValue());
            wchar_t buf[16]{};
            swprintf(buf, 16, L"%.1f", vm_.strokeWidth());
            strokeWidthValueText_.Text(buf);
        });
        strokeWidthSlider_.PointerCaptureLost([this](auto&&, auto&&) { Persist(); });

        textSizeSlider_ = Controls::Slider();
        textSizeSlider_.Header(winrt::box_value(
            kit::Lx("settings.text.size", L"文本大小")));
        textSizeSlider_.Minimum(8.0);
        textSizeSlider_.Maximum(72.0);
        textSizeSlider_.StepFrequency(1.0);
        textSizeSlider_.Width(280.0);
        textSizeValueText_ = Controls::TextBlock();
        textSizeValueText_.VerticalAlignment(VerticalAlignment::Center);
        textSizeValueText_.MinWidth(40.0);
        Controls::StackPanel sizeRow;
        sizeRow.Spacing(12.0);
        sizeRow.Children().Append(textSizeSlider_);
        sizeRow.Children().Append(textSizeValueText_);
        card.panel.Children().Append(sizeRow);
        textSizeSlider_.ValueChanged([this](auto&&, auto&& args) {
            if (updating_) return;
            vm_.setTextSize(args.NewValue());
            wchar_t buf[16]{};
            swprintf(buf, 16, L"%.0f", vm_.textSize());
            textSizeValueText_.Text(buf);
        });
        textSizeSlider_.PointerCaptureLost([this](auto&&, auto&&) { Persist(); });

        root.Children().Append(card.border);
    }

    // ---------------- 卡片 2：数据与日志（PRD FR-35 高级条目） ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("win.advanced.data.title", L"数据与日志").c_str()));

        Controls::StackPanel dataRow;
        dataRow.Orientation(Controls::Orientation::Horizontal);
        dataRow.Spacing(8.0);
        Controls::TextBlock dataPath = kit::MakeText(store::RoamingDir().empty()
                                                         ? L"%APPDATA%\\TransFrame"
                                                         : store::RoamingDir());
        dataPath.TextTrimming(TextTrimming::CharacterEllipsis);
        dataPath.TextWrapping(TextWrapping::NoWrap);
        dataPath.MaxWidth(320.0);
        dataPath.VerticalAlignment(VerticalAlignment::Center);
        Controls::Button openData;
        openData.Content(winrt::box_value(
            kit::Lx("win.advanced.openDataDir", L"打开数据目录")));
        openData.Click([](auto&&, auto&&) { OpenInExplorer(store::RoamingDir()); });
        dataRow.Children().Append(dataPath);
        dataRow.Children().Append(openData);
        card.panel.Children().Append(dataRow);

        Controls::StackPanel logRow;
        logRow.Orientation(Controls::Orientation::Horizontal);
        logRow.Spacing(8.0);
        // 显示展开后的真实路径（与数据目录行一致）；定位失败才退回字面量。
        const std::wstring logDir =
            store::LocalDir().empty() ? std::wstring(L"%LOCALAPPDATA%\\TransFrame\\logs")
                                      : store::LocalDir() + L"\\logs";
        Controls::TextBlock logText = kit::MakeText(logDir);
        logText.TextTrimming(TextTrimming::CharacterEllipsis);
        logText.TextWrapping(TextWrapping::NoWrap);
        logText.MaxWidth(320.0);
        logText.VerticalAlignment(VerticalAlignment::Center);
        Controls::Button openLogs;
        openLogs.Content(winrt::box_value(kit::Lx("win.advanced.openLogs", L"打开日志")));
        openLogs.Click([](auto&&, auto&&) {
            std::wstring dir = store::LocalDir() + L"\\logs";
            if (store::LocalDir().empty()) dir = L"%LOCALAPPDATA%\\TransFrame\\logs";
            OpenInExplorer(dir);
        });
        logRow.Children().Append(logText);
        logRow.Children().Append(openLogs);
        card.panel.Children().Append(logRow);

        Controls::StackPanel perfRow;
        perfRow.Orientation(Controls::Orientation::Horizontal);
        perfRow.Spacing(8.0);
        Controls::Button exportPerf;
        exportPerf.Content(winrt::box_value(
            kit::Lx("win.advanced.exportPerf", L"导出性能报告")));
        exportPerf.Click([this](auto&&, auto&&) { ExportPerformanceReport(); });
        Controls::TextBlock perfNote = kit::MakeCaption(
            kit::Lx("win.advanced.perfNote", L"环境快照（FR-40 埋点接入后为全链路数据）").c_str());
        perfNote.VerticalAlignment(VerticalAlignment::Center);
        perfRow.Children().Append(exportPerf);
        perfRow.Children().Append(perfNote);
        card.panel.Children().Append(perfRow);

        root.Children().Append(card.border);
    }

    // ---------------- 卡片 3：全部重置（spec §6） ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("win.advanced.reset.title", L"重置").c_str(),
            kit::Lx("win.advanced.reset.hint", L"把描边颜色、描边宽度、文本大小恢复为默认值（不影响其它设置）").c_str()));
        Controls::Button reset;
        reset.Content(winrt::box_value(kit::Lx("win.advanced.resetAll", L"全部重置")));
        reset.Click([this](auto&&, auto&&) { ShowResetConfirmAsync(); });
        card.panel.Children().Append(reset);
        root.Children().Append(card.border);
    }

    statusText_ = Controls::TextBlock();
    statusText_.FontSize(12.0);
    statusText_.Opacity(0.7);
    root.Children().Append(statusText_);
}

void AdvancedPage::SyncFromViewModel() {
    updating_ = true;

    colorPanel_.Children().Clear();
    colorSwatches_.clear();
    const auto& presets = vmns::AdvancedPageViewModel::presetColors();
    for (std::size_t i = 0; i < presets.size(); ++i) {
        Controls::Button btn;
        btn.Padding({0, 0, 0, 0});
        btn.MinWidth(0.0);
        btn.MinHeight(0.0);
        btn.Width(34.0);
        btn.Height(34.0);
        btn.CornerRadius({17.0, 17.0, 17.0, 17.0});

        winrt::Microsoft::UI::Xaml::Shapes::Ellipse fill;
        fill.Width(24.0);
        fill.Height(24.0);
        winrt::Microsoft::UI::Xaml::Media::SolidColorBrush brush;
        brush.Color(ToUiColor(presets[i].color));
        fill.Fill(brush);
        Controls::Border swatch;
        swatch.Child(fill);
        swatch.CornerRadius({17.0, 17.0, 17.0, 17.0});
        btn.Content(swatch);

        const std::size_t index = i;
        btn.Click([this, index](auto&&, auto&&) {
            const auto& p = vmns::AdvancedPageViewModel::presetColors()[index];
            if (vm_.setStrokeColor(p.color)) {
                UpdateColorSelectionVisuals();
                Persist();
            }
        });
        colorPanel_.Children().Append(btn);
        colorSwatches_.push_back(swatch);
    }

    strokeWidthSlider_.Value(vm_.strokeWidth());
    textSizeSlider_.Value(vm_.textSize());
    wchar_t buf[16]{};
    swprintf(buf, 16, L"%.1f", vm_.strokeWidth());
    strokeWidthValueText_.Text(buf);
    swprintf(buf, 16, L"%.0f", vm_.textSize());
    textSizeValueText_.Text(buf);
    UpdateColorSelectionVisuals();
    updating_ = false;
}

void AdvancedPage::UpdateColorSelectionVisuals() {
    const auto& presets = vmns::AdvancedPageViewModel::presetColors();
    for (std::size_t i = 0; i < colorSwatches_.size() && i < presets.size(); ++i) {
        const bool selected = vm_.isColorSelected(presets[i].color);
        Controls::Border swatch = colorSwatches_[i];
        swatch.BorderThickness(Thickness(selected ? 3.0 : 1.0));
        swatch.BorderBrush(selected
                               ? kit::ThemeBrush(L"SystemAccentColor",
                                                 Windows::UI::Color{0xFF, 0x00, 0x63, 0xB1})
                               : kit::ThemeBrush(
                                     L"ControlStrokeColorDefaultBrush",
                                     Windows::UI::Color{0xFF, 0x60, 0x60, 0x60}));
        swatch.Padding(Thickness(selected ? 3.0 : 0.0));
    }
}

void AdvancedPage::Persist() {
    const std::string merged =
        tf::core::serializeEngineConfigPatch(store::ReadEnginesJson(), vm_.persistencePatch());
    if (store::WriteEnginesJsonAtomic(merged)) {
        statusText_.Text(kit::Lx("win.common.saved", L"已保存"));
    } else {
        statusText_.Text(kit::Lx("win.common.saveFailedEngines", L"保存失败（engines.json 不可写）"));
    }
}

winrt::Windows::Foundation::IAsyncAction AdvancedPage::ShowResetConfirmAsync() {
    Controls::ContentDialog dialog;
    dialog.Title(winrt::box_value(kit::Lx("win.advanced.resetAll", L"全部重置")));
    dialog.Content(winrt::box_value(
        kit::Lx("win.advanced.resetDialog.body", L"将把描边颜色、描边宽度和文本大小恢复为默认值。此操作无法撤销。")));
    dialog.PrimaryButtonText(kit::Lx("win.advanced.reset.title", L"重置"));
    dialog.CloseButtonText(kit::Lx("action.cancel", L"取消"));
    dialog.DefaultButton(Controls::ContentDialogButton::Close);
    dialog.XamlRoot(XamlRoot());

    const Controls::ContentDialogResult result = co_await dialog.ShowAsync();
    if (result == Controls::ContentDialogResult::Primary) {
        vm_.resetToDefaults();
        SyncFromViewModel();
        Persist();
    }
    co_return;
}

void AdvancedPage::ExportPerformanceReport() {
    // FR-40 PerformanceRecorder 尚未接入：导出当前可用的环境/设置快照，
    // 报告目录与命名保留给全链路数据。
    const std::wstring dir = store::LocalDir() + L"\\reports";
    if (store::LocalDir().empty()) {
        statusText_.Text(kit::Lx("win.advanced.noLocalAppdata", L"无法定位 %LOCALAPPDATA%"));
        return;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    const std::filesystem::path out = dir + L"\\" + TimestampedName(L"performance");
    FILE* f = nullptr;
    if (_wfopen_s(&f, out.c_str(), L"wb") != 0 || !f) {
        statusText_.Text(kit::Lx("win.advanced.exportFailed", L"性能报告导出失败"));
        return;
    }
    std::fprintf(f,
                 "{\n"
                 "  \"kind\": \"performance-snapshot\",\n"
                 "  \"generatedAtUnix\": %lld,\n"
                 "  \"app\": \"TransFrame (Windows)\",\n"
                 "  \"annotation\": { \"strokeWidth\": %.1f, \"textSize\": %.0f },\n"
                 "  \"note\": \"FR-40 PerformanceRecorder 未接入；当前导出为环境快照\"\n"
                 "}\n",
                 static_cast<long long>(std::time(nullptr)), vm_.strokeWidth(),
                 vm_.textSize());
    std::fclose(f);
    statusText_.Text(core::loc::formatArgsWide(
        kit::Lx("win.advanced.exported", L"已导出 %@"), {out.wstring()}));
    OpenInExplorer(dir);
}

}  // namespace tf::ui
