// windows/UI/PromptsPage/PromptsPage.h
// FR-35 Prompt 页签 code-behind —— 纯代码构建（原 PromptsPage.h/.cpp 依赖
// XAML 生成头，重构为代码构建路线；PromptsPage.xaml 保留为规格参考）。
//
// 内容（spec §3）：引擎 Prompt 四行（openai/claude/gemini/ollama）+
// 兼容引擎 Prompt 动态行 + 场景 Prompt 三行（截图翻译/划词翻译/翻译插入）
// + 默认 Prompt 预览（defaultPrompt / defaultInsertPrompt 只读）。
// 编辑经 PromptEditorSheet（ContentDialog：编辑器 + 模板变量按钮）。
// 持久化：VM changed 回调 → mergeIntoEngineConfigJson + 原子写 engines.json。
#pragma once

#include "PromptsPageViewModel.h"

#include <winrt/Microsoft.UI.Xaml.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Windows.Foundation.h>

#include <string>

namespace tf::ui {

class PromptsPage : public winrt::Microsoft::UI::Xaml::Controls::UserControlT<PromptsPage> {
public:
    PromptsPage();

    /// shell 页签激活时调用：从磁盘重载（引擎页签可能增删兼容实例）。
    void OnShown();

private:
    void BuildUi();
    void LoadFromDisk();
    void RebuildAllRows();

    /// 三类编辑入口；target = 引擎 / 兼容实例 UUID / 场景。编辑文本经
    /// editingText_ 成员中转（对话框按值取初始文本，避免悬挂引用）。
    void EditEnginePrompt(tf::TranslationEngineType engine);
    void EditCompatiblePrompt(const std::string& instanceId);
    void EditScenePrompt(tf::TranslationScene scene);
    winrt::Windows::Foundation::IAsyncOperation<
        winrt::Microsoft::UI::Xaml::Controls::ContentDialogResult>
    OpenEditorAsync(const std::wstring& title, std::string initialText);
    static void InsertTokenAtCursor(winrt::Microsoft::UI::Xaml::Controls::TextBox const& editor,
                                    std::wstring_view token);

    winrt::Microsoft::UI::Xaml::Controls::StackPanel engineRowsHost_;
    winrt::Microsoft::UI::Xaml::Controls::Border compatibleSection_;  // 整卡显隐（实例为空时折叠）
    winrt::Microsoft::UI::Xaml::Controls::StackPanel compatibleRowsHost_;
    winrt::Microsoft::UI::Xaml::Controls::StackPanel sceneRowsHost_;

    PromptsPageViewModel vm_;
    std::string editingText_;  // 编辑器工作缓冲（Sheet 取消→回滚语义）
    bool dialogOpen_ = false;
};

}  // namespace tf::ui
