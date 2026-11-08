// windows/UI/EnginesPage/EnginesPage.cpp
// FR-35 引擎页签 code-behind 实现（纯代码构建；对照 spec §2 逐控件）。
#include "EnginesPage.h"
#include "SettingsFileStore.hpp"
#include "SettingsUiKit.hpp"

#include <nlohmann/json.hpp>

#include <windows.h>  // OutputDebugStringW（保存失败诊断）

#include <algorithm>

namespace tf::ui {
namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

std::string TagOf(winrt::Windows::Foundation::IInspectable const& item) {
    if (!item) return {};
    auto box = item.try_as<winrt::Windows::Foundation::IPropertyValue>();
    if (box && box.Type() == winrt::Windows::Foundation::PropertyType::String) {
        return winrt::to_string(box.GetString());
    }
    return {};
}

void SelectComboByTag(Controls::ComboBox const& combo, const std::string& raw) {
    for (std::uint32_t i = 0; i < combo.Items().Size(); ++i) {
        if (TagOf(combo.Items().GetAt(i)) == raw) {
            combo.SelectedIndex(i);
            return;
        }
    }
    combo.SelectedIndex(combo.Items().Size() > 0 ? 0 : -1);
}

void FillCombo(Controls::ComboBox const& combo,
               std::initializer_list<std::pair<const char*, const wchar_t*>> entries) {
    for (const auto& [raw, label] : entries) {
        Controls::ComboBoxItem item;
        item.Content(winrt::box_value(label));
        item.Tag(winrt::box_value(winrt::to_hstring(raw)));
        combo.Items().Append(item);
    }
}

bool IsLocalEndpoint(std::string_view url) {
    return url.find("localhost") != std::string_view::npos ||
           url.find("127.0.0.1") != std::string_view::npos ||
           url.find("[::1]") != std::string_view::npos ||
           url.find("host.docker.internal") != std::string_view::npos;
}

/// VLM per-provider CredMan 账户（schema §4：macOS Keychain 对应位）。
std::string VlmCredentialAccount(const std::string& provider) {
    return "TransFrame/vlm/" + provider;
}
constexpr const char* kPaddleCloudAccount = "TransFrame/paddleocr/cloud";

/// 四态健康图例色说明。
std::wstring HealthStateLabel(const std::string& state) {
    if (state == "green") return core::loc::tw("engine.health.available", L"可用");
    if (state == "red") return core::loc::tw("win.engines.health.abnormal", L"异常");
    if (state == "orange") return core::loc::tw("win.engines.health.noCredentials", L"缺少凭证");
    return core::loc::tw("engine.health.unverified", L"未验证");
}

}  // namespace

EnginesPage::EnginesPage() {
    BindPersistence();
    rootStack_ = kit::MakePageRoot(*this);
    rootStack_.Children().Append(kit::MakeSectionHeader(
        kit::Lx("settings.section.engines", L"引擎").c_str()));

    BuildVlmCard();
    BuildPaddleCard();
    BuildMultiEngineCard();

    statusText_ = Controls::TextBlock();
    statusText_.FontSize(12.0);
    statusText_.Opacity(0.7);
    statusText_.TextWrapping(TextWrapping::Wrap);
    rootStack_.Children().Append(statusText_);
}

// ---------------------------------------------------------------------------
// 持久化接线：VM 的 load/save 闭包接 store:: 文件层。
// save 闭包把 VM 输出（serializeEngineConfigPatch("", 镜像) 的 wt-settings
// 全量文档）以磁盘既有文本为合并基提升（未知键全保留），再叠加 VM 拥有
// 的 engineConfigs / compatibleProviderConfigs 两段（Core store 未收录，
// schema §2.5.2/§2.5.3）。
// ---------------------------------------------------------------------------
void EnginesPage::BindPersistence() {
    EnginesPageViewModel::Persistence p;
    p.loadEnginesJson = [] { return store::ReadEnginesJson(); };
    EnginesPage* self = this;
    p.saveEnginesJson = [self](const std::string& vmOut) {
        std::string existing = store::ReadEnginesJson();
        const auto patch = tf::core::parseEngineConfig(vmOut).config;
        std::string merged = tf::core::serializeEngineConfigPatch(existing, patch);
        nlohmann::json root = nlohmann::json::parse(merged, nullptr, false);
        if (root.is_discarded() || !root.is_object()) root = nlohmann::json::object();
        root["engineConfigs"] = nlohmann::json::parse(
            EnginesPageViewModel::SerializeEngineConfigs(self->vm_.EngineConfigMirror()),
            nullptr, false);
        root["compatibleProviderConfigs"] = nlohmann::json::parse(
            EnginesPageViewModel::SerializeCompatibleInstances(self->vm_.CompatibleInstances()),
            nullptr, false);
        if (!store::WriteEnginesJsonAtomic(root.dump(2) + "\n")) {
            ::OutputDebugStringW(L"TransFrame: engines.json 保存失败（引擎页）\n");
        }
    };
    vm_.SetPersistence(std::move(p));
}

