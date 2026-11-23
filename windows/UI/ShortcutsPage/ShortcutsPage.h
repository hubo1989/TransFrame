// windows/UI/ShortcutsPage/ShortcutsPage.h
// FR-35 快捷键页签 code-behind —— 纯代码构建（原 ShortcutsPage.h/.cpp 面向
// Windows.UI.Xaml（UWP 投影）且依赖 XAML 生成头，重构为 WinUI 3
// Microsoft.UI.Xaml + 代码构建；ShortcutsPage.xaml 保留为规格参考，不编译）。
//
// 内容（spec §5）：一张卡片，六行录制控件（全屏截图/区域截图/翻译模式/
// 划词翻译/翻译插入/取色）。每行：动作名 + 组合按钮（点击进入录制，
// 显示"录制中…"）+ 重置按钮；冲突行下方红色提示。
// 键盘捕获：UserControl KeyDown（录制开始时 GrabFocus）。
// 持久化：前 5 行经 Core HotkeySettingsStore（settings.json）；
// pickColor 动作 store 未收录 [报告项] —— 本页按 schema §1 键
// "TransFrame.pickColorShortcut" 单独写 {vk, modifiers}（store 的
// serialize 保留未知键，两段可共存于同一文件）。
#pragma once

#include "ShortcutsPageViewModel.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

#include <array>
#include <string>

namespace tf::ui {

class ShortcutsPage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<ShortcutsPage> {
public:
    ShortcutsPage();

private:
    void BuildUi();
    void LoadFromDisk();
    void HandleRecordClick(int row);
    void HandleResetClick(int row);
    // 非虚命名（避免遮蔽基类 Control::OnKeyDown(KeyRoutedEventArgs) 虚函数）。
    void HandleKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                       winrt::Microsoft::UI::Xaml::Input::KeyRoutedEventArgs const& e);
    void RefreshRow(int row);
    void RefreshAllRows();
    void RefreshStatusText();
    void PersistBindings();

    std::array<winrt::Microsoft::UI::Xaml::Controls::Button, kShortcutRowCount> recordButtons_;
    std::array<winrt::Microsoft::UI::Xaml::Controls::Button, kShortcutRowCount> resetButtons_;
    std::array<winrt::Microsoft::UI::Xaml::Controls::TextBlock, kShortcutRowCount> conflictTexts_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock statusText_;

    ShortcutsPageViewModel vm_;
    bool updating_ = false;
};

}  // namespace tf::ui
