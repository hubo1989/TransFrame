// windows/UI/EnginesPage/EnginesPage.xaml.h
// FR-35 引擎页签 code-behind（头）—— C++/WinRT 纯头投影，不用 /ZW。
//
// UserControl 不进 XamlTypeInfo：本页由 shell 直接实例化
// （winrt::make<tf::ui::EnginesPage>()），XAML 生成头
// EnginesPage.xaml.g.h 由 XAML 编译器产出，需与本项目一同编译
// （shell 侧 vcxproj 把本目录的 EnginesPage.xaml 作为 <Page> 项、
//  本 .h/.cpp 作为 ClCompile/ClInclude，生成器 winrt::xamlcodegen 已在
//  Probe 项目验证 —— winui3-migration.md §5）。
#pragma once

#include "EnginesPageViewModel.hpp"

// WinUI 3 投影头（XAML 编译器生成的生成头）：
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Microsoft.UI.Xaml.Navigation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.UI.h>

#include <memory>
#include <string>

// 前向：XAML 生成头（若未生成则跳过 —— 集成阶段由 xamlcodegen 产出）。
#if __has_include("EnginesPage.xaml.g.h")
#include "EnginesPage.xaml.g.h"
#endif

namespace tf::ui {

// x:Bind 集合行的 WinRT 投影（ChainRowViewModel / CompatibleInstanceRow 是
// 纯 C++ 结构，x:Bind 集合绑定经这些运行时类投影走 ItemsControl）。
struct ChainRow : winrt::implements<ChainRow, winrt::Microsoft::UI::Xaml::Data::INotifyPropertyChanged> {
    // 简化：链/对比行与引擎卡由 code-behind 命令式构建（本页绑定策略），
    // 此类仅保留给后续 ItemsSource 迁移 —— 当前实现不依赖。
};

/// 引擎页（spec §2 全部控件）。
class EnginesPage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<EnginesPage> {
public:
    EnginesPage();

    // x:Bind 数据源：页面属性。XAML 里绑定路径 ViewModel.*（x:Bind 编译期
    // 生成的访问代码会调这两个方法）。
    tf::ui::EnginesPageViewModel& ViewModel() { return *vm_; }
    const tf::ui::EnginesPageViewModel& ViewModel() const { return *vm_; }

    // ---- XAML 事件处理器（签名与 EnginesPage.xaml 的 x:Name 一一对应）----
    void OnVlmProviderChanged(winrt::IInspectable const& sender,
                              winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
    void OnVlmOpenAIModeChanged(winrt::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
    void OnVlmBaseURLCommitted(winrt::IInspectable const& sender,
                               winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnVlmModelNameCommitted(winrt::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnSaveVlmClick(winrt::IInspectable const& sender,
                        winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    void OnPaddleModeChanged(winrt::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
    void OnPaddleUseCloudChanged(winrt::IInspectable const& sender,
                                 winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnPaddleCloudBaseURLCommitted(winrt::IInspectable const& sender,
                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnPaddleCloudAPIKeyChanged(winrt::IInspectable const& sender,
                                    winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnPaddleCloudModelIdCommitted(winrt::IInspectable const& sender,
                                       winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnSavePaddleClick(winrt::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

    void OnEngineModeChanged(winrt::IInspectable const& sender,
                             winrt::Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& e);
    void OnSaveChainClick(winrt::IInspectable const& sender,
                          winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnSaveCompareClick(winrt::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);

private:
    // ---- 命令式 UI 构建（无 XamlTypeInfo 路径的页面策略：XAML 骨架 + 动态填充）----
    void LoadFromViewModel();          // Loaded 时一次性拉平 VM → 控件
    void RebuildChainRows();
    void RebuildCompareRows();
    void RebuildEngineCards();
    void RebuildAddChainMenu();
    void RebuildAddCompareMenu();
    void UpdateModeSectionVisibility();
    void UpdateVlmOpenAIModeVisibility();
    void UpdatePaddleCloudVisibility();

    // Sheet 入口（ContentDialog，spec §2.4 / §2.5）
    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
        ShowEngineConfigSheetAsync(const std::string& engineRaw);
    winrt::Windows::Foundation::IAsyncOperation<winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
        ShowCompatibleSheetAsync(const std::string& instanceId);

    // 健康探测（EngineHealthChecker 由集成方注入；未注入时全灰——
    // VM.HealthOf 默认 "gray"，四态点颜色映射见 VM.HealthColorHex）。
    void RefreshHealthDots();

    // 集成注入点：engines.json 持久化闭包（构造前由 shell 提供；默认纯内存）。
    void SetPersistence(EnginesPageViewModel::Persistence p) { vm_->~EnginesPageViewModel(); new (&*vm_) EnginesPageViewModel(std::move(p)); }

    std::unique_ptr<EnginesPageViewModel> vm_;
    // 临时 Paddle 云端 API Key 输入缓冲（提交保存时经 VM 钩子落 CredMan）。
    std::string paddleApiKeyDraft_;
};

}  // namespace tf::ui
