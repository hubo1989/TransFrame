// windows/UI/EnginesPage/EnginesPage.xaml.cpp
// FR-35 引擎页签 code-behind（实现）—— C++/WinRT 纯头投影。
//
// 页面策略：XAML 只搭骨架（分组卡 + 空容器），链/对比集行、引擎卡、
// 添加菜单、Sheet 全部命令式构建 —— 绕开无 .idl 时 x:Bind 集合绑定对
// XamlTypeInfo 的依赖（UserControl 直接实例化路径，任务第 4 点）。
#include "EnginesPage.xaml.h"

#include <algorithm>
#include <sstream>

namespace tf::ui {
using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
namespace wf = winrt::Windows::Foundation;
namespace wfc = winrt::Windows::Foundation::Collections;

namespace {

Controls::TextBlock MakeText(const std::string& s, bool bold = false, double size = 14) {
    Controls::TextBlock t;
    t.Text(to_hstring(s));
    if (bold) t.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    t.FontSize(size);
    t.TextWrapping(TextWrapping::Wrap);
    return t;
}

Controls::FontIcon MakeIcon(const wchar_t* glyph) {
    Controls::FontIcon i;
    i.Glyph(glyph);
    return i;
}

Controls::Border MakeStatusDot(const std::string& colorHex) {
    Controls::Shapes::Ellipse dot;
    dot.Width(10); dot.Height(10);
    Media::SolidColorBrush b;
    // 解析 "#RRGGBB"
    std::string hex = colorHex.substr(1);
    uint32_t v = std::stoul(hex, nullptr, 16);
    b.Color(Windows::UI::ColorHelper::FromArgb(0xFF,
        static_cast<uint8_t>((v >> 16) & 0xFF),
        static_cast<uint8_t>((v >> 8) & 0xFF),
        static_cast<uint8_t>(v & 0xFF)));
    dot.Fill(b);
    Controls::Border wrap;
    wrap.Child(dot);
    return wrap;
}

std::string HstrToS(hstring const& h) { return winrt::to_string(h); }

/// 从 Tag 拿 raw 字符串（ComboBoxItem.Tag）
std::string TagOf(IInspectable const& item) {
    if (!item) return {};
    auto prop = item.try_as<wf::IPropertyValue>();
    if (prop && prop.Type() == wf::PropertyType::String) return winrt::to_string(prop.GetString());
    return {};
}

}  // namespace

EnginesPage::EnginesPage()
    : vm_(std::make_unique<EnginesPageViewModel>()) {
    // UserControl 基类构造在 InitializeComponent()（生成头）里完成；
    // 未集成生成头时（Core 单测编译本 .cpp 会跳过 UI 路径）由集成方保证。
#if __has_include("EnginesPage.xaml.g.h")
    InitializeComponent();
#endif
    Loaded([this](auto&&, auto&&) { LoadFromViewModel(); });
}

// ---------------------------------------------------------------------------
// 初始拉平：VM → 控件
// ---------------------------------------------------------------------------
void EnginesPage::LoadFromViewModel() {
    // VLM
    for (auto const& item : VlmProviderCombo().Items()) {
        auto comboItem = item.as<ComboBoxItem>();
        if (TagOf(comboItem) == vm_->VlmProvider()) { VlmProviderCombo().SelectedItem(comboItem); break; }
    }
    VlmBaseURLBox().Text(to_hstring(vm_->VlmBaseURL()));
    VlmModelNameBox().Text(to_hstring(vm_->VlmModelName()));
    for (auto const& item : VlmOpenAIModeCombo().Items()) {
        auto comboItem = item.as<ComboBoxItem>();
        if (TagOf(comboItem) == vm_->VlmOpenAIMode()) { VlmOpenAIModeCombo().SelectedItem(comboItem); break; }
    }
    UpdateVlmOpenAIModeVisibility();

    // PaddleOCR
    PaddleStatusText().Text(to_hstring(vm_->PaddleOcrVersionText()));
    for (auto const& item : PaddleModeCombo().Items()) {
        auto comboItem = item.as<ComboBoxItem>();
        if (TagOf(comboItem) == vm_->PaddleOcrMode()) { PaddleModeCombo().SelectedItem(comboItem); break; }
    }
    PaddleUseCloudToggle().IsOn(vm_->PaddleOcrUseCloud());
    PaddleCloudBaseURLBox().Text(to_hstring(vm_->PaddleOcrCloudBaseURL()));
    PaddleCloudModelIdBox().Text(to_hstring(vm_->PaddleOcrCloudModelId()));
    UpdatePaddleCloudVisibility();

    // 多引擎
    for (auto const& item : EngineModeCombo().Items()) {
        auto comboItem = item.as<ComboBoxItem>();
        if (TagOf(comboItem) == vm_->EngineModeV2()) { EngineModeCombo().SelectedItem(comboItem); break; }
    }
    ChainCountText().Text(to_hstring(vm_->ChainCountText()));
    CompareCountText().Text(to_hstring(vm_->CompareCountText()));
    UpdateModeSectionVisibility();
    RebuildChainRows();
    RebuildCompareRows();
    RebuildEngineCards();
    RebuildAddChainMenu();
    RebuildAddCompareMenu();
}

// ---------------------------------------------------------------------------
// VLM
// ---------------------------------------------------------------------------
void EnginesPage::OnVlmProviderChanged(IInspectable const&, Controls::SelectionChangedEventArgs const& e) {
    auto sel = VlmProviderCombo().SelectedItem();
    if (!sel) return;
    vm_->SetVlmProvider(TagOf(sel.as<ComboBoxItem>()));
    UpdateVlmOpenAIModeVisibility();
}

void EnginesPage::OnVlmOpenAIModeChanged(IInspectable const&, Controls::SelectionChangedEventArgs const& e) {
    auto sel = VlmOpenAIModeCombo().SelectedItem();
    if (!sel) return;
    vm_->SetVlmOpenAIMode(TagOf(sel.as<ComboBoxItem>()));
}

void EnginesPage::OnVlmBaseURLCommitted(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetVlmBaseURL(HstrToS(VlmBaseURLBox().Text()));
}

void EnginesPage::OnVlmModelNameCommitted(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetVlmModelName(HstrToS(VlmModelNameBox().Text()));
}

void EnginesPage::OnSaveVlmClick(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetVlmBaseURL(HstrToS(VlmBaseURLBox().Text()));
    vm_->SetVlmModelName(HstrToS(VlmModelNameBox().Text()));
    vm_->SaveVlm();
}

void EnginesPage::UpdateVlmOpenAIModeVisibility() {
    VlmOpenAIModeCombo().Visibility(vm_->VlmShowsOpenAIMode()
        ? Visibility::Visible : Visibility::Collapsed);
}

// ---------------------------------------------------------------------------
// PaddleOCR
// ---------------------------------------------------------------------------
void EnginesPage::OnPaddleModeChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) {
    auto sel = PaddleModeCombo().SelectedItem();
    if (!sel) return;
    vm_->SetPaddleOcrMode(TagOf(sel.as<ComboBoxItem>()));
}

