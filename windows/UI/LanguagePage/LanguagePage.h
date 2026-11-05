#pragma once
// LanguagePage.h — 语言页签 code-behind（UserControl 根，嵌入 NavigationView Content）。
//
// 统一约定（四页一致）：
// - C++/WinRT 纯头投影：只 include winrt 投影头，不写 .idl、不用 C++/CX。
// - VM（tf::ui::LanguagePageViewModel）是普通 C++ 类，由本类持有，不依赖
//   WinRT 属性系统。
// - 本页无 x:Bind 路径：ComboBox 选中项与普通 C++ VM 的同步走
//   SelectionChanged 事件 + code-behind 回填（TwoWay 到非依赖属性源需要
//   WinRT 属性通知，按统一约定改事件驱动；后续若加纯展示文本的 x:Bind，
//   路径必须与本文件公有声明一字不差）。
//
// [集成点] "LanguagePage.g.h" 由 XAML 编译器按 x:Class 生成（InitializeComponent、
// x:Name 成员访问器如 GlobalSourceLanguagePicker() 均来自它）；本目录加入
// vcxproj 时 XAML 以 Page 条目登记并启用 XAML 编译。
// [集成点] 宿主在 C++ 代码里实例化本页需要激活工厂：若工具链要求，由 shell 侧
// 统一补 .idl / factory_implementation（本页按约定不新增）。

#include "LanguagePage.g.h"

#include "LanguagePageViewModel.h"

#include "tf/EngineConfigStore.hpp"

#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::TransFrame::UI::implementation {

struct LanguagePage : LanguagePageT<LanguagePage> {
    LanguagePage();

    // 宿主注入配置对象（XAML 实例化无法走带参构造）；注入后按配置值回填四个 Picker。
    // [集成点] 传入的对象由设置窗口持有：读自 engines.json，写回时机亦由宿主决定。
    void AttachEngineConfig(tf::core::EngineConfig& config);

    // 事件处理器（与 LanguagePage.xaml 的 SelectionChanged 属性一字不差；
    // 签名符合 C++/WinRT SelectionChanged 委托）。
    void OnGlobalSourceLanguageSelected(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
    void OnGlobalTargetLanguageSelected(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
    void OnInsertSourceLanguageSelected(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);
    void OnInsertTargetLanguageSelected(
        winrt::Windows::Foundation::IInspectable const& sender,
        winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& args);

private:
    // 四个 Picker 的角色划分（回填初始选中与变化落库共用 VM 的两张选项表）。
    enum class PickerRole { GlobalSource, GlobalTarget, InsertSource, InsertTarget };

    // 按 VM 选项表填充四个 ComboBox 并设置当前选中；条目 Tag 存 rawValue。
    // 未 Attach 时按默认值显示（源 auto / 目标“跟随系统”）。
    void RebuildPickers();

    // SelectionChanged 统一入口：从 ComboBoxItem.Tag 取 rawValue 写入 VM 对应字段。
    void ApplySelectionFromPicker(
        winrt::Windows::Foundation::IInspectable const& sender, PickerRole role);

    tf::ui::LanguagePageViewModel m_viewModel;
    bool m_updating = false;  // 回填初始选中期间触发的 SelectionChanged 不落库
};

}  // namespace winrt::TransFrame::UI::implementation
