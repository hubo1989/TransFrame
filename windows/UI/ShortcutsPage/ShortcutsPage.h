// tf/ShortcutsPage.h — 快捷键页签 code-behind（C++/WinRT 纯头投影，无 .idl）
// x:Bind 对 UserControl 根解析到本类公有成员；XAML 中每个 x:Bind 路径
// 必须与这里的声明一字不差（见下）。
// 动态状态（录制高亮 / 冲突标红 / 组合文本刷新 / 状态提示）走事件驱动 +
// code-behind 同步（VM 是普通 C++ 类，无 WinRT 属性通知，故不用 Mode=OneWay）。
#ifndef TF_SHORTCUTS_PAGE_H
#define TF_SHORTCUTS_PAGE_H

#include "ShortcutsPageViewModel.h"

// [集成点] winrt 投影头按工程配置引入（Windows SDK + C++/WinRT NuGet）：
//           实际文件名形如 "ShortcutsPage.g.h"（XAML 编译器生成） +
//           "ShortcutsPage.xaml.h"（如项目采用生成头模式）。本文件按
//           "目录内四文件" 硬约定给出手工声明版；若项目改用 xaml-generated
//           头（ShortcutsPage.g.h），将本类 body 迁入并删除重复声明。
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Input.h>
#include <winrt/Windows.System.h>

#include <memory>

namespace winrt::TransFrame::UI::implementation {

struct ShortcutsPage : winrt::Windows::UI::Xaml::Controls::UserControlT<ShortcutsPage> {
    ShortcutsPage();

    // ---- x:Bind 目标（XAML 路径与这里一字不差）----
    winrt::hstring HeaderTextValue() const { return L"快捷键"; }
    winrt::hstring HintTextValue() const {
        return L"点击组合按钮后按下新组合键（需含至少一个修饰键），Esc 取消录制；冲突的快捷键会标红";
    }
    winrt::hstring ResetButtonText() const { return L"重置"; }
    winrt::hstring ActionLabelFor(int row) const;   // → ActionLabelForRow(row)
    winrt::hstring ComboTextFor(int row) const;     // → VM::ComboTextForRow(row)（OneTime 初值；录制后由 code-behind 刷新）
    winrt::hstring RecordButtonTextFor(int row) const; // → 录制中 ? "录制中…" : "点击录制"

    // ---- 事件处理器（XAML Click/KeyDown 绑定；签名符合 C++/WinRT 委托）----
    void OnKeyDown(winrt::Windows::Foundation::IInspectable const& sender,
                   winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e);
    void OnRecordButton0Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnRecordButton1Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnRecordButton2Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnRecordButton3Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnRecordButton4Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnRecordButton5Click(winrt::Windows::Foundation::IInspectable const& sender,
                              winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnResetButton0Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnResetButton1Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnResetButton2Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnResetButton3Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnResetButton4Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Windows::UI::Xaml::RoutedEventArgs const& e);
    void OnResetButton5Click(winrt::Windows::Foundation::IInspectable const& sender,
                             winrt::Windows::UI::Xaml::RoutedEventArgs const& e);

    // VM（普通 C++ 类，页面持有）
    tf::ui::ShortcutsPageViewModel& ViewModel() { return *viewModel_; }

private:
    std::unique_ptr<tf::ui::ShortcutsPageViewModel> viewModel_;

    // x:Name 引用容器（Name 属性在 XAML 编译器生成基类里；此处通过
    // [集成点] 生成的 ShortcutsPage.g.h 字段访问。若手工编译无生成头，
    // 用 FindName 查找。以下以生成头字段为准的访问器：）
    winrt::Windows::UI::Xaml::Controls::TextBlock ConflictTextAt(int row);
    winrt::Windows::UI::Xaml::Controls::TextBlock ComboTextAt(int row);
    winrt::Windows::UI::Xaml::Controls::Button RecordButtonAt(int row);
    winrt::Windows::UI::Xaml::Controls::TextBlock StatusTextControl();

    void HandleRecordClick(int row);
    void HandleResetClick(int row);
    void RefreshRow(int row);          // 组合文本 + 录制按钮文案 + 录制高亮 + 冲突状态
    void RefreshAllRows();
    void RefreshStatusText();
    void PersistBindings();            // 写盘 + 触发 shell 重注册
};

}  // namespace winrt::TransFrame::UI::implementation

#endif  // TF_SHORTCUTS_PAGE_H