// ---------------------------------------------------------------------------
// 卡片 B：VLM 配置（spec §2.2 前半）
// ---------------------------------------------------------------------------
void EnginesPage::BuildVlmCard() {
    kit::Card card = kit::MakeCard();
    card.panel.Children().Append(kit::MakeLabelBlock(
        kit::Lx("settings.vlm.title", L"VLM 配置").c_str(),
        kit::Lx("win.engines.vlm.hint",
                L"视觉语言模型，用于截图翻译前的图像理解（API Key 存于凭据管理器）").c_str()));

    vlmProviderCombo_ = Controls::ComboBox();
    vlmProviderCombo_.Header(winrt::box_value(
            kit::Lx("settings.vlm.provider", L"提供商")));
    vlmProviderCombo_.MinWidth(240.0);
    FillCombo(vlmProviderCombo_,
              {{"openai", L"OpenAI"},
               {"claude", L"Claude"},
               {"gemini", L"Gemini"},
               {"ollama", L"Ollama"},
               {"paddleocr", L"PaddleOCR"}});
    vlmProviderCombo_.SelectionChanged([this](auto&&, auto&&) {
        if (updating_) return;
        const std::string raw = TagOf(vlmProviderCombo_.SelectedItem());
        if (!vm_.IsValidVlmProvider(raw)) return;
        vm_.SetVlmProvider(raw);
        UpdateVlmOpenAIModeVisibility();
        RefreshVlmKeyIndicator();
    });
    card.panel.Children().Append(vlmProviderCombo_);

    vlmBaseUrlBox_ = Controls::TextBox();
    vlmBaseUrlBox_.Header(winrt::box_value(
            kit::Lx("settings.vlm.baseURL", L"Base URL")));
    vlmBaseUrlBox_.MinWidth(320.0);
    card.panel.Children().Append(vlmBaseUrlBox_);

    vlmModelBox_ = Controls::TextBox();
    vlmModelBox_.Header(winrt::box_value(
            kit::Lx("settings.vlm.model", L"模型名")));
    vlmModelBox_.MinWidth(320.0);
    card.panel.Children().Append(vlmModelBox_);

    vlmOpenAIModeCombo_ = Controls::ComboBox();
    vlmOpenAIModeCombo_.Header(winrt::box_value(
            kit::Lx("settings.vlm.apiMode", L"OpenAI 接口模式")));
    vlmOpenAIModeCombo_.MinWidth(200.0);
    FillCombo(vlmOpenAIModeCombo_, {{"chat", L"Chat Completions"}, {"responses", L"Responses"}});
    vlmOpenAIModeCombo_.SelectionChanged([this](auto&&, auto&&) {
        if (updating_) return;
        vm_.SetVlmOpenAIMode(TagOf(vlmOpenAIModeCombo_.SelectedItem()));
    });
    card.panel.Children().Append(vlmOpenAIModeCombo_);

    Controls::StackPanel keyRow;
    keyRow.Spacing(8.0);
    vlmApiKeyBox_ = Controls::PasswordBox();
    vlmApiKeyBox_.Header(winrt::box_value(
            kit::Lx("win.engines.apiKey", L"API Key")));
    vlmApiKeyBox_.MinWidth(320.0);
    vlmApiKeyBox_.PasswordRevealMode(Controls::PasswordRevealMode::Peek);
    vlmApiKeyBox_.PasswordChanged([this](auto&&, auto&&) {
        vlmApiKeyDraft_ = winrt::to_string(vlmApiKeyBox_.Password());
    });
    vlmKeyStatusText_ = Controls::TextBlock();
    vlmKeyStatusText_.VerticalAlignment(VerticalAlignment::Center);
    vlmKeyStatusText_.FontSize(12.0);
    vlmKeyStatusText_.Opacity(0.7);
    keyRow.Children().Append(vlmApiKeyBox_);
    keyRow.Children().Append(vlmKeyStatusText_);
    card.panel.Children().Append(keyRow);

    Controls::Button save;
    save.Content(winrt::box_value(
            kit::Lx("win.engines.saveVlm", L"保存 VLM 配置")));
    save.Click([this](auto&&, auto&&) {
        vm_.SetVlmBaseURL(winrt::to_string(vlmBaseUrlBox_.Text()));
        vm_.SetVlmModelName(winrt::to_string(vlmModelBox_.Text()));
        if (!vlmApiKeyDraft_.empty()) {
            tf::StoredCredentials cred;
            cred.apiKey = vlmApiKeyDraft_;
            credentials_.save(VlmCredentialAccount(vm_.VlmProvider()), cred);
            vlmApiKeyDraft_.clear();
            vlmApiKeyBox_.Password(L"");
            RefreshVlmKeyIndicator();
        }
        statusText_.Text(vm_.SaveVlm()
                         ? kit::Lx("win.engines.vlmSaved", L"VLM 配置已保存")
                         : kit::Lx("win.common.saveFailedEngines",
                                   L"保存失败（engines.json 不可写）"));
    });
    card.panel.Children().Append(save);

    rootStack_.Children().Append(card.border);

    // 回填
    SelectComboByTag(vlmProviderCombo_, vm_.VlmProvider());
    vlmBaseUrlBox_.Text(winrt::to_hstring(vm_.VlmBaseURL()));
    vlmModelBox_.Text(winrt::to_hstring(vm_.VlmModelName()));
    SelectComboByTag(vlmOpenAIModeCombo_, vm_.VlmOpenAIMode());
    UpdateVlmOpenAIModeVisibility();
    RefreshVlmKeyIndicator();
}

void EnginesPage::UpdateVlmOpenAIModeVisibility() {
    vlmOpenAIModeCombo_.Visibility(vm_.VlmShowsOpenAIMode() ? Visibility::Visible
                                                            : Visibility::Collapsed);
}

void EnginesPage::RefreshVlmKeyIndicator() {
    const bool has = credentials_.has(VlmCredentialAccount(vm_.VlmProvider()));
    vlmKeyStatusText_.Text(has
                            ? kit::Lx("win.engines.keyStored", L"已保存密钥（输入可替换）")
                            : kit::Lx("win.engines.keyNotSet", L"未设置密钥"));
}

