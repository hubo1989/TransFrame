// windows/UI/EnginesPage/EnginesPage.h
// FR-35 引擎页签 code-behind（重构为纯代码构建，spec §2 全部控件）。
//
// 原 EnginesPage.xaml.h/.cpp 依赖 XAML 编译器生成头（EnginesPage.xaml.g.h
// 的 x:Name 访问器）——Build Tools 上该管线不可用（winui3-migration.md §5），
// 现按 SettingsShell 同路线全部命令式构建；EnginesPage.xaml 保留为规格
// 参考，不编译。
//
// 内容：VLM 配置卡（spec §2.2 前半）、PaddleOCR 卡（§2.2 后半）、
// 多引擎翻译卡（§2.3：策略/顺序链/并行对比/九引擎卡+兼容实例），
// 两个配置 Sheet 以 ContentDialog 实现（§2.4/§2.5）。
// 健康状态点：VM 四态 + 「刷新健康状态」按钮经 Core EngineHealthChecker
// （CredManKeyStore + WinHttpTransport，后台线程探测、结果投递回 STA）。
// 凭证（API Key / Baidu AppID+Secret / PaddleOCR 云端 Key）：CredMan
// （NativeShell CredManKeyStore），明文不落 engines.json（FR-36）。
#pragma once

#include "EnginesPageViewModel.hpp"

#include "tf/CredManKeyStore.h"
#include "tf/EngineHealthChecker.hpp"
#include "tf/WinHttpTransport.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Dispatching.h>
#include <winrt/Windows.Foundation.h>

#include <memory>
#include <string>
#include <vector>

namespace tf::ui {

class EnginesPage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<EnginesPage> {
public:
    EnginesPage();

private:
    // ---- 构建骨架 ----
    void BuildVlmCard();
    void BuildPaddleCard();
    void BuildMultiEngineCard();
    void RebuildChainRows();
    void RebuildCompareRows();
    void RebuildAddMenus();
    void RebuildEngineCards();
    void UpdateModeSectionVisibility();
    void UpdateVlmOpenAIModeVisibility();
    void UpdatePaddleCloudVisibility();
    void RefreshVlmKeyIndicator();

    // ---- Sheet（ContentDialog，spec §2.4 / §2.5）----
    winrt::Windows::Foundation::IAsyncOperation<
        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
    ShowEngineConfigSheetAsync(const std::string& engineRaw);
    winrt::Windows::Foundation::IAsyncOperation<
        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
    ShowCompatibleSheetAsync(const std::string& instanceId);

    // ---- 健康探测（后台线程 + STA 回投）----
    /// 每次刷新新建（探针需要兼容实例快照；TTL 缓存在单次刷新内无意义）。
    std::shared_ptr<tf::EngineHealthChecker> MakeHealthChecker();
    void RefreshHealth();
    void ApplyHealthResults(const std::vector<std::pair<std::string, std::string>>& results);

    // ---- 持久化 ----
    void BindPersistence();   // VM 闭包接 store:: 文件层
    void PersistAllSections();// Save* 统一入口（闭包内合并未知键）

    // 控件成员（构造时创建，仅 STA 访问）
    winrt::Microsoft::UI::Xaml::Controls::ComboBox vlmProviderCombo_;
    winrt::Microsoft::UI::Xaml::Controls::TextBox vlmBaseUrlBox_;
    winrt::Microsoft::UI::Xaml::Controls::TextBox vlmModelBox_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox vlmOpenAIModeCombo_;
    winrt::Microsoft::UI::Xaml::Controls::PasswordBox vlmApiKeyBox_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock vlmKeyStatusText_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock paddleStatusText_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox paddleModeCombo_;
    winrt::Microsoft::UI::Xaml::Controls::ToggleSwitch paddleUseCloudToggle_;
    winrt::Microsoft::UI::Xaml::Controls::TextBox paddleCloudBaseUrlBox_;
    winrt::Microsoft::UI::Xaml::Controls::PasswordBox paddleCloudApiKeyBox_;
    winrt::Microsoft::UI::Xaml::Controls::TextBox paddleCloudModelBox_;
    winrt::Microsoft::UI::Xaml::Controls::ComboBox engineModeCombo_;
    winrt::Microsoft::UI::Xaml::Controls::Border orderedChainSection_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock chainCountText_;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel chainListPanel_;
    winrt::Microsoft::UI::Xaml::Controls::Button addChainButton_;
    winrt::Microsoft::UI::Xaml::Controls::Border compareSection_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock compareCountText_;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel compareListPanel_;
    winrt::Microsoft::UI::Xaml::Controls::Button addCompareButton_;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel engineCardsPanel_;
    winrt::Microsoft::UI::Xaml::Controls::TextBlock statusText_;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel rootStack_;  // 页面卡片宿主

    EnginesPageViewModel vm_;
    bool updating_ = false;   // 回填触发的选择/开关事件不落盘
    std::string vlmApiKeyDraft_;    // 非空 → 保存时写 CredMan
    std::string paddleApiKeyDraft_; // 同上
    tf::CredManKeyStore credentials_;
};

}  // namespace tf::ui
