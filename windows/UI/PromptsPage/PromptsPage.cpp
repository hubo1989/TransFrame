// windows/UI/PromptsPage/PromptsPage.cpp
// FR-35 Prompt 页签 code-behind 实现（三层 Prompt + 默认预览 + 编辑 Sheet）。
#include "PromptsPage.h"
#include "EnginesPageViewModel.hpp"  // ParseCompatibleInstances（同一 schema 段）
#include "SettingsFileStore.hpp"
#include "SettingsUiKit.hpp"

#include <windows.h>  // OutputDebugStringW（保存失败诊断）

namespace tf::ui {
namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

std::wstring SceneDisplayNameZh(tf::TranslationScene scene) {
    namespace loc = tf::core::loc;
    switch (scene) {
        case tf::TranslationScene::Screenshot:
            return loc::tw("win.prompts.scene.screenshot", L"截图翻译");
        case tf::TranslationScene::TextSelection:
            return loc::tw("win.prompts.scene.textSelection", L"划词翻译");
        case tf::TranslationScene::TranslateAndInsert:
            return loc::tw("win.prompts.scene.translateInsert", L"翻译插入");
    }
    return L"";
}

}  // namespace

PromptsPage::PromptsPage() {
    // Prompt 任一层变更 → 合并进 engines.json（保留其余键）+ 原子写。
    vm_.setChangedHandler([this] {
        const std::string json = PromptsPageViewModel::mergeIntoEngineConfigJson(
            store::ReadEnginesJson(), vm_.config());
        if (!store::WriteEnginesJsonAtomic(json)) {
            ::OutputDebugStringW(L"TransFrame: promptConfig 保存失败\n");
        }
    });
    BuildUi();
    LoadFromDisk();
}

// ---------------------------------------------------------------------------
// UI 骨架
// ---------------------------------------------------------------------------
void PromptsPage::BuildUi() {
    Controls::StackPanel root = kit::MakePageRoot(*this);
    root.Children().Append(kit::MakeSectionHeader(
        kit::Lx("settings.section.prompts", L"Prompt").c_str()));

    // 卡片 1：引擎 Prompt（spec §3.1）
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("prompt.engine.title", L"引擎 Prompt").c_str(),
            kit::Lx("win.prompts.engine.hint", L"为单个引擎设置自定义 Prompt（覆盖内置默认）").c_str()));
        engineRowsHost_ = Controls::StackPanel();
        engineRowsHost_.Spacing(8.0);
        card.panel.Children().Append(engineRowsHost_);
        root.Children().Append(card.border);
    }

    // 卡片 2：兼容引擎 Prompt（spec §3.2，实例非空才显示）
    {
        kit::Card card = kit::MakeCard();
        compatibleSection_ = card.border;
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("prompt.compatible.title", L"兼容引擎 Prompt").c_str(),
            kit::Lx("win.prompts.compatible.hint", L"按 OpenAI 兼容实例单独设置").c_str()));
        compatibleRowsHost_ = Controls::StackPanel();
        compatibleRowsHost_.Spacing(8.0);
        card.panel.Children().Append(compatibleRowsHost_);
        root.Children().Append(compatibleSection_);
    }

    // 卡片 3：场景 Prompt（spec §3.3）
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("prompt.scene.title", L"场景 Prompt").c_str(),
            kit::Lx("win.prompts.scene.hint", L"按使用场景设置（优先级：场景 > 引擎 > 默认）").c_str()));
        sceneRowsHost_ = Controls::StackPanel();
        sceneRowsHost_.Spacing(8.0);
        card.panel.Children().Append(sceneRowsHost_);
        root.Children().Append(card.border);
    }

    // 卡片 4：默认 Prompt 预览（spec §3.4，只读）
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("win.prompts.default.title", L"默认 Prompt 预览").c_str()));

        Controls::TextBox def = Controls::TextBox();
        def.Header(winrt::box_value(kit::Lx("win.prompts.fallback", L"通用默认")));
        def.Text(winrt::to_hstring(PromptsPageViewModel::defaultPromptText()));
        def.IsReadOnly(true);
        def.TextWrapping(TextWrapping::Wrap);
        def.AcceptsReturn(true);
        def.MaxHeight(140.0);
        card.panel.Children().Append(def);

        Controls::TextBox defInsert = Controls::TextBox();
        defInsert.Header(winrt::box_value(
            kit::Lx("win.prompts.insertDefault", L"翻译插入默认")));
        defInsert.Text(winrt::to_hstring(PromptsPageViewModel::defaultInsertPromptText()));
        defInsert.IsReadOnly(true);
        defInsert.TextWrapping(TextWrapping::Wrap);
        defInsert.AcceptsReturn(true);
        defInsert.MaxHeight(140.0);
        card.panel.Children().Append(defInsert);

        root.Children().Append(card.border);
    }
}