void EnginesPage::OnPaddleUseCloudChanged(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetPaddleOcrUseCloud(PaddleUseCloudToggle().IsOn());
    UpdatePaddleCloudVisibility();
}

void EnginesPage::OnPaddleCloudBaseURLCommitted(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetPaddleOcrCloudBaseURL(HstrToS(PaddleCloudBaseURLBox().Text()));
}

void EnginesPage::OnPaddleCloudAPIKeyChanged(IInspectable const&, RoutedEventArgs const&) {
    paddleApiKeyDraft_ = HstrToS(PaddleCloudAPIKeyBox().Password());
}

void EnginesPage::OnPaddleCloudModelIdCommitted(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetPaddleOcrCloudModelId(HstrToS(PaddleCloudModelIdBox().Text()));
}

void EnginesPage::OnSavePaddleClick(IInspectable const&, RoutedEventArgs const&) {
    vm_->SetPaddleOcrCloudBaseURL(HstrToS(PaddleCloudBaseURLBox().Text()));
    vm_->SetPaddleOcrCloudModelId(HstrToS(PaddleCloudModelIdBox().Text()));
    if (!paddleApiKeyDraft_.empty()) vm_->SetPaddleOcrCloudAPIKey(paddleApiKeyDraft_);
    vm_->SavePaddleOcr();
}

