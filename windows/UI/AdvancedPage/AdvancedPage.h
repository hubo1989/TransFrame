// windows/UI/AdvancedPage/AdvancedPage.h
// 高级页签 code-behind（C++/WinRT 纯头投影，无 .idl / 无 C++/CX）。
// 命名空间 winrt::TransFrame::UI::implementation；x:Bind 解析到本类公有成员/方法。
// 持有 AdvancedPageViewModel（普通 C++ 类），事件驱动 + code-behind 同步：
// Slider ValueChanged / 色钮 Click / 重置确认后统一走 VM，再刷新 UI 与持久化。
// （Slider.Value 是依赖属性可 TwoWay，但为保持与 VM 单写者一致，采用事件驱动同步。）

#pragma once

#include "AdvancedPageViewModel.h"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Xaml.h>
#include <winrt/Windows.UI.Xaml.Controls.h>
#include <winrt/Windows.UI.Xaml.Controls.Primitives.h>
#include <winrt/Windows.UI.Xaml.Media.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#if __has_include("AdvancedPage.g.h")
#include "AdvancedPage.g.h"
#endif

namespace winrt::TransFrame::UI::implementation {

// [待确认] spec §6：是否另有自定义取色器；当前保守选择：仅 9 个预设色圆形按钮，不加取色器。

struct AdvancedPage : AdvancedPageT<AdvancedPage> {
public:
    AdvancedPage();

    // --- x:Bind 目标（与 AdvancedPage.xaml 路径一字不差） ---
    // Slider 旁显当前值（OneWay，格式保留 1 位小数）。
    hstring StrokeWidthText() const;
    hstring TextSizeText() const;

    // --- 事件处理器（签名符合 C++/WinRT 委托） ---
    void OnStrokeWidthChanged(Windows::Foundation::IInspectable const& sender,
                              Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
    void OnTextSizeChanged(Windows::Foundation::IInspectable const& sender,
                           Windows::UI::Xaml::Controls::Primitives::RangeBaseValueChangedEventArgs const& args);
    void OnColorButtonClick(Windows::Foundation::IInspectable const& sender,
                            Windows::UI::Xaml::RoutedEventArgs const& args);
    void OnResetAllClick(Windows::Foundation::IInspectable const& sender,
                         Windows::UI::Xaml::RoutedEventArgs const& args);

    // --- ColorConfig ↔ Windows::UI::Color 转换器（VM 静态要求：写成 code-behind 静态函数） ---
    static Windows::UI::Color ToUiColor(tf::core::ColorConfig const& c);
    static tf::core::ColorConfig FromUiColor(Windows::UI::Color const& c);

private:
    // 初始化时用 VM 当前值填充控件（Slider 初值 / 色钮选中态）。
    void SyncFromViewModel();
    // 重建 9 个预设色圆形按钮（描边按钮动态生成，避免 XAML 重复 9 段）。
    void BuildColorButtons();
    // 更新色钮选中外环（选中项 Border 双层描边）。
    void UpdateColorSelectionVisuals();
    // VM 值变化 → 落盘 + 通知 shell。
    void PersistAndNotifyShell();

    AdvancedPageViewModel m_viewModel{tf::core::EngineConfig::makeDefault()};
    // 动态色钮数组（顺序同 VM::presetColors()），索引即预设下标。
    std::vector<Windows::UI::Xaml::Controls::Button> m_colorButtons;

    // [集成点] shell 侧需注入：engines.json 现有内容（%APPDATA%\TransFrame\engines.json）。
    //          页面构造后由 shell 调用 SetExistingJson / 或扩展构造器传入。
    std::string m_existingJson;

    // [集成点] shell 侧需提供：全部重置确认后重新注册热键 + 重绘叠加层
    //          （mac: appDelegate.updateHotkeys()）。本页只发状态，具体回调由 shell 挂接。
    //          建议 shell 侧提供 winrt::TransFrame::UI::IShellNotifier 挂到本页，
    //          或在 PersistAndNotifyShell 内直接调用 NativeShell 的 HotkeyManager 句柄。
};

} // namespace winrt::TransFrame::UI::implementation

namespace winrt::TransFrame::UI::factory_implementation {
// UserControl 无需 factory；保留空命名空间占位避免误加 .idl。
}
