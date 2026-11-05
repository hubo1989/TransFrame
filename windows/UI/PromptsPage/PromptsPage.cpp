// PromptsPage.cpp — 「Prompt」页签实现（settings-spec-mac-parity.md §3）。
// 仅 C++/WinRT 头投影，无 .idl / C++/CX。

#include "PromptsPage.h"

#include <algorithm>

namespace winrt::TransFrame::UI::implementation
{
namespace
{
namespace mux = winrt::Microsoft::UI::Xaml;
namespace muxc = winrt::Microsoft::UI::Xaml::Controls;
}  // namespace

PromptsPage::PromptsPage()
{
    InitializeComponent();
    // 静态行（引擎 / 场景）由 x:Bind 一次性求值；兼容小节为空时整体隐藏。
    RebuildCompatibleRows();
    // [集成点] shell 侧接线时机：页面构造后由宿主调用
    //   ViewModel().setChangedHandler(...)（落盘回调）与
    //   ViewModel().setCompatibleInstances(...) + RefreshFromViewModel()
    //   （注入引擎页签维护的 compatibleProviderConfigs）。
}

// ---------------------------------------------------------------------------
// x:Bind 绑定目标
// ---------------------------------------------------------------------------

winrt::hstring PromptsPage::EngineDisplayName(int index) const
{
    auto const& engines = tf::ui::PromptsPageViewModel::promptableEngines();
    if (index < 0 || index >= static_cast<int>(engines.size()))
        return L"";
    // 显示名复用 Core 的 translationEngineDisplayName，不在 UI 层另定义。
    return winrt::to_hstring(tf::translationEngineDisplayName(engines[static_cast<size_t>(index)]));
}

bool PromptsPage::EngineHasCustomPrompt(int index) const
{
    auto const& engines = tf::ui::PromptsPageViewModel::promptableEngines();
    if (index < 0 || index >= static_cast<int>(engines.size()))
        return false;
    return m_viewModel.hasEnginePrompt(engines[static_cast<size_t>(index)]);
}

winrt::hstring PromptsPage::SceneDisplayName(int index) const
{
    auto const& scenes = tf::ui::PromptsPageViewModel::sceneOrder();
    if (index < 0 || index >= static_cast<int>(scenes.size()))
        return L"";
    return SceneDisplayNameZh(scenes[static_cast<size_t>(index)]);
}

bool PromptsPage::SceneHasCustomPrompt(int index) const
{
    auto const& scenes = tf::ui::PromptsPageViewModel::sceneOrder();
    if (index < 0 || index >= static_cast<int>(scenes.size()))
        return false;
    return m_viewModel.hasScenePrompt(scenes[static_cast<size_t>(index)]);
}

bool PromptsPage::CompatibleSectionVisible() const
{
    return !m_viewModel.compatibleInstances().empty();
}

winrt::hstring PromptsPage::DefaultPromptText() const
{
    return winrt::to_hstring(m_viewModel.defaultPromptText());
}

winrt::hstring PromptsPage::DefaultInsertPromptText() const
{
    return winrt::to_hstring(m_viewModel.defaultInsertPromptText());
}

winrt::hstring PromptsPage::SceneDisplayNameZh(tf::TranslationScene scene)
{
    // spec §3.3 的场景中文名；raw 值由 Core translationSceneRaw 管理。
    switch (scene)
    {
    case tf::TranslationScene::Screenshot: return L"截图翻译";
    case tf::TranslationScene::TextSelection: return L"划词翻译";
    case tf::TranslationScene::TranslateAndInsert: return L"翻译插入";
    }
    return L"";
}

// ---------------------------------------------------------------------------
// 编辑入口（引擎 / 兼容实例 / 场景 三类，spec §3.5）
// ---------------------------------------------------------------------------

void PromptsPage::OnEditEnginePrompt(winrt::Windows::Foundation::IInspectable const& sender,
                                     mux::RoutedEventArgs const&)
{
    if (m_dialogOpen)
        return;
    auto button = sender.as<muxc::Button>();
    auto tag = button.Tag().try_as<winrt::hstring>();
    if (!tag)
        return;
    // Tag 存引擎 rawValue（"openai"...），经 Core 解析，不在 UI 层硬编码枚举映射。
    auto engine = tf::translationEngineFromRaw(winrt::to_string(*tag));
    if (!engine)
        return;
    m_editingKind = PromptTargetKind::Engine;
    m_editingEngine = *engine;
    OpenEditor(winrt::to_hstring(tf::translationEngineDisplayName(*engine)) + L" Prompt");
}

void PromptsPage::OnEditScenePrompt(winrt::Windows::Foundation::IInspectable const& sender,
                                    mux::RoutedEventArgs const&)
{
    if (m_dialogOpen)
        return;
    auto button = sender.as<muxc::Button>();
    auto tag = button.Tag().try_as<winrt::hstring>();
    if (!tag)
        return;
    auto scene = tf::translationSceneFromRaw(winrt::to_string(*tag));
    if (!scene)
        return;
    m_editingKind = PromptTargetKind::Scene;
    m_editingScene = *scene;
    OpenEditor(SceneDisplayNameZh(*scene) + L" Prompt");
}

void PromptsPage::OnEditCompatiblePrompt(winrt::Windows::Foundation::IInspectable const& sender,
                                         mux::RoutedEventArgs const&)
{
    if (m_dialogOpen)
        return;
    auto button = sender.as<muxc::Button>();
    auto tag = button.Tag().try_as<winrt::hstring>();
    if (!tag)
        return;
    std::string instanceId = winrt::to_string(*tag);
    auto const* instance = m_viewModel.findCompatibleInstance(instanceId);
    if (!instance)
        return;  // 实例已被引擎页签删除：不打开编辑器
    m_editingKind = PromptTargetKind::Compatible;
    m_editingCompatibleId = instanceId;
    OpenEditor(winrt::to_hstring(instance->displayName + " Prompt"));
}

winrt::fire_and_forget PromptsPage::OpenEditor(winrt::hstring title)
{
    // 成员协程持强引用，防止 co_await 期间页面析构。
    auto lifetime = get_strong();
    if (m_dialogOpen)
        co_return;
    m_dialogOpen = true;

    // 初始文本：已设置 → 该层自定义内容；未设置 → 以默认模板为编辑起点
    // （翻译插入场景用 defaultInsertPrompt，其余用 defaultPrompt，均出自 Core）。
    std::string initial;
    switch (m_editingKind)
    {
    case PromptTargetKind::Engine:
        initial = m_viewModel.hasEnginePrompt(m_editingEngine)
                      ? m_viewModel.enginePrompt(m_editingEngine)
                      : m_viewModel.defaultPromptText();
        break;
    case PromptTargetKind::Compatible:
        initial = m_viewModel.hasCompatiblePrompt(m_editingCompatibleId)
                      ? m_viewModel.compatiblePrompt(m_editingCompatibleId)
                      : m_viewModel.defaultPromptText();
        break;
    case PromptTargetKind::Scene:
        if (m_viewModel.hasScenePrompt(m_editingScene))
            initial = m_viewModel.scenePrompt(m_editingScene);
        else if (m_editingScene == tf::TranslationScene::TranslateAndInsert)
            initial = m_viewModel.defaultInsertPromptText();
        else
            initial = m_viewModel.defaultPromptText();
        break;
    }

    auto editor = PromptEditorTextBox();
    // 光标按 UTF-16 code unit 计：先转 hstring 再取长度（std::string 的
    // UTF-8 字节数在含中文模板时会越界，依赖 TextBox 内部钳制不可靠）。
    winrt::hstring const initialText = winrt::to_hstring(initial);
    editor.Text(initialText);
    editor.SelectionStart(static_cast<int32_t>(initialText.size()));
    editor.SelectionLength(0);

    auto dialog = EditPromptDialog();
    // 标题改为代码赋值：对话框复用、每次目标不同，OneTime x:Bind 无法跟随
    // （等价 WinRT 属性系统双向场景 → 事件驱动 + code-behind 同步，任务约定）。
    dialog.Title(winrt::box_value(title));
    // WinUI 3 桌面端 ShowAsync 前必须赋 XamlRoot；页面挂在窗口树上后取自身 XamlRoot。
    dialog.XamlRoot(this->XamlRoot());
    co_await dialog.ShowAsync();
    m_dialogOpen = false;
}

void PromptsPage::OnEditDialogSave(muxc::ContentDialog const&,
                                   muxc::ContentDialogButtonClickEventArgs const&)
{
    SaveEditingPrompt();
}

void PromptsPage::SaveEditingPrompt()
{
    std::string text = winrt::to_string(PromptEditorTextBox().Text());
    // TextBox 换行为 \r\n；持久化与 Core substitute() 输入统一为 \n。
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());