void EnginesPage::UpdatePaddleCloudVisibility() {
    auto v = PaddleUseCloudToggle().IsOn() ? Visibility::Visible : Visibility::Collapsed;
    PaddleCloudBaseURLBox().Visibility(v);
    PaddleCloudAPIKeyBox().Visibility(v);
    PaddleCloudModelIdBox().Visibility(v);
}

// ---------------------------------------------------------------------------
// 多引擎：策略 + 链 + 对比
// ---------------------------------------------------------------------------
void EnginesPage::OnEngineModeChanged(IInspectable const&, Controls::SelectionChangedEventArgs const&) {
    auto sel = EngineModeCombo().SelectedItem();
    if (!sel) return;
    vm_->SetEngineModeV2(TagOf(sel.as<ComboBoxItem>()));
    vm_->SaveEngineModeV2();
    UpdateModeSectionVisibility();
}

void EnginesPage::UpdateModeSectionVisibility() {
    OrderedChainSection().Visibility(vm_->IsOrderedMode() ? Visibility::Visible : Visibility::Collapsed);
    CompareEnginesSection().Visibility(vm_->IsCompareMode() ? Visibility::Visible : Visibility::Collapsed);
}

void EnginesPage::RebuildChainRows() {
    auto panel = ChainListPanel();
    panel.Children().Clear();
    auto rows = vm_->ChainRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(8);

        // 上移 / 下移（拖拽排序的键盘可达等价）
        Controls::Button up; up.Content(box_value(L"↑"));
        up.Click([this, i](auto&&, auto&&) {
            if (i > 0) { vm_->MoveChainRow(i, i - 1); vm_->SaveEngineChainV2(); RebuildChainRows(); }
        });
        Controls::Button down; down.Content(box_value(L"↓"));
        down.Click([this, i, rows](auto&&, auto&&) {
            if (i + 1 < rows.size()) { vm_->MoveChainRow(i, i + 1); vm_->SaveEngineChainV2(); RebuildChainRows(); }
        });

        // 行内替换 Menu（任一已启用内置引擎或兼容实例）
        Controls::MenuFlyout replaceMenu;
        for (auto const& cand : vm_->ChainCandidates()) {
            Controls::MenuFlyoutItem mi;
            mi.Text(to_hstring(cand.displayName));
            mi.Click([this, i, cand](auto&&, auto&&) {
                vm_->ReplaceChainRow(i, cand);
                vm_->SaveEngineChainV2();
                RebuildChainRows(); RebuildEngineCards();
            });
            replaceMenu.Items().Append(mi);
        }
        Controls::DropDownButton replace;
        replace.Text(to_hstring(row.displayName));
        replace.Flyout(replaceMenu);

        // 行内删除
        Controls::Button del; del.Content(MakeIcon(L"\uE74D"));
        del.Click([this, i](auto&&, auto&&) {
            if (vm_->CanRemoveChainRow()) {
                vm_->RemoveChainRow(i);
                vm_->SaveEngineChainV2();
                RebuildChainRows(); RebuildEngineCards();
            }
        });

        line.Children().Append(up);
        line.Children().Append(down);
        line.Children().Append(replace);
        line.Children().Append(del);
        panel.Children().Append(line);
    }
    ChainCountText().Text(to_hstring(vm_->ChainCountText()));
}

