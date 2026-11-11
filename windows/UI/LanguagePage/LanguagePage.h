// windows/UI/LanguagePage/LanguagePage.h
// FR-35 语言页签 code-behind —— 纯代码构建（原 LanguagePage.h/.cpp 依赖
// XAML 编译器生成头 LanguagePage.g.h，Build Tools 上不可用，重构为与
// SettingsShell 同路线；LanguagePage.xaml 保留为规格参考，不编译）。
//
// 内容（spec §4）：一张卡片，全局翻译对 + 翻译插入对，各为
// 源语言（auto+全集）/ 目标语言（跟随系统+非 auto）两个下拉。
// VM（LanguagePageViewModel.h）Attach 到本页持有的 EngineConfig；
// 选择变化即写回 VM 并整份 patch 合并落盘（EngineConfigStore）。
// 界面语言卡只在通用页（mac 对齐：AppLanguagePicker 在
// GeneralSettingsTab；本页不重复）。
#pragma once

#include "LanguagePageViewModel.h"

#include "tf/EngineConfigStore.hpp"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

namespace tf::ui {

class LanguagePage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<LanguagePage> {
public:
    LanguagePage();

    /// shell 页签激活时调用：从磁盘重载（其它页签可能改过 engines.json）。
    void OnShown();

private:
    void BuildUi();
    void SyncFromConfig();
    void Persist();
    void ApplySelection(winrt::Microsoft::UI::Xaml::Controls::ComboBox const& combo, int role);

    enum class PickerRole { GlobalSource = 0, GlobalTarget, InsertSource, InsertTarget };

    winrt::Microsoft::UI::Xaml::Controls::ComboBox globalSourcePicker_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox globalTargetPicker_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox insertSourcePicker_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox insertTargetPicker_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock statusText_;

    tf::core::EngineConfig config_;  // 宿主持有（VM Attach 契约）
    LanguagePageViewModel vm_;
    bool updating_ = false;
};

}  // namespace tf::ui