// ---------------------------------------------------------------------------
// 卡片：PaddleOCR（spec §2.2 后半）
// ---------------------------------------------------------------------------
void EnginesPage::BuildPaddleCard() {
    kit::Card card = kit::MakeCard();
    card.panel.Children().Append(kit::MakeLabelBlock(
        L"PaddleOCR",
        kit::Lx("win.engines.ocr.paddleHint", L"本地 OCR 引擎（可选云端）").c_str()));

    Controls::StackPanel statusRow;
    statusRow.Orientation(Controls::Orientation::Horizontal);
    statusRow.Spacing(8.0);
    statusRow.Children().Append(kit::MakeStatusDot("#9E9E9E"));
    paddleStatusText_ = Controls::TextBlock();
    paddleStatusText_.VerticalAlignment(VerticalAlignment::Center);
    paddleStatusText_.Text(winrt::to_hstring(vm_.PaddleOcrVersionText()));
    statusRow.Children().Append(paddleStatusText_);
    card.panel.Children().Append(statusRow);

    paddleModeCombo_ = Controls::ComboBox();
    paddleModeCombo_.Header(winrt::box_value(
            kit::Lx("win.engines.ocrMode", L"OCR 模式")));
    paddleModeCombo_.MinWidth(220.0);
    FillCombo(paddleModeCombo_,
              {{"fast", kit::Lx("win.engines.ocr.fast", L"快速（fast，约 1s，行分组）").c_str()},
               {"precise", kit::Lx("win.engines.ocr.precise", L"精准（precise，约 12s，VL-1.5）").c_str()}});
    paddleModeCombo_.SelectionChanged([this](auto&&, auto&&) {
        if (updating_) return;
        vm_.SetPaddleOcrMode(TagOf(paddleModeCombo_.SelectedItem()));
    });
    card.panel.Children().Append(paddleModeCombo_);

    paddleUseCloudToggle_ = Controls::ToggleSwitch();
    paddleUseCloudToggle_.Header(winrt::box_value(
            kit::Lx("settings.paddleocr.useCloud", L"使用云端")));
    paddleUseCloudToggle_.OnContent(winrt::box_value(kit::Lx("win.common.on", L"开")));
    paddleUseCloudToggle_.OffContent(winrt::box_value(kit::Lx("win.common.off", L"关")));
    paddleUseCloudToggle_.Toggled([this](auto&&, auto&&) {
        if (updating_) return;
        vm_.SetPaddleOcrUseCloud(paddleUseCloudToggle_.IsOn());
        UpdatePaddleCloudVisibility();
    });
    card.panel.Children().Append(paddleUseCloudToggle_);

    paddleCloudBaseUrlBox_ = Controls::TextBox();
    paddleCloudBaseUrlBox_.Header(winrt::box_value(
            kit::Lx("win.engines.ocr.cloudUrl", L"云端 Base URL")));
    paddleCloudBaseUrlBox_.MinWidth(320.0);
    card.panel.Children().Append(paddleCloudBaseUrlBox_);

    paddleCloudApiKeyBox_ = Controls::PasswordBox();
    paddleCloudApiKeyBox_.Header(winrt::box_value(
            kit::Lx("win.engines.ocr.cloudKey", L"云端 API Key")));
    paddleCloudApiKeyBox_.MinWidth(320.0);
    paddleCloudApiKeyBox_.PasswordRevealMode(Controls::PasswordRevealMode::Peek);
    paddleCloudApiKeyBox_.PasswordChanged([this](auto&&, auto&&) {
        paddleApiKeyDraft_ = winrt::to_string(paddleCloudApiKeyBox_.Password());
    });
    card.panel.Children().Append(paddleCloudApiKeyBox_);

    paddleCloudModelBox_ = Controls::TextBox();
    paddleCloudModelBox_.Header(winrt::box_value(
            kit::Lx("win.engines.ocr.cloudModel", L"云端模型 ID")));
    paddleCloudModelBox_.MinWidth(320.0);
    card.panel.Children().Append(paddleCloudModelBox_);

    Controls::Button save;
    save.Content(winrt::box_value(
            kit::Lx("win.engines.savePaddle", L"保存 PaddleOCR 配置")));
    save.Click([this](auto&&, auto&&) {
        vm_.SetPaddleOcrCloudBaseURL(winrt::to_string(paddleCloudBaseUrlBox_.Text()));
        vm_.SetPaddleOcrCloudModelId(winrt::to_string(paddleCloudModelBox_.Text()));
        if (!paddleApiKeyDraft_.empty()) {
            tf::StoredCredentials cred;
            cred.apiKey = paddleApiKeyDraft_;
            credentials_.save(kPaddleCloudAccount, cred);
            vm_.SetPaddleOcrCloudAPIKey(paddleApiKeyDraft_);
            paddleApiKeyDraft_.clear();
            paddleCloudApiKeyBox_.Password(L"");
        }
        statusText_.Text(vm_.SavePaddleOcr()
                             ? kit::Lx("win.engines.paddleSaved", L"PaddleOCR 配置已保存")
                             : kit::Lx("win.common.saveFailedEngines",
                                       L"保存失败（engines.json 不可写）"));
    });
    card.panel.Children().Append(save);

    rootStack_.Children().Append(card.border);

    SelectComboByTag(paddleModeCombo_, vm_.PaddleOcrMode());
    paddleUseCloudToggle_.IsOn(vm_.PaddleOcrUseCloud());
    paddleCloudBaseUrlBox_.Text(winrt::to_hstring(vm_.PaddleOcrCloudBaseURL()));
    paddleCloudModelBox_.Text(winrt::to_hstring(vm_.PaddleOcrCloudModelId()));
    UpdatePaddleCloudVisibility();
}

void EnginesPage::UpdatePaddleCloudVisibility() {
    const auto v = paddleUseCloudToggle_.IsOn() ? Visibility::Visible : Visibility::Collapsed;
    paddleCloudBaseUrlBox_.Visibility(v);
    paddleCloudApiKeyBox_.Visibility(v);
    paddleCloudModelBox_.Visibility(v);
}