void EnginesPage::RebuildCompareRows() {
    auto panel = CompareListPanel();
    panel.Children().Clear();
    auto rows = vm_->CompareRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(8);

        TextBlock name = MakeText(row.displayName);
        Controls::Button del; del.Content(MakeIcon(L"\uE74D"));
        del.Click([this, i](auto&&, auto&&) {
            if (vm_->CanRemoveCompareRow()) {
                vm_->RemoveCompareRow(i);
                vm_->SaveCompareEnginesV2();
                RebuildCompareRows(); RebuildEngineCards();
            }
        });
        line.Children().Append(name);
        line.Children().Append(del);
        panel.Children().Append(line);
    }
    CompareCountText().Text(to_hstring(vm_->CompareCountText()));
}

void EnginesPage::RebuildAddChainMenu() {
    auto menu = AddChainMenu();
    menu.Items().Clear();
    for (auto const& cand : vm_->ChainCandidates()) {
        Controls::MenuFlyoutItem mi;
        mi.Text(to_hstring(cand.displayName));
        mi.Click([this, cand](auto&&, auto&&) {
            vm_->AppendChainRow(cand);
            vm_->SaveEngineChainV2();
            RebuildChainRows(); RebuildEngineCards();
        });
        menu.Items().Append(mi);
    }
    AddChainButton().IsEnabled(vm_->CanAddChainRow());
}

void EnginesPage::RebuildAddCompareMenu() {
    auto menu = AddCompareMenu();
    menu.Items().Clear();
    for (auto const& cand : vm_->CompareCandidates()) {
        Controls::MenuFlyoutItem mi;
        mi.Text(to_hstring(cand.displayName));
        mi.Click([this, cand](auto&&, auto&&) {
            vm_->AppendCompareRow(cand);
            vm_->SaveCompareEnginesV2();
            RebuildCompareRows(); RebuildEngineCards();
        });
        menu.Items().Append(mi);
    }
    AddCompareButton().IsEnabled(vm_->CanAddCompareRow());
}

