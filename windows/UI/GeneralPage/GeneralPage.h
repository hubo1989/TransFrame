// windows/UI/GeneralPage/GeneralPage.h
// FR-35 通用页签 code-behind —— 纯代码构建 WinUI 控件（无 XAML 编译，
// 同 SettingsShell 的构建路线；GeneralPage.xaml 不存在，规格参考
// settings-spec-mac-parity.md §1 + PRD FR-35 通用页签条目）。
//
// 卡片：保存位置 / 导出格式与质量 / 界面语言 / 启动与更新。
// 持久化：store 字段经 VM（EngineConfigStore patch 合并）；开机自启 =
// HKCU Run 键（注册表即持久化）；更新检查 = settings.json
// "TransFrame.checkForUpdates.win"（PRD FR-60 .win 命名空间）。
#pragma once

#include "GeneralPageViewModel.hpp"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace tf::ui {

class GeneralPage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<GeneralPage> {
public:
    GeneralPage();

    /// shell 页签激活时调用：从磁盘重载（其它页签可能改过 engines.json）。
    void OnShown();

private:
    void BuildUi();
    void SyncFromViewModel();
    void PersistWithStatus();
    void PickSaveFolder();
    void RevealSaveLocation();

    // 控件（构造时创建，仅 STA 访问）
    winrt::Microsoft::UI::Xaml::Controls::TextBlock savePathText_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox formatCombo_;
    winrt::Microsoft::UI::Xaml::Controls::Slider qualitySlider_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock qualityValueText_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox languageCombo_;
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch launchAtLoginToggle_;
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch checkUpdatesToggle_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock statusText_;

    GeneralPageViewModel vm_;
    bool updating_ = false;  // 回填触发的选择变化不落盘
};

}  // namespace tf::ui