// ---------------------------------------------------------------------------
// 卡片 C：多引擎翻译（spec §2.3）
// ---------------------------------------------------------------------------
void EnginesPage::BuildMultiEngineCard() {

    // C-1 策略
    kit::Card modeCard = kit::MakeCard();
    modeCard.panel.Children().Append(kit::MakeLabelBlock(
        kit::Lx("win.engines.translationPolicy", L"翻译策略").c_str()));
    engineModeCombo_ = Controls::ComboBox();
    engineModeCombo_.MinWidth(260.0);
    FillCombo(engineModeCombo_,
              {{"ordered", kit::Lx("win.engines.policyOrdered", L"顺序回退链（ordered）").c_str()},
               {"compare", kit::Lx("win.engines.policyCompare", L"并行对比（compare）").c_str()}});
    engineModeCombo_.SelectionChanged([this](auto&&, auto&&) {
        if (updating_) return;
        vm_.SetEngineModeV2(TagOf(engineModeCombo_.SelectedItem()));
        vm_.SaveEngineModeV2();
        UpdateModeSectionVisibility();
        statusText_.Text(kit::Lx("win.engines.policySaved", L"翻译策略已保存"));
    });
    modeCard.panel.Children().Append(engineModeCombo_);
    rootStack_.Children().Append(modeCard.border);

    // C-2 顺序回退链
    {
        kit::Card card = kit::MakeCard();
        Controls::StackPanel header;
        header.Orientation(Controls::Orientation::Horizontal);
        header.Spacing(12.0);
        header.Children().Append(kit::MakeText(
            kit::Lx("win.engines.chainTitle", L"顺序回退链").c_str(), true));
        chainCountText_ = Controls::TextBlock();
        chainCountText_.VerticalAlignment(VerticalAlignment::Center);
        chainCountText_.Opacity(0.7);
        header.Children().Append(chainCountText_);
        header.Children().Append(kit::MakeCaption(
            kit::Lx("win.engines.policyOrderedHint", L"按顺序尝试，失败自动回退到下一个").c_str()));
        card.panel.Children().Append(header);

        chainListPanel_ = Controls::StackPanel();
        chainListPanel_.Spacing(8.0);
        card.panel.Children().Append(chainListPanel_);

        addChainButton_ = Controls::Button();
        addChainButton_.Content(winrt::box_value(
            kit::Lx("win.engines.addEngine", L"添加引擎 ▾")));
        card.panel.Children().Append(addChainButton_);

        orderedChainSection_ = card.border;
        rootStack_.Children().Append(orderedChainSection_);
    }

    // C-3 并行对比
    {
        kit::Card card = kit::MakeCard();
        Controls::StackPanel header;
        header.Orientation(Controls::Orientation::Horizontal);
        header.Spacing(12.0);
        header.Children().Append(kit::MakeText(
            kit::Lx("win.engines.compareTitle", L"并行对比引擎").c_str(), true));
        compareCountText_ = Controls::TextBlock();
        compareCountText_.VerticalAlignment(VerticalAlignment::Center);
        compareCountText_.Opacity(0.7);
        header.Children().Append(compareCountText_);
        header.Children().Append(kit::MakeCaption(
            kit::Lx("win.engines.policyCompareHint", L"同时调用，结果并排对比（2–4 个）").c_str()));
        card.panel.Children().Append(header);

        compareListPanel_ = Controls::StackPanel();
        compareListPanel_.Spacing(8.0);
        card.panel.Children().Append(compareListPanel_);

        addCompareButton_ = Controls::Button();
        addCompareButton_.Content(winrt::box_value(
            kit::Lx("win.engines.addEngine", L"添加引擎 ▾")));
        card.panel.Children().Append(addCompareButton_);

        compareSection_ = card.border;
        rootStack_.Children().Append(compareSection_);
    }

    // C-4 可用引擎 + 兼容实例 + 健康刷新
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
        kit::Lx("engine.available.title", L"可用引擎").c_str(),
        kit::Lx("win.engines.available.hint", L"点击名称配置；开关控制是否可加入链/对比集").c_str()));
        engineCardsPanel_ = Controls::StackPanel();
        engineCardsPanel_.Spacing(10.0);
        card.panel.Children().Append(engineCardsPanel_);

        Controls::StackPanel legend;
        legend.Orientation(Controls::Orientation::Horizontal);
        legend.Spacing(12.0);
        legend.Children().Append(kit::MakeStatusDot("#10893E"));
        legend.Children().Append(kit::MakeCaption(
                kit::Lx("engine.health.available", L"可用").c_str()));
        legend.Children().Append(kit::MakeStatusDot("#C42B1C"));
        legend.Children().Append(kit::MakeCaption(
                kit::Lx("win.engines.health.abnormal", L"异常").c_str()));
        legend.Children().Append(kit::MakeStatusDot("#FF8C00"));
        legend.Children().Append(kit::MakeCaption(
                kit::Lx("win.engines.health.noCredentials", L"缺少凭证").c_str()));
        legend.Children().Append(kit::MakeStatusDot("#9E9E9E"));
        legend.Children().Append(kit::MakeCaption(
                kit::Lx("engine.health.unverified", L"未验证").c_str()));
        card.panel.Children().Append(legend);

        Controls::Button refresh;
        refresh.Content(winrt::box_value(
            kit::Lx("win.engines.refreshHealth", L"刷新健康状态")));
        refresh.Click([this](auto&&, auto&&) { RefreshHealth(); });
        card.panel.Children().Append(refresh);

        rootStack_.Children().Append(card.border);
    }

    SelectComboByTag(engineModeCombo_, vm_.EngineModeV2());
    UpdateModeSectionVisibility();
    RebuildChainRows();
    RebuildCompareRows();
    RebuildAddMenus();
    RebuildEngineCards();
}

void EnginesPage::UpdateModeSectionVisibility() {
    orderedChainSection_.Visibility(vm_.IsOrderedMode() ? Visibility::Visible
                                                        : Visibility::Collapsed);
    compareSection_.Visibility(vm_.IsCompareMode() ? Visibility::Visible : Visibility::Collapsed);
}

// ---------------------------------------------------------------------------
// 链 / 对比集行（[↑][↓][替换▾][删除]）
// ---------------------------------------------------------------------------
void EnginesPage::RebuildChainRows() {
    chainListPanel_.Children().Clear();
    const auto rows = vm_.ChainRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        Controls::StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(8.0);

        Controls::Button up;
        up.Content(winrt::box_value(L"↑"));
        up.Click([this, i](auto&&, auto&&) {
            vm_.MoveChainRow(i, i - 1);
            vm_.SaveEngineChainV2();
            RebuildChainRows();
            RebuildEngineCards();
        });
        Controls::Button down;
        down.Content(winrt::box_value(L"↓"));
        down.Click([this, i](auto&&, auto&&) {
            vm_.MoveChainRow(i, i + 1);
            vm_.SaveEngineChainV2();
            RebuildChainRows();
            RebuildEngineCards();
        });

        // 行内替换：候选 = 已启用内置（排除 custom）+ 全部兼容实例
        Controls::MenuFlyout replaceMenu;
        for (const auto& cand : vm_.ChainCandidates()) {
            Controls::MenuFlyoutItem mi;
            mi.Text(winrt::to_hstring(cand.displayName));
            mi.Click([this, i, cand](auto&&, auto&&) {
                vm_.ReplaceChainRow(i, cand);
                vm_.SaveEngineChainV2();
                RebuildChainRows();
                RebuildEngineCards();
            });
            replaceMenu.Items().Append(mi);
        }
        Controls::DropDownButton replace;
        replace.Content(winrt::box_value(winrt::to_hstring(row.displayName)));
        replace.Flyout(replaceMenu);

        Controls::Button del;
        del.Content(winrt::box_value(L"✕"));
        del.Click([this, i](auto&&, auto&&) {
            if (vm_.CanRemoveChainRow()) {
                vm_.RemoveChainRow(i);
                vm_.SaveEngineChainV2();
                RebuildChainRows();
                RebuildEngineCards();
                RebuildAddMenus();
            }
        });

        line.Children().Append(up);
        line.Children().Append(down);
        line.Children().Append(replace);
        line.Children().Append(del);
        chainListPanel_.Children().Append(line);
    }
    chainCountText_.Text(winrt::to_hstring(vm_.ChainCountText()));
}