// ---------------------------------------------------------------------------
// C-4 引擎卡（9 内置 + 兼容实例，按 spec 分组）
// ---------------------------------------------------------------------------
void EnginesPage::RebuildEngineCards() {
    auto panel = EngineCardsPanel();
    panel.Children().Clear();

    // 分组：builtIn(mtran) / LLM(openai, claude, gemini, ollama) /
    // cloudService(google, deepl, baidu) / compatible(动态) / extra(custom)
    struct Group { const wchar_t* title; std::vector<std::string> raws; };
    std::vector<Group> groups = {
        {L"内置引擎", {"mtran"}},
        {L"LLM", {"openai", "claude", "gemini", "ollama"}},
        {L"云服务", {"google", "deepl", "baidu"}},
        {L"其他", {"custom"}},
    };

    auto addCard = [&](const std::string& raw, bool compatible, const std::string& displayName) {
        StackPanel card;
        card.Orientation(Controls::Orientation::Horizontal);
        card.Spacing(8);
        card.Padding({8, 8, 8, 8});
        card.BorderThickness({1, 1, 1, 1});
        card.BorderBrush(Media::SolidColorBrush(Windows::UI::Colors::LightGray()));
        card.CornerRadius({4, 4, 4, 4});

        Controls::ToggleSwitch enableToggle;
        enableToggle.OnContent(L""); enableToggle.OffContent(L"");
        enableToggle.IsOn(vm_->BuiltinEnabled(raw));
        std::string rawCopy = raw;
        enableToggle.Toggled([this, rawCopy, compatible](auto&&, auto&&) {
            // 注意：Toggled 在 Rebuild 期间也会触发 —— 以点击后状态为准提交。
            bool on = false;
            // 读取 sender 状态更稳，但 lambda 里无 sender；直接回读 VM 之外的状态由
            // 调用点保证（下面 card 构建时用 IsOn 的当前值）。此处用捕获时的 VM 翻转：
            on = !vm_->BuiltinEnabled(rawCopy);
            vm_->SetBuiltinEnabled(rawCopy, on);
            vm_->SaveEngineConfigs();
            RebuildAddChainMenu(); RebuildAddCompareMenu();
        });

        Controls::FontIcon icon = MakeIcon(compatible ? L"" : L"");
        TextBlock name = MakeText(displayName, true);
        TextBlock desc = MakeText(compatible ? "OpenAI 兼容端点" : "点击配置", false, 12);
        auto dot = MakeStatusDot(vm_->HealthColorHex(raw));
        if (compatible) {
            // 兼容卡健康 key = 实例 UUID（display 传 raw 字段承载 id）
        }

        // 点击卡片打开 Sheet
        Controls::HyperlinkButton open;
        open.Content(name);
        std::string sheetKey = raw;
        open.Click([this, sheetKey, compatible](auto&&, auto&&) {
            if (compatible) ShowCompatibleSheetAsync(sheetKey);
            else ShowEngineConfigSheetAsync(sheetKey);
        });

        card.Children().Append(enableToggle);
        card.Children().Append(icon);
        card.Children().Append(open);
        card.Children().Append(desc);
        card.Children().Append(dot);
        panel.Children().Append(card);
    };

    for (auto const& g : groups) {
        TextBlock header = MakeText(winrt::to_string(g.title), true, 12);
        header.Margin({0, 8, 0, 0});
        panel.Children().Append(header);
        for (auto const& raw : g.raws) {
            if (auto t = translationEngineFromRaw(raw))
                addCard(raw, false, translationEngineDisplayName(*t));
        }
    }

    // 兼容分组
    TextBlock compHeader = MakeText("兼容引擎", true, 12);
    compHeader.Margin({0, 8, 0, 0});
    panel.Children().Append(compHeader);
    for (auto const& c : vm_->CompatibleInstances()) {
        StackPanel card;
        card.Orientation(Controls::Orientation::Horizontal);
        card.Spacing(8);
        card.Padding({8, 8, 8, 8});
        Controls::FontIcon icon = MakeIcon(L"\uE7F1");  // gearshape.2
        TextBlock name = MakeText(c.displayName, true);
        TextBlock inUse = MakeText(vm_->CompatibleInUse(c.id) ? "使用中" : "", false, 12);
        auto dot = MakeStatusDot(vm_->HealthColorHex(c.id));
        Controls::HyperlinkButton open;
        open.Content(name);
        std::string id = c.id;
        open.Click([this, id](auto&&, auto&&) { ShowCompatibleSheetAsync(id); });
        card.Children().Append(icon);
        card.Children().Append(open);
        card.Children().Append(inUse);
        card.Children().Append(dot);
        panel.Children().Append(card);
    }
    // 添加按钮 / 上限文案
    if (vm_->CanAddCompatibleInstance()) {
        Controls::Button addCompat;
        addCompat.Content(box_value(L"新建兼容引擎"));
        addCompat.Click([this](auto&&, auto&&) { ShowCompatibleSheetAsync({}); });
        panel.Children().Append(addCompat);
    } else {
        panel.Children().Append(MakeText("兼容引擎已达上限（5）", false, 12));
    }
}

void EnginesPage::RefreshHealthDots() {
    // 健康探测（集成方注入 EngineHealthChecker 的异步结果回调后调用）：
    // VM.SetHealthOf(key, state) → RebuildEngineCards()。
    RebuildEngineCards();
}