    // 保存目标按来源写入三层 overlay（spec §3.5）。
    // [待确认] 空文本保存语义（spec 未写明）；当前保守选择：空 = 清除该层自定义。
    switch (m_editingKind)
    {
    case PromptTargetKind::Engine:
        m_viewModel.setEnginePrompt(m_editingEngine, text);
        break;
    case PromptTargetKind::Compatible:
        m_viewModel.setCompatiblePrompt(m_editingCompatibleId, text);
        break;
    case PromptTargetKind::Scene:
        m_viewModel.setScenePrompt(m_editingScene, text);
        break;
    }

    // VM 无 INPC：手动刷新 OneWay 绑定（铅笔标记 / 兼容小节可见性）。
    // 兼容行的铅笔标记是 RebuildCompatibleRows 一次性设置的普通属性（非绑定），
    // 保存兼容实例 Prompt 后必须重建行才能立即反映 —— 与 Bindings().Update()
    // 并列调用，否则铅笔标记要等宿主下次 RefreshFromViewModel 才出现。
    RebuildCompatibleRows();
    Bindings().Update();
    // [集成点] VM 已触发 changed 回调；shell 在回调中执行
    // mergeIntoEngineConfigJson + tf::core::writeEngineConfigFileAtomic 落盘。
}

// ---------------------------------------------------------------------------
// 模板变量按钮（spec §3.5：插入光标处）
// ---------------------------------------------------------------------------