void EnginesPage::RebuildCompareRows() {
    compareListPanel_.Children().Clear();
    const auto rows = vm_.CompareRows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = rows[i];
        Controls::StackPanel line;
        line.Orientation(Controls::Orientation::Horizontal);
        line.Spacing(8.0);
        line.Children().Append(kit::MakeText(kit::ToWide(row.displayName)));

        Controls::MenuFlyout replaceMenu;
        for (const auto& cand : vm_.CompareCandidates()) {
            Controls::MenuFlyoutItem mi;
            mi.Text(winrt::to_hstring(cand.displayName));
            mi.Click([this, i, cand](auto&&, auto&&) {
                vm_.RemoveCompareRow(i);
                vm_.AppendCompareRow(cand);
                vm_.SaveCompareEnginesV2();
                RebuildCompareRows();
                RebuildEngineCards();
                RebuildAddMenus();
            });
            replaceMenu.Items().Append(mi);
        }
        Controls::DropDownButton replace;
        replace.Content(winrt::box_value(
                kit::Lx("win.engines.replace", L"替换 ▾")));
        replace.Flyout(replaceMenu);

        Controls::Button del;
        del.Content(winrt::box_value(L"✕"));
        del.Click([this, i](auto&&, auto&&) {
            if (vm_.CanRemoveCompareRow()) {
                vm_.RemoveCompareRow(i);
                vm_.SaveCompareEnginesV2();
                RebuildCompareRows();
                RebuildEngineCards();
                RebuildAddMenus();
            }
        });

        line.Children().Append(replace);
        line.Children().Append(del);
        compareListPanel_.Children().Append(line);
    }
    compareCountText_.Text(winrt::to_hstring(vm_.CompareCountText()));
}

void EnginesPage::RebuildAddMenus() {
    auto fill = [](Controls::Button& button,
                   const std::vector<ChainRowViewModel>& candidates,
                   auto onPick) {
        Controls::MenuFlyout menu;
        for (const auto& cand : candidates) {
            Controls::MenuFlyoutItem mi;
            mi.Text(winrt::to_hstring(cand.displayName));
            // onPick 按值捕获：菜单项的点击可能在 fill 返回之后发生。
            mi.Click([cand, onPick](auto&&, auto&&) { onPick(cand); });
            menu.Items().Append(mi);
        }
        button.Flyout(menu);
    };
    fill(addChainButton_, vm_.ChainCandidates(), [this](const ChainRowViewModel& cand) {
        vm_.AppendChainRow(cand);
        vm_.SaveEngineChainV2();
        RebuildChainRows();
        RebuildEngineCards();
    });
    addChainButton_.IsEnabled(vm_.CanAddChainRow());
    fill(addCompareButton_, vm_.CompareCandidates(), [this](const ChainRowViewModel& cand) {
        vm_.AppendCompareRow(cand);
        vm_.SaveCompareEnginesV2();
        RebuildCompareRows();
        RebuildEngineCards();
    });
    addCompareButton_.IsEnabled(vm_.CanAddCompareRow());
}