// ---------------------------------------------------------------------------
// Sheet 1：引擎配置（ContentDialog，spec §2.4，500×450）
// ---------------------------------------------------------------------------
wf::IAsyncOperation<Controls::ContentDialogResult> EnginesPage::ShowEngineConfigSheetAsync(
    const std::string& engineRaw) {
    auto draft = vm_->OpenBuiltinSheetDraft(engineRaw);
    bool requiresKey = false, requiresAppId = false, hasBaseUrl = false, hasModel = false;
    bool isMtran = engineRaw == "mtran", isOpenAI = engineRaw == "openai";
    if (auto t = translationEngineFromRaw(engineRaw)) {
        requiresKey = translationEngineRequiresApiKey(*t);
        requiresAppId = translationEngineRequiresAppId(*t);
        hasBaseUrl = !translationEngineDefaultBaseUrl(*t).empty() || isMtran;
        hasModel = !translationEngineDefaultModelName(*t).empty() || engineRaw == "custom";
    }

    StackPanel content;
    content.Spacing(12);

    // OpenAI 预设 Picker（仅 openai）
    ComboBox presetCombo;
    if (isOpenAI) {
        presetCombo.Header(box_value(L"预设"));
        presetCombo.SelectedValuePath(L"Tag");
        ComboBoxItem i1; i1.Content(box_value(L"OpenAI 官方")); i1.Tag(box_value(L"openAI"));
        ComboBoxItem i2; i2.Content(box_value(L"本地 Ollama")); i2.Tag(box_value(L"ollamaLocal"));
        ComboBoxItem i3; i3.Content(box_value(L"自定义")); i3.Tag(box_value(L"custom"));
        presetCombo.Items().Append(i1); presetCombo.Items().Append(i2); presetCombo.Items().Append(i3);
        presetCombo.SelectedIndex(0);
        content.Children().Append(presetCombo);
    }

    // 启用开关
    ToggleSwitch enableSwitch;
    enableSwitch.Header(box_value(L"启用"));
    enableSwitch.IsOn(draft.isEnabled);
    content.Children().Append(enableSwitch);

    // MTranServer URL（占位 http://localhost:8989）
    TextBox baseUrlBox;
    if (hasBaseUrl) {
        baseUrlBox.Header(box_value(L"Base URL"));
        baseUrlBox.PlaceholderText(isMtran ? box_value(L"http://localhost:8989")
                                           : box_value(L""));
        baseUrlBox.Text(to_hstring(draft.baseURL));
        content.Children().Append(baseUrlBox);
    }

    // 模型名
    TextBox modelBox;
    if (hasModel) {
        modelBox.Header(box_value(L"模型名"));
        modelBox.Text(to_hstring(draft.modelName));
        content.Children().Append(modelBox);
    }

    // API Key（SecureField + 显隐；集成方落 CredMan —— 集成点见下注释）
    PasswordBox keyBox;
    if (requiresKey) {
        keyBox.Header(box_value(L"API Key"));
        keyBox.PasswordRevealMode(Controls::PasswordRevealMode::Peek);
        content.Children().Append(keyBox);
    }
    // Baidu: AppID + SecretKey（集成点：CredMan 账户名见
    // windows/Core/include/tf/CredentialAccounts（credentialAccount(engine)））
    PasswordBox appIdBox, secretBox;
    if (requiresAppId) {
        appIdBox.Header(box_value(L"App ID"));
        secretBox.Header(box_value(L"Secret Key"));
        content.Children().Append(appIdBox);
        content.Children().Append(secretBox);
    }

    // 测试连接 + 结果
    TextBlock testResult;
    ProgressRing testRing;
    Controls::Button testBtn;
    testBtn.Content(box_value(L"测试连接"));
    testBtn.Click([&testRing, &testResult](auto&&, auto&&) {
        // 集成点：经 EngineHealthChecker 同步/异步探测；此处仅 UI 反馈骨架。
        testRing.IsActive(true);
        testResult.Text(L"测试中…");
    });
    content.Children().Append(testBtn);
    content.Children().Append(testRing);
    content.Children().Append(testResult);

    Controls::ContentDialog dialog;
    dialog.Title(box_value(to_hstring(std::string("配置 ") + engineRaw)));
    dialog.Content(content);
    dialog.PrimaryButtonText(L"保存");
    dialog.CloseButtonText(L"取消");
    dialog.DefaultButton(Controls::ContentDialogButton::Primary);
    // 保存按钮在 !isEnabled && !hasValidConfig 时禁用 —— code-behind 动态：
    dialog.IsPrimaryButtonEnabled(true);

    auto result = co_await dialog.ShowAsync();

    if (result == Controls::ContentDialogResult::Primary) {
        draft.isEnabled = enableSwitch.IsOn();
        if (hasBaseUrl) draft.baseURL = HstrToS(baseUrlBox.Text());
        if (hasModel) draft.modelName = HstrToS(modelBox.Text());
        // 凭证（API Key / Baidu AppID+Secret）落 CredMan 由集成方实现
        //（NativeShell CredManKeyStore）；此处仅提交非敏感字段。
        vm_->CommitBuiltinSheet(draft);
        vm_->SaveEngineConfigs();
        RebuildEngineCards(); RebuildAddChainMenu(); RebuildAddCompareMenu();
    }
    // 取消 → 丢弃 draft（工作副本语义，无需回滚）
    co_return result;
}

