#pragma once
// PromptsPage.h — 「Prompt」页签 code-behind（settings-spec-mac-parity.md §3）。
// C++/WinRT 纯头投影 + XAML 编译器生成基类；不写 .idl、不用 C++/CX。
//
// [集成点] vcxproj 需把本目录加入工程：PromptsPage.xaml 以 <Page>（XamlCompiler）
//          条目引入（生成 PromptsPage.g.h / PromptsPage.g.cpp），PromptsPage.cpp
//          一同编译；x:Class="TransFrame.UI.PromptsPage" 对应命名空间
//          winrt::TransFrame::UI::implementation。不修改任何现有工程文件，
//          由 shell 侧接线时统一登记。
//
// x:Bind 约定：UserControl 根的 x:Bind 解析到本类公有成员；XAML 中每个绑定
// 路径（EngineDisplayName / EngineHasCustomPrompt / SceneDisplayName /
// SceneHasCustomPrompt / CompatibleSectionVisible / DefaultPromptText /
// DefaultInsertPromptText）与下方声明一字不差。VM 为普通 C++ 类（无 INPC），
// 动态绑定使用 Mode="OneWay" + code-behind 调 Bindings().Update() 手动刷新。

#include "PromptsPage.g.h"  // XAML 编译器生成（macOS 上无 XAML 工具链，见 winui3-migration.md）
#include "PromptsPageViewModel.h"

// [集成点] 工程包含路径已含 windows/Core/include 与 windows/Core/vendor，
//          经 PromptsPageViewModel.h 间接使用 tf/CoreModels.hpp / tf/Json.hpp。
#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Windows.Foundation.h>

namespace winrt::TransFrame::UI::implementation
{
/// Prompt 编辑目标三类（spec §3.5：保存目标按来源写入
/// enginePrompts / compatibleEnginePrompts / scenePrompts）。
enum class PromptTargetKind { Engine, Compatible, Scene };

struct PromptsPage : PromptsPageT<PromptsPage>
{
    PromptsPage();

    // ---- x:Bind 绑定目标（公有；与 XAML 路径一字不差）----
    /// index 0..3 对应 ViewModel::promptableEngines()：openai/claude/gemini/ollama。
    winrt::hstring EngineDisplayName(int index) const;
    bool EngineHasCustomPrompt(int index) const;
    /// index 0..2 对应 ViewModel::sceneOrder()：
    /// screenshot / text_selection / translate_and_insert。
    winrt::hstring SceneDisplayName(int index) const;
    bool SceneHasCustomPrompt(int index) const;
    /// 兼容引擎小节可见性（spec §3.2：仅当兼容实例列表非空时显示）。
    bool CompatibleSectionVisible() const;
    /// 默认模板预览（spec §3.4；VM 直接调 Core，不抄文本）。
    winrt::hstring DefaultPromptText() const;
    winrt::hstring DefaultInsertPromptText() const;

    // ---- 事件处理器（签名符合 C++/WinRT 委托）----
    void OnEditEnginePrompt(winrt::Windows::Foundation::IInspectable const& sender,
                            winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnEditScenePrompt(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    /// 兼容实例行的编辑按钮（行在 code-behind 动态生成时以
    /// { get_weak(), &PromptsPage::OnEditCompatiblePrompt } 订阅）。
    void OnEditCompatiblePrompt(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnInsertSourceLanguage(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnInsertTargetLanguage(winrt::Windows::Foundation::IInspectable const& sender,
                                winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    void OnInsertTextToken(winrt::Windows::Foundation::IInspectable const& sender,
                           winrt::Microsoft::UI::Xaml::RoutedEventArgs const& e);
    /// ContentDialog.PrimaryButtonClick 委托：
    /// TypedEventHandler<ContentDialog, ContentDialogButtonClickEventArgs>。
    void OnEditDialogSave(winrt::Microsoft::UI::Xaml::Controls::ContentDialog const& sender,
                          winrt::Microsoft::UI::Xaml::Controls::ContentDialogButtonClickEventArgs const& e);

    // ---- shell 侧接入 ----
    /// [集成点] shell 层入口：注入兼容引擎实例列表（引擎页签的
    /// compatibleProviderConfigs）后调用，重建动态行并刷新绑定。
    void RefreshFromViewModel();
    /// [集成点] shell 层入口：取出 VM 装载已解析配置 / 注册落盘回调。
    tf::ui::PromptsPageViewModel& ViewModel() { return m_viewModel; }

private:
    tf::ui::PromptsPageViewModel m_viewModel;

    PromptTargetKind m_editingKind = PromptTargetKind::Engine;
    tf::TranslationEngineType m_editingEngine = tf::TranslationEngineType::OpenAI;
    std::string m_editingCompatibleId;  // 兼容实例 UUID 字符串
    tf::TranslationScene m_editingScene = tf::TranslationScene::Screenshot;
    bool m_dialogOpen = false;

    static winrt::hstring SceneDisplayNameZh(tf::TranslationScene scene);
    void RebuildCompatibleRows();
    /// 打开编辑对话框：按当前 m_editing* 取初始文本（已设置 → 自定义内容；
    /// 未设置 → 对应场景默认模板作编辑起点）。
    /// 参数按值传递（C++/WinRT 协程参数约定，避免 co_await 期间悬挂引用）。
    winrt::fire_and_forget OpenEditor(winrt::hstring title);
    void SaveEditingPrompt();
    /// 在 PromptEditorTextBox 光标处插入模板变量（覆盖当前选区）。
    void InsertTokenAtCursor(std::wstring_view token);
};

}  // namespace winrt::TransFrame::UI::implementation