void PromptsPage::LoadFromDisk() {
    const std::string json = store::ReadEnginesJson();
    vm_.loadConfig(PromptsPageViewModel::parsePromptConfig(json).config);
    // 兼容实例与引擎页签共用 engines.json 的 compatibleProviderConfigs 段。
    std::vector<tf::CompatibleConfig> instances;
    for (const auto& row : EnginesPageViewModel::ParseCompatibleInstances(json)) {
        tf::CompatibleConfig c;
        c.id = row.id;
        c.displayName = row.displayName;
        c.baseURL = row.baseURL;
        c.modelName = row.modelName;
        c.hasAPIKey = row.hasAPIKey;
        instances.push_back(std::move(c));
    }
    vm_.setCompatibleInstances(std::move(instances));
    RebuildAllRows();
}

void PromptsPage::OnShown() { LoadFromDisk(); }

// ---------------------------------------------------------------------------
// 行构建
// ---------------------------------------------------------------------------
void PromptsPage::RebuildAllRows() {
    // 引擎行
    engineRowsHost_.Children().Clear();
    for (auto engine : PromptsPageViewModel::promptableEngines()) {
        Controls::StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(10.0);

        Controls::TextBlock name = kit::MakeText(
            kit::ToWide(tf::translationEngineDisplayName(engine)));
        name.VerticalAlignment(VerticalAlignment::Center);
        name.MinWidth(120.0);

        Controls::FontIcon pencil;
        pencil.Glyph(L"");
        pencil.FontSize(14.0);
        pencil.VerticalAlignment(VerticalAlignment::Center);
        pencil.Visibility(vm_.hasEnginePrompt(engine) ? Visibility::Visible
                                                      : Visibility::Collapsed);
        Controls::ToolTipService::SetToolTip(
                pencil, winrt::box_value(kit::Lx("win.prompts.customSet", L"已设置自定义 Prompt")));

        Controls::Button edit;
        edit.Content(winrt::box_value(kit::Lx("prompt.button.edit", L"编辑")));
        edit.Click([this, engine](auto&&, auto&&) { EditEnginePrompt(engine); });

        line.Children().Append(name);
        line.Children().Append(pencil);
        line.Children().Append(edit);
        engineRowsHost_.Children().Append(line);
    }

    // 兼容实例行（无实例 → 整卡隐藏，spec §3.2）
    compatibleRowsHost_.Children().Clear();
    compatibleSection_.Visibility(vm_.compatibleInstances().empty()
                                      ? Visibility::Collapsed
                                      : Visibility::Visible);
    for (const auto& inst : vm_.compatibleInstances()) {
        Controls::StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(10.0);

        Controls::TextBlock name = kit::MakeText(kit::ToWide(inst.displayName));
        name.VerticalAlignment(VerticalAlignment::Center);
        name.MinWidth(120.0);

        Controls::FontIcon pencil;
        pencil.Glyph(L"");
        pencil.FontSize(14.0);
        pencil.VerticalAlignment(VerticalAlignment::Center);
        pencil.Visibility(vm_.hasCompatiblePrompt(inst.id) ? Visibility::Visible
                                                           : Visibility::Collapsed);

        Controls::Button edit;
        edit.Content(winrt::box_value(kit::Lx("prompt.button.edit", L"编辑")));
        std::string id = inst.id;
        edit.Click([this, id](auto&&, auto&&) { EditCompatiblePrompt(id); });

        line.Children().Append(name);
        line.Children().Append(pencil);
        line.Children().Append(edit);
        compatibleRowsHost_.Children().Append(line);
    }

    // 场景行
    sceneRowsHost_.Children().Clear();
    for (auto scene : PromptsPageViewModel::sceneOrder()) {
        Controls::StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(10.0);

        Controls::TextBlock name =
            kit::MakeText(SceneDisplayNameZh(scene).c_str());
        name.VerticalAlignment(VerticalAlignment::Center);
        name.MinWidth(120.0);

        Controls::FontIcon pencil;
        pencil.Glyph(L"");
        pencil.FontSize(14.0);
        pencil.VerticalAlignment(VerticalAlignment::Center);
        pencil.Visibility(vm_.hasScenePrompt(scene) ? Visibility::Visible
                                                    : Visibility::Collapsed);

        Controls::Button edit;
        edit.Content(winrt::box_value(kit::Lx("prompt.button.edit", L"编辑")));
        edit.Click([this, scene](auto&&, auto&&) { EditScenePrompt(scene); });

        line.Children().Append(name);
        line.Children().Append(pencil);
        line.Children().Append(edit);
        sceneRowsHost_.Children().Append(line);
    }
}

// ---------------------------------------------------------------------------
// 编辑入口（spec §3.5：已设置 → 该层自定义；未设置 → 以默认模板为起点）。
// 编辑文本经 editingText_ 成员中转（Sheet 取消 → 丢弃，保存 → 提交 VM）。
// ---------------------------------------------------------------------------
void PromptsPage::EditEnginePrompt(tf::TranslationEngineType engine) {
    if (dialogOpen_) return;
    const std::string initial = vm_.hasEnginePrompt(engine) ? vm_.enginePrompt(engine)
                                                            : vm_.defaultPromptText();
    OpenEditorAsync(kit::ToWide(tf::translationEngineDisplayName(engine)) + L" Prompt",
                    initial)
        .Completed([this, engine](auto const& op, auto const&) {
            if (op.GetResults() == Controls::ContentDialogResult::Primary) {
                vm_.setEnginePrompt(engine, editingText_);
                RebuildAllRows();
            }
        });
}