void PromptsPage::OnInsertSourceLanguage(winrt::Windows::Foundation::IInspectable const&,
                                         mux::RoutedEventArgs const&)
{
    InsertTokenAtCursor(L"{source_language}");
}

void PromptsPage::OnInsertTargetLanguage(winrt::Windows::Foundation::IInspectable const&,
                                         mux::RoutedEventArgs const&)
{
    InsertTokenAtCursor(L"{target_language}");
}

void PromptsPage::OnInsertTextToken(winrt::Windows::Foundation::IInspectable const&,
                                    mux::RoutedEventArgs const&)
{
    InsertTokenAtCursor(L"{text}");
}

void PromptsPage::InsertTokenAtCursor(std::wstring_view token)
{
    auto editor = PromptEditorTextBox();
    std::wstring text(editor.Text().c_str());
    int32_t sel = editor.SelectionStart();
    int32_t selLen = editor.SelectionLength();
    if (sel < 0)
        sel = 0;
    // TextBox 换行 \r\n 计两字符，保守钳制到合法区间。
    if (sel > static_cast<int32_t>(text.size()))
        sel = static_cast<int32_t>(text.size());
    if (selLen > 0 && sel + selLen <= static_cast<int32_t>(text.size()))
        text.erase(static_cast<size_t>(sel), static_cast<size_t>(selLen));  // 覆盖当前选区
    text.insert(static_cast<size_t>(sel), token);
    editor.Text(winrt::hstring(text));
    editor.SelectionStart(sel + static_cast<int32_t>(token.size()));
    editor.SelectionLength(0);
    editor.Focus(mux::Input::FocusState::Programmatic);
}

// ---------------------------------------------------------------------------
// 兼容引擎动态行（spec §3.2：按 displayName 列行）
// ---------------------------------------------------------------------------

void PromptsPage::RebuildCompatibleRows()
{
    auto host = CompatibleRowsHost();
    host.Children().Clear();

    auto makeColumn = [](mux::GridUnitType unit) {
        muxc::ColumnDefinition column;
        column.Width(mux::GridLengthHelper::FromValueAndType(1.0, unit));
        return column;
    };

    for (auto const& instance : m_viewModel.compatibleInstances())
    {
        auto row = muxc::Grid();
        row.ColumnDefinitions().Append(makeColumn(mux::GridUnitType::Auto));
        row.ColumnDefinitions().Append(makeColumn(mux::GridUnitType::Star));
        row.ColumnDefinitions().Append(makeColumn(mux::GridUnitType::Auto));
        row.ColumnDefinitions().Append(makeColumn(mux::GridUnitType::Auto));
        row.ColumnSpacing(12.0);
        row.Margin(mux::Thickness{0, 10, 0, 0});

        // E71B Link：OpenAI 兼容端点语义。
        auto icon = muxc::FontIcon();
        icon.FontSize(16.0);
        icon.Glyph(L"\uE71B");
        muxc::Grid::SetColumn(icon, 0);
        row.Children().Append(icon);

        auto name = muxc::TextBlock();
        name.Text(winrt::to_hstring(instance.displayName));
        name.VerticalAlignment(mux::VerticalAlignment::Center);
        muxc::Grid::SetColumn(name, 1);
        row.Children().Append(name);

        // E70F Edit：已设置自定义 Prompt 的铅笔标记（与静态行一致）。
        auto pencil = muxc::FontIcon();
        pencil.FontSize(14.0);
        pencil.Glyph(L"\uE70F");
        pencil.Visibility(m_viewModel.hasCompatiblePrompt(instance.id)
                              ? mux::Visibility::Visible
                              : mux::Visibility::Collapsed);
        muxc::ToolTipService::SetToolTip(pencil, winrt::box_value(L"已设置自定义 Prompt"));
        muxc::Grid::SetColumn(pencil, 2);
        row.Children().Append(pencil);

        auto edit = muxc::Button();
        edit.Content(winrt::box_value(L"编辑"));
        edit.Tag(winrt::box_value(winrt::to_hstring(instance.id)));  // 实例 UUID 字符串
        // 弱引用订阅：行随本页重建/销毁，handler 与静态行走同一入口。
        edit.Click({get_weak(), &PromptsPage::OnEditCompatiblePrompt});
        muxc::Grid::SetColumn(edit, 3);
        row.Children().Append(edit);

        host.Children().Append(row);
    }
}

void PromptsPage::RefreshFromViewModel()
{
    RebuildCompatibleRows();
    Bindings().Update();
}

}  // namespace winrt::TransFrame::UI::implementation
