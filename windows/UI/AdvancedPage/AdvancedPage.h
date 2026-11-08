// windows/UI/AdvancedPage/AdvancedPage.h
// FR-35 高级页签 code-behind —— 纯代码构建（原 AdvancedPage.h/.cpp 面向
// Windows.UI.Xaml（UWP 投影）且依赖 XAML 生成头，重构为 WinUI 3 代码
// 构建；AdvancedPage.xaml 保留为规格参考，不编译）。
//
// 内容：
//   * 标注默认样式（spec §6）：描边颜色 9 预设 + 描边宽度 1–20 + 文本
//     大小 8–72，全部经 VM patch 合并落 engines.json。
//   * 数据与日志（PRD FR-35 高级页签条目）：数据目录（打开
//     %APPDATA%\TransFrame）、日志（打开 %LOCALAPPDATA%\TransFrame\logs）、
//     性能报告（导出环境快照 JSON 到 reports\ —— FR-40 埋点未接入，
//     当前仅快照，见交付差异清单）。
//   * 全部重置（spec §6）：destructive + ContentDialog 二次确认。
#pragma once

#include "AdvancedPageViewModel.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.Foundation.h>

#include <vector>

namespace tf::ui {

class AdvancedPage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<AdvancedPage> {
public:
    AdvancedPage();

    /// shell 页签激活时调用：从磁盘重载。
    void OnShown();

private:
    void BuildUi();
    void SyncFromViewModel();
    void UpdateColorSelectionVisuals();
    void Persist();
    winrt::Windows::Foundation::IAsyncAction ShowResetConfirmAsync();
    void ExportPerformanceReport();

    winrt::Microsoft::UI::Xaml::Controls::StackPanel colorPanel_;
    winrt::Microsoft::UI::Xaml::Controls::Slider strokeWidthSlider_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock strokeWidthValueText_;
    winrt::Microsoft::UI::Xaml::Controls::Slider textSizeSlider_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock textSizeValueText_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock statusText_;
    std::vector<winrt::Microsoft::UI::Xaml::Controls::Border> colorSwatches_;

    winrt::TransFrame::UI::AdvancedPageViewModel vm_;
    bool updating_ = false;
};

}  // namespace tf::ui