// ---------------------------------------------------------------------------
// C-4 引擎卡（9 内置分组 + 兼容实例动态）
// ---------------------------------------------------------------------------
void EnginesPage::RebuildEngineCards() {
    engineCardsPanel_.Children().Clear();

    struct Group {
        const char* titleKey;          // FR-37：分组标题走 Localization 表
        const wchar_t* titleFallback;
        std::vector<std::string> raws;
    };
    static const std::vector<Group> kGroups = {
        {"win.engines.group.builtin", L"内置引擎", {"mtran"}},
        {"win.engines.group.llm", L"LLM", {"openai", "claude", "gemini", "ollama"}},
        {"engine.category.cloud", L"云服务", {"google", "deepl", "baidu"}},
        {"win.engines.group.other", L"其他", {"custom"}},
    };

    for (const Group& g : kGroups) {
        Controls::TextBlock header =
            kit::MakeCaption(kit::Lx(g.titleKey, g.titleFallback).c_str());
        header.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
        header.Margin({0, 6, 0, 0});
        engineCardsPanel_.Children().Append(header);

        for (const std::string& raw : g.raws) {
            auto t = translationEngineFromRaw(raw);
            if (!t) continue;
            const std::string displayName = translationEngineDisplayName(*t);

            Controls::StackPanel card;
            card.Orientation(Controls::Orientation::Horizontal);
            card.Spacing(10.0);
            card.Padding({8, 6, 8, 6});
            card.CornerRadius({4, 4, 4, 4});
            card.BorderThickness({1, 1, 1, 1});
            card.BorderBrush(kit::ThemeBrush(L"CardStrokeColorDefaultBrush",
                                              Windows::UI::Color{0xFF, 0x60, 0x60, 0x60}));

            Controls::ToggleSwitch enable;
            enable.OnContent(winrt::box_value(L""));
            enable.OffContent(winrt::box_value(L""));
            enable.IsOn(vm_.BuiltinEnabled(raw));
            std::string rawCopy = raw;
            enable.Toggled([this, rawCopy](winrt::Windows::Foundation::IInspectable const& sender,
                                           auto&&) {
                if (updating_) return;
                auto toggle = sender.as<Controls::ToggleSwitch>();
                vm_.SetBuiltinEnabled(rawCopy, toggle.IsOn());
                vm_.SaveEngineConfigs();
                RebuildAddMenus();
                statusText_.Text(kit::Lx("win.engines.toggleSaved", L"引擎开关已保存"));
            });

            Controls::HyperlinkButton open;
            open.Content(winrt::box_value(winrt::to_hstring(displayName)));
            open.Click([this, raw](auto&&, auto&&) {
                ShowEngineConfigSheetAsync(raw);
            });

            Controls::TextBlock desc =
                    kit::MakeCaption(kit::Lx("win.engines.clickToConfigure", L"点击名称配置").c_str());
            desc.VerticalAlignment(VerticalAlignment::Center);

            Controls::Border dot = kit::MakeStatusDot(vm_.HealthColorHex(raw));
            Controls::ToolTipService::SetToolTip(
                dot, winrt::box_value(HealthStateLabel(vm_.HealthOf(raw))));

            card.Children().Append(enable);
            card.Children().Append(open);
            card.Children().Append(desc);
            card.Children().Append(dot);
            engineCardsPanel_.Children().Append(card);
        }
    }

    // 兼容实例（spec §2.3 C-4：0–5，动态）
    Controls::TextBlock compHeader =
        kit::MakeCaption(kit::Lx("win.engines.compatibleSection", L"兼容引擎（OpenAI 兼容端点）").c_str());
    compHeader.FontWeight(Windows::UI::Text::FontWeights::SemiBold());
    compHeader.Margin({0, 6, 0, 0});
    engineCardsPanel_.Children().Append(compHeader);

    for (const auto& inst : vm_.CompatibleInstances()) {
        Controls::StackPanel card;
        card.Orientation(Controls::Orientation::Horizontal);
        card.Spacing(10.0);
        card.Padding({8, 6, 8, 6});
        card.CornerRadius({4, 4, 4, 4});
        card.BorderThickness({1, 1, 1, 1});
        card.BorderBrush(kit::ThemeBrush(L"CardStrokeColorDefaultBrush",
                                          Windows::UI::Color{0xFF, 0x60, 0x60, 0x60}));

        Controls::FontIcon icon;
        icon.Glyph(L"");  // gearshape.2
        icon.FontSize(14.0);
        icon.VerticalAlignment(VerticalAlignment::Center);

        Controls::HyperlinkButton open;
        open.Content(winrt::box_value(winrt::to_hstring(inst.displayName)));
        std::string id = inst.id;
        open.Click([this, id](auto&&, auto&&) { ShowCompatibleSheetAsync(id); });

        Controls::TextBlock inUse = kit::MakeCaption(
            vm_.CompatibleInUse(inst.id)
                ? kit::Lx("win.engines.inUse", L"使用中").c_str()
                : kit::Lx("win.engines.notInChain", L"未加入链/对比集").c_str());
        inUse.VerticalAlignment(VerticalAlignment::Center);

        Controls::Border dot = kit::MakeStatusDot(vm_.HealthColorHex(inst.id));
        Controls::ToolTipService::SetToolTip(
            dot, winrt::box_value(HealthStateLabel(vm_.HealthOf(inst.id))));

        Controls::Button del;
        del.Content(winrt::box_value(kit::Lx("action.delete", L"删除")));
        del.Click([this, id](auto&&, auto&&) {
            vm_.RemoveCompatibleInstance(id);
            vm_.SaveEngineConfigs();
            RebuildEngineCards();
            RebuildAddMenus();
            RebuildChainRows();
            RebuildCompareRows();
            statusText_.Text(kit::Lx("win.engines.compatibleDeleted", L"兼容实例已删除"));
        });

        card.Children().Append(icon);
        card.Children().Append(open);
        card.Children().Append(inUse);
        card.Children().Append(dot);
        card.Children().Append(del);
        engineCardsPanel_.Children().Append(card);
    }

    if (vm_.CanAddCompatibleInstance()) {
        Controls::Button add;
        add.Content(winrt::box_value(
            kit::Lx("engine.compatible.new", L"新建兼容引擎")));
        add.Click([this](auto&&, auto&&) { ShowCompatibleSheetAsync({}); });
        engineCardsPanel_.Children().Append(add);
    } else if (!vm_.CompatibleInstances().empty()) {
        engineCardsPanel_.Children().Append(kit::MakeCaption(
            kit::Lx("win.engines.compatibleLimit", L"兼容引擎已达上限（5）").c_str()));
    }
}