void PromptsPage::EditCompatiblePrompt(const std::string& instanceId) {
    if (dialogOpen_) return;
    const auto* inst = vm_.findCompatibleInstance(instanceId);
    if (!inst) return;  // 实例已被引擎页签删除
    const std::string initial = vm_.hasCompatiblePrompt(instanceId)
                                    ? vm_.compatiblePrompt(instanceId)
                                    : vm_.defaultPromptText();
    OpenEditorAsync(kit::ToWide(inst->displayName) + L" Prompt", initial)
        .Completed([this, instanceId](auto const& op, auto const&) {
            if (op.GetResults() == Controls::ContentDialogResult::Primary) {
                vm_.setCompatiblePrompt(instanceId, editingText_);
                RebuildAllRows();
            }
        });
}

void PromptsPage::EditScenePrompt(tf::TranslationScene scene) {
    if (dialogOpen_) return;
    std::string initial;
    if (vm_.hasScenePrompt(scene)) {
        initial = vm_.scenePrompt(scene);
    } else if (scene == tf::TranslationScene::TranslateAndInsert) {
        initial = vm_.defaultInsertPromptText();
    } else {
        initial = vm_.defaultPromptText();
    }
    OpenEditorAsync(SceneDisplayNameZh(scene) + L" Prompt", initial)
        .Completed([this, scene](auto const& op, auto const&) {
            if (op.GetResults() == Controls::ContentDialogResult::Primary) {
                vm_.setScenePrompt(scene, editingText_);
                RebuildAllRows();
            }
        });
}

winrt::Windows::Foundation::IAsyncOperation<Controls::ContentDialogResult>
PromptsPage::OpenEditorAsync(const std::wstring& title, std::string initialText) {
    dialogOpen_ = true;
    editingText_ = std::move(initialText);

    Controls::TextBox editor;
    editor.AcceptsReturn(true);
    editor.TextWrapping(TextWrapping::Wrap);
    editor.MinHeight(220.0);
    editor.MinWidth(420.0);
    editor.MaxHeight(360.0);
    winrt::hstring initial = winrt::to_hstring(editingText_);
    editor.Text(initial);
    editor.SelectionStart(static_cast<std::int32_t>(initial.size()));
    editor.SelectionLength(0);

    // 模板变量按钮（spec §3.5：插入光标处）
    Controls::StackPanel tokens;
    tokens.Orientation(Controls::Orientation::Horizontal);
    tokens.Spacing(8.0);
    for (const std::wstring token :
         {L"{source_language}", L"{target_language}", L"{text}"}) {
        Controls::Button b;
        b.Content(winrt::box_value(core::loc::formatArgsWide(
            kit::Lx("win.prompts.insertVariable", L"插入 %@"), {token})));
        b.Click([editor, token](auto&&, auto&&) { InsertTokenAtCursor(editor, token); });
        tokens.Children().Append(b);
    }

    Controls::StackPanel content;
    content.Spacing(12.0);
    content.Children().Append(tokens);
    content.Children().Append(editor);

    Controls::ContentDialog dialog;
    dialog.Title(winrt::box_value(title));
    dialog.Content(content);
    dialog.PrimaryButtonText(kit::Lx("action.save", L"保存"));
    dialog.CloseButtonText(kit::Lx("action.cancel", L"取消"));
    dialog.DefaultButton(Controls::ContentDialogButton::Primary);
    dialog.XamlRoot(XamlRoot());

    const Controls::ContentDialogResult result = co_await dialog.ShowAsync();
    dialogOpen_ = false;
    if (result == Controls::ContentDialogResult::Primary) {
        // TextBox 换行为 \r\n；持久化与 Core substitute() 输入统一为 \n。
        std::string text = winrt::to_string(editor.Text());
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
        editingText_ = text;
    } else {
        editingText_.clear();  // 取消 → 丢弃工作副本
    }
    co_return result;
}

void PromptsPage::InsertTokenAtCursor(Controls::TextBox const& editor, std::wstring_view token) {
    std::wstring text(editor.Text().c_str());
    std::int32_t sel = editor.SelectionStart();
    std::int32_t selLen = editor.SelectionLength();
    if (sel < 0) sel = 0;
    if (sel > static_cast<std::int32_t>(text.size())) sel = static_cast<std::int32_t>(text.size());
    if (selLen > 0 && sel + selLen <= static_cast<std::int32_t>(text.size())) {
        text.erase(static_cast<std::size_t>(sel), static_cast<std::size_t>(selLen));
    }
    text.insert(static_cast<std::size_t>(sel), token);
    editor.Text(winrt::hstring(text));
    editor.SelectionStart(sel + static_cast<std::int32_t>(token.size()));
    editor.SelectionLength(0);
    editor.Focus(FocusState::Programmatic);
}

}  // namespace tf::ui