// ---------------------------------------------------------------------------
// Sheet 2：兼容引擎配置（ContentDialog，spec §2.5）
// ---------------------------------------------------------------------------
wf::IAsyncOperation<Controls::ContentDialogResult> EnginesPage::ShowCompatibleSheetAsync(
    const std::string& instanceId) {
    bool isNew = instanceId.empty();
    auto draft = isNew ? vm_->NewCompatibleSheetDraft() : vm_->OpenCompatibleSheetDraft(instanceId);

    TextBox nameBox; nameBox.Header(box_value(L"显示名称")); nameBox.Text(to_hstring(draft.displayName));
    TextBox baseUrlBox; baseUrlBox.Header(box_value(L"Base URL"));
    baseUrlBox.PlaceholderText(box_value(L"http://localhost:8000/v1"));
    baseUrlBox.Text(to_hstring(draft.baseURL));
    TextBox modelBox; modelBox.Header(box_value(L"模型名"));
    modelBox.PlaceholderText(box_value(L"gpt-4o-mini"));
    modelBox.Text(to_hstring(draft.modelName));
    PasswordBox keyBox; keyBox.Header(box_value(L"API Key"));
    keyBox.PasswordRevealMode(Controls::PasswordRevealMode::Peek);

    StackPanel content; content.Spacing(12);
    content.Children().Append(nameBox);
    content.Children().Append(baseUrlBox);
    content.Children().Append(modelBox);
    content.Children().Append(keyBox);

    Controls::ContentDialog dialog;
    dialog.Title(box_value(isNew ? L"新建兼容引擎" : L"编辑兼容引擎"));
    dialog.Content(content);
    dialog.PrimaryButtonText(L"保存");
    dialog.CloseButtonText(L"取消");

    auto result = co_await dialog.ShowAsync();

    if (result == Controls::ContentDialogResult::Primary) {
        draft.displayName = HstrToS(nameBox.Text());
        draft.baseURL = HstrToS(baseUrlBox.Text());
        draft.modelName = HstrToS(modelBox.Text());
        if (isNew) {
            // UUID 由 Core Uuid::random() 生成（CoreModels.hpp）；集成方在
            // App 侧可替换为 winrt GUID。此处用 Core 实现。
            draft.id = tf::Uuid::random().toString();
        }
        draft.hasAPIKey = !HstrToS(keyBox.Password()).empty();
        // Key 本体落 CredMan：credentialAccountForCompatible(draft.id)
        vm_->CommitCompatibleSheet(draft);
        vm_->SaveEngineConfigs();
        RebuildEngineCards();
    }
    co_return result;
}

}  // namespace tf::ui