// ---------------------------------------------------------------------------
// Sheet 1：引擎配置（ContentDialog，spec §2.4）
// ---------------------------------------------------------------------------
winrt::Windows::Foundation::IAsyncOperation<Controls::ContentDialogResult>
EnginesPage::ShowEngineConfigSheetAsync(const std::string& engineRaw) {
    auto draft = vm_.OpenBuiltinSheetDraft(engineRaw);
    bool requiresKey = false, requiresAppId = false, hasBaseUrl = false, hasModel = false;
    const bool isMtran = engineRaw == "mtran";
    const bool isOpenAI = engineRaw == "openai";
    if (auto t = translationEngineFromRaw(engineRaw)) {
        requiresKey = translationEngineRequiresApiKey(*t);
        requiresAppId = translationEngineRequiresAppId(*t);
        hasBaseUrl = !translationEngineDefaultBaseUrl(*t).empty() || isMtran;
        hasModel = !translationEngineDefaultModelName(*t).empty() || engineRaw == "custom";
    }

    Controls::StackPanel content;
    content.Spacing(12.0);
    content.MinWidth(360.0);

    Controls::ComboBox presetCombo{nullptr};
    if (isOpenAI) {
        presetCombo = Controls::ComboBox();
        presetCombo.Header(winrt::box_value(
                kit::Lx("win.engines.presets", L"预设")));
        FillCombo(presetCombo,
                  {{"openAI", kit::Lx("engine.preset.openai", L"OpenAI 官方").c_str()},
                   {"ollamaLocal", kit::Lx("engine.preset.ollama", L"本地 Ollama").c_str()},
                   {"custom", kit::Lx("engine.preset.custom", L"自定义").c_str()}});
        SelectComboByTag(presetCombo, vm_.OpenAIPreset());
        content.Children().Append(presetCombo);
    }

    Controls::ToggleSwitch enableSwitch;
    enableSwitch.Header(winrt::box_value(
            kit::Lx("win.engines.enabledShort", L"启用")));
    enableSwitch.OnContent(winrt::box_value(kit::Lx("win.common.on", L"开")));
    enableSwitch.OffContent(winrt::box_value(kit::Lx("win.common.off", L"关")));
    enableSwitch.IsOn(draft.isEnabled);
    content.Children().Append(enableSwitch);

    Controls::TextBox baseUrlBox;
    if (hasBaseUrl) {
        baseUrlBox.Header(winrt::box_value(
            kit::Lx("settings.vlm.baseURL", L"Base URL")));
        baseUrlBox.PlaceholderText(isMtran ? L"http://localhost:8989" : L"");
        baseUrlBox.Text(winrt::to_hstring(draft.baseURL.empty() && isMtran
                                              ? "http://localhost:8989"
                                              : draft.baseURL));
        content.Children().Append(baseUrlBox);
    }

    Controls::TextBox modelBox;
    if (hasModel) {
        modelBox.Header(winrt::box_value(
            kit::Lx("settings.vlm.model", L"模型名")));
        modelBox.Text(winrt::to_hstring(draft.modelName));
        content.Children().Append(modelBox);
    }

    Controls::PasswordBox keyBox;
    bool showsKey = requiresKey;
    if (showsKey) {
        keyBox.Header(winrt::box_value(
            kit::Lx("win.engines.apiKey", L"API Key")));
        keyBox.PasswordRevealMode(Controls::PasswordRevealMode::Peek);
        const bool hasSaved = credentials_.has(credentialAccount(*translationEngineFromRaw(engineRaw)));
        keyBox.PlaceholderText(hasSaved
                                  ? kit::Lx("win.engines.keyStored", L"已保存密钥（输入可替换）")
                                  : kit::Lx("win.engines.keyNotSet", L"未设置"));
        content.Children().Append(keyBox);
    }

    Controls::PasswordBox appIdBox{nullptr}, secretBox{nullptr};
    if (requiresAppId) {
        appIdBox = Controls::PasswordBox();
        appIdBox.Header(winrt::box_value(L"App ID"));
        secretBox = Controls::PasswordBox();
        secretBox.Header(winrt::box_value(L"Secret Key"));
        auto saved = credentials_.load(credentialAccount(*translationEngineFromRaw(engineRaw)));
        if (saved) {
            appIdBox.Password(winrt::to_hstring(saved->appID));
            if (!saved->apiKey.empty()) {
                secretBox.PlaceholderText(
                    kit::Lx("win.engines.keyStored", L"已保存密钥（输入可替换）"));
            }
        }
        content.Children().Append(appIdBox);
        content.Children().Append(secretBox);
    }

    // 预设联动：ollamaLocal / custom+本地 URL 隐藏 API Key（spec §2.4）
    auto applyPresetVisibility = [&]() {
        if (!isOpenAI || !showsKey) return;
        const std::string preset = presetCombo ? TagOf(presetCombo.SelectedItem()) : "openAI";
        const bool local =
            preset == "ollamaLocal" || (preset == "custom" && IsLocalEndpoint(winrt::to_string(baseUrlBox.Text())));
        keyBox.Visibility(local ? Visibility::Collapsed : Visibility::Visible);
        if (preset == "ollamaLocal") baseUrlBox.Text(L"http://localhost:11434");
    };
    if (isOpenAI && hasBaseUrl) {
        presetCombo.SelectionChanged([applyPresetVisibility](auto&&, auto&&) { applyPresetVisibility(); });
        applyPresetVisibility();
    }

    // 测试连接：后台线程经 Core EngineHealthChecker 探测（仅免费端点，
    // FR-22），结果回投 STA 更新文本；对话框若已关闭，TextBlock 仍被
    // 引用计数保活，置文本无副作用。
    Controls::TextBlock testResult;
    testResult.FontSize(12.0);
    Controls::Button testBtn;
    testBtn.Content(winrt::box_value(
            kit::Lx("engine.config.test", L"测试连接")));
    testBtn.Click([this, engineRaw, testResult](auto&&, auto&&) {
        auto engine = translationEngineFromRaw(engineRaw);
        if (!engine) return;
        testResult.Text(kit::Lx("win.engines.healthTesting", L"测试中…"));
        auto checker = MakeHealthChecker();
        auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
        std::thread([checker, engine = *engine, queue, testResult]() {
            const EngineHealth h = checker->refresh(EngineHealthKey::forBuiltin(engine));
            const std::wstring text =
                h == EngineHealth::Available
                    ? core::loc::tw("win.engines.legendGreen", L"连接正常（绿）")
                    : (h == EngineHealth::Unconfigured
                           ? core::loc::tw("win.engines.legendOrange", L"缺少凭证（橙）")
                           : core::loc::tw("win.engines.legendRed", L"连接失败（红）"));
            queue.TryEnqueue(
                [testResult, text]() { testResult.Text(text.c_str()); });
        }).detach();
    });
    content.Children().Append(testBtn);
    content.Children().Append(testResult);

    Controls::ContentDialog dialog;
    dialog.Title(winrt::box_value(core::loc::formatArgsWide(
        kit::Lx("win.engines.configureEngine", L"配置 %@"),
        {kit::ToWide(engineRaw)})));
    dialog.Content(content);
    dialog.PrimaryButtonText(kit::Lx("action.save", L"保存"));
    dialog.CloseButtonText(kit::Lx("action.cancel", L"取消"));
    dialog.DefaultButton(Controls::ContentDialogButton::Primary);
    dialog.XamlRoot(XamlRoot());

    const Controls::ContentDialogResult result = co_await dialog.ShowAsync();
    if (result == Controls::ContentDialogResult::Primary) {
        draft.isEnabled = enableSwitch.IsOn();
        if (hasBaseUrl) draft.baseURL = winrt::to_string(baseUrlBox.Text());
        if (hasModel) draft.modelName = winrt::to_string(modelBox.Text());
        vm_.CommitBuiltinSheet(draft);
        // 凭证 → CredMan（FR-36：明文不落 engines.json）
        if (auto t = translationEngineFromRaw(engineRaw)) {
            const std::string account = credentialAccount(*t);
            tf::StoredCredentials cred;
            auto existing = credentials_.load(account);
            if (existing) cred = *existing;
            bool touched = false;
            if (showsKey && keyBox.Visibility() == Visibility::Visible &&
                !winrt::to_string(keyBox.Password()).empty()) {
                cred.apiKey = winrt::to_string(keyBox.Password());
                touched = true;
            }
            if (requiresAppId) {
                const std::string appId = winrt::to_string(appIdBox.Password());
                const std::string secret = winrt::to_string(secretBox.Password());
                if (!appId.empty() || !secret.empty()) {
                    if (!appId.empty()) cred.appID = appId;
                    if (!secret.empty()) cred.apiKey = secret;
                    touched = true;
                }
            }
            if (touched) credentials_.save(account, cred);
        }
        vm_.SaveEngineConfigs();
        RebuildEngineCards();
        RebuildAddMenus();
        statusText_.Text(kit::Lx("win.engines.engineSaved", L"引擎配置已保存"));
    }
    co_return result;
}

// ---------------------------------------------------------------------------
// Sheet 2：兼容引擎配置（ContentDialog，spec §2.5）
// ---------------------------------------------------------------------------
winrt::Windows::Foundation::IAsyncOperation<Controls::ContentDialogResult>
EnginesPage::ShowCompatibleSheetAsync(const std::string& instanceId) {
    const bool isNew = instanceId.empty();
    auto draft = isNew ? vm_.NewCompatibleSheetDraft() : vm_.OpenCompatibleSheetDraft(instanceId);

    Controls::TextBox nameBox;
    nameBox.Header(winrt::box_value(
            kit::Lx("engine.compatible.displayName", L"显示名称")));
    nameBox.Text(winrt::to_hstring(draft.displayName));
    Controls::TextBox baseUrlBox;
    baseUrlBox.Header(winrt::box_value(
            kit::Lx("settings.vlm.baseURL", L"Base URL")));
    baseUrlBox.PlaceholderText(L"http://localhost:8000/v1");
    baseUrlBox.Text(winrt::to_hstring(draft.baseURL));
    Controls::TextBox modelBox;
    modelBox.Header(winrt::box_value(
            kit::Lx("settings.vlm.model", L"模型名")));
    modelBox.PlaceholderText(L"gpt-4o-mini");
    modelBox.Text(winrt::to_hstring(draft.modelName));
    Controls::PasswordBox keyBox;
    keyBox.Header(winrt::box_value(
            kit::Lx("win.engines.apiKey", L"API Key")));
    keyBox.PasswordRevealMode(Controls::PasswordRevealMode::Peek);
    if (!isNew && draft.hasAPIKey) {
        keyBox.PlaceholderText(
            kit::Lx("win.engines.keyStored", L"已保存密钥（输入可替换）"));
    }

    Controls::StackPanel content;
    content.Spacing(12.0);
    content.MinWidth(360.0);
    content.Children().Append(nameBox);
    content.Children().Append(baseUrlBox);
    content.Children().Append(modelBox);
    content.Children().Append(keyBox);

    Controls::ContentDialog dialog;
    dialog.Title(winrt::box_value(
        isNew ? kit::Lx("engine.compatible.new", L"新建兼容引擎")
              : kit::Lx("win.engines.editCompatible", L"编辑兼容引擎")));
    dialog.Content(content);
    dialog.PrimaryButtonText(kit::Lx("action.save", L"保存"));
    dialog.CloseButtonText(kit::Lx("action.cancel", L"取消"));
    dialog.DefaultButton(Controls::ContentDialogButton::Primary);
    dialog.XamlRoot(XamlRoot());

    const Controls::ContentDialogResult result = co_await dialog.ShowAsync();
    if (result == Controls::ContentDialogResult::Primary) {
        draft.displayName = winrt::to_string(nameBox.Text());
        draft.baseURL = winrt::to_string(baseUrlBox.Text());
        draft.modelName = winrt::to_string(modelBox.Text());
        const std::string typedKey = winrt::to_string(keyBox.Password());
        if (isNew) draft.id = tf::Uuid::random().toString();
        if (!typedKey.empty()) {
            draft.hasAPIKey = true;
            tf::StoredCredentials cred;
            cred.apiKey = typedKey;
            credentials_.save(credentialAccountForCompatible(draft.id), cred);
        }
        vm_.CommitCompatibleSheet(draft);
        vm_.SaveEngineConfigs();
        RebuildEngineCards();
        RebuildAddMenus();
        statusText_.Text(kit::Lx("win.engines.compatibleSaved", L"兼容实例已保存"));
    }
    co_return result;
}

// ---------------------------------------------------------------------------
// 健康探测：后台线程逐 key 探测（Core EngineHealthChecker + CredMan +
// WinHttpTransport），结果经 DispatcherQueue 回 STA 落 VM 再重绘。
// 探针的兼容实例表按刷新时刻快照注入（后台线程不触碰 STA 侧 VM）。
// ---------------------------------------------------------------------------
std::shared_ptr<tf::EngineHealthChecker> EnginesPage::MakeHealthChecker() {
    std::vector<CompatibleConfig> snapshot;
    for (const auto& inst : vm_.CompatibleInstances()) {
        CompatibleConfig c;
        c.id = inst.id;
        c.displayName = inst.displayName;
        c.baseURL = inst.baseURL;
        c.modelName = inst.modelName;
        c.hasAPIKey = inst.hasAPIKey;
        snapshot.push_back(std::move(c));
    }
    return std::make_shared<tf::EngineHealthChecker>(
        std::make_shared<tf::DefaultEngineProbe>(std::make_shared<tf::CredManKeyStore>(),
                                                 std::make_shared<tf::WinHttpTransport>(),
                                                 [snapshot]() { return snapshot; }));
}

void EnginesPage::RefreshHealth() {
    // 探测集：全部内置卡（custom 无端点跳过）+ 全部兼容实例。
    auto keys = std::make_shared<std::vector<EngineHealthKey>>();
    for (const auto& raw : vm_.BuiltinCardEngineRaws()) {
        if (raw == "custom") continue;
        if (auto t = translationEngineFromRaw(raw)) keys->push_back(EngineHealthKey::forBuiltin(*t));
    }
    for (const auto& inst : vm_.CompatibleInstances()) {
        keys->push_back(EngineHealthKey::forCompatible(inst.id));
    }

    statusText_.Text(kit::Lx("win.engines.healthChecking", L"正在检查引擎健康状态…"));
    auto checker = MakeHealthChecker();
    auto queue = winrt::Microsoft::UI::Dispatching::DispatcherQueue::GetForCurrentThread();
    auto weak = get_weak();
    std::thread([checker, keys, queue, weak]() {
        std::vector<std::pair<std::string, std::string>> results;
        for (const auto& key : *keys) {
            const EngineHealth h = checker->refresh(key);
            results.push_back({key.isBuiltin ? translationEngineRaw(key.builtin)
                                             : key.compatibleId,
                               engineHealthName(h)});
        }
        // weak.get() 在 STA 上的回调里解析（TryEnqueue 投递）；页面已销毁 → 空跳过。
        queue.TryEnqueue([results, weak]() {
            if (auto self = weak.get()) self->ApplyHealthResults(results);
        });
    }).detach();
}

void EnginesPage::ApplyHealthResults(
    const std::vector<std::pair<std::string, std::string>>& results) {
    for (const auto& [key, state] : results) vm_.SetHealthOf(key, state);
    RebuildEngineCards();
    statusText_.Text(kit::Lx("win.engines.healthRefreshed", L"健康状态已刷新"));
}

}  // namespace tf::ui
