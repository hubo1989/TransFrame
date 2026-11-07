// windows/UI/AdvancedPage/AdvancedPage.cpp
// 高级页签 code-behind 实现。C++/WinRT 纯头投影，不写 .idl、不用 C++/CX。

#include "AdvancedPage.h"
#if __has_include("AdvancedPage.g.cpp")
#include "AdvancedPage.g.cpp"
#endif

#include <winrt/Windows.UI.Xaml.Automation.h>
#include <winrt/Windows.UI.Xaml.Shapes.h>

#include <cstdio>
#include <string>

namespace winrt::TransFrame::UI::implementation {

using namespace Windows::UI;
using namespace Windows::UI::Xaml;
using namespace Windows::UI::Xaml::Controls;
using namespace Windows::UI::Xaml::Controls::Primitives;
using namespace Windows::UI::Xaml::Media;
using namespace Windows::UI::Xaml::Shapes;

// ---------- ColorConfig ↔ Windows::UI::Color 转换器 ----------
// VM 保持平台干净；转换在 code-behind 完成（Core ColorConfig 分量为 0..1 double，
// Windows::UI::Color 为 0..255 uint8_t）。
Windows::UI::Color AdvancedPage::ToUiColor(tf::core::ColorConfig const& c) {
    auto clamp01 = [](double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); };
    return Windows::UI::Color{
        /*A*/ static_cast<uint8_t>(clamp01(c.opacity) * 255.0 + 0.5),
        /*R*/ static_cast<uint8_t>(clamp01(c.red)   * 255.0 + 0.5),
        /*G*/ static_cast<uint8_t>(clamp01(c.green) * 255.0 + 0.5),
        /*B*/ static_cast<uint8_t>(clamp01(c.blue)  * 255.0 + 0.5),
    };
}

tf::core::ColorConfig AdvancedPage::FromUiColor(Windows::UI::Color const& c) {
    return tf::core::ColorConfig{
        c.R / 255.0, c.G / 255.0, c.B / 255.0, c.A / 255.0,
    };
}

// ---------- 构造 ----------
AdvancedPage::AdvancedPage() {
    InitializeComponent();

    // [集成点] shell 在构造前从 %APPDATA%\TransFrame\engines.json 读取现有内容
    //          （tf::core::parseEngineConfig），把结果注入 VM：
    //          m_viewModel = AdvancedPageViewModel(parseResult.config);
    //          m_existingJson = 文件原文（保留未知键合并所需）。
    //          当前保守选择：默认值初始化，构造后由 shell 注入。

    BuildColorButtons();
    SyncFromViewModel();
}

// ---------- x:Bind 目标 ----------
hstring AdvancedPage::StrokeWidthText() const {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.1f", m_viewModel.strokeWidth());
    return hstring(buf);
}

hstring AdvancedPage::TextSizeText() const {
    wchar_t buf[32];
    swprintf(buf, 32, L"%.0f", m_viewModel.textSize());
    return hstring(buf);
}

// ---------- 事件处理器 ----------
void AdvancedPage::OnStrokeWidthChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args) {
    if (m_viewModel.setStrokeWidth(args.NewValue())) {
        StrokeWidthValueText().Text(StrokeWidthText());
        PersistAndNotifyShell();
    }
}

void AdvancedPage::OnTextSizeChanged(IInspectable const&, RangeBaseValueChangedEventArgs const& args) {
    if (m_viewModel.setTextSize(args.NewValue())) {
        TextSizeValueText().Text(TextSizeText());
        PersistAndNotifyShell();
    }
}

void AdvancedPage::OnColorButtonClick(IInspectable const& sender, RoutedEventArgs const&) {
    if (auto btn = sender.try_as<Button>()) {
        auto tag = btn.Tag().try_as<Windows::Foundation::IPropertyValue>();
        if (tag) {
            int32_t index = tag.GetInt32();
            auto const& presets = AdvancedPageViewModel::presetColors();
            if (index >= 0 && static_cast<size_t>(index) < presets.size()) {
                if (m_viewModel.setStrokeColor(presets[static_cast<size_t>(index)].color)) {
                    UpdateColorSelectionVisuals();
                    PersistAndNotifyShell();
                }
            }
        }
    }
}

void AdvancedPage::OnResetAllClick(IInspectable const&, RoutedEventArgs const&) {
    // 全部重置：ContentDialog 二次确认（spec：destructive + 确认弹窗）。
    ContentDialog dialog;
    dialog.Title(box_value(L"全部重置"));
    dialog.Content(box_value(L"将把描边颜色、描边宽度和文本大小恢复为默认值。此操作无法撤销。"));
    dialog.PrimaryButtonText(L"重置");
    dialog.CloseButtonText(L"取消");
    dialog.DefaultButton(ContentDialogButton::Close);

    // [集成点] ContentDialog 需要.XamlRoot（嵌入 NavigationView Content 场景下从
    //          本页 XamlRoot() 取）；此处异步协程等待结果，确认后恢复默认。
    dialog.XamlRoot(this->XamlRoot());

    // fire-and-forget 协程：确认后恢复默认并通知 shell。
    [] (AdvancedPage* self, ContentDialog dlg) -> fire_and_forget {
        co_await dlg.ShowAsync();
        if (dlg.Result() == ContentDialogResult::Primary) {
            self->m_viewModel.resetToDefaults();
            self->SyncFromViewModel();
            self->UpdateColorSelectionVisuals();
            self->PersistAndNotifyShell();
        }
    }(this, dialog);
}

// ---------- 私有 ----------
void AdvancedPage::BuildColorButtons() {
    auto const& presets = AdvancedPageViewModel::presetColors();
    m_colorButtons.clear();
    m_colorButtons.reserve(presets.size());
    for (size_t i = 0; i < presets.size(); ++i) {
        Button btn;
        btn.Tag(box_value(static_cast<int32_t>(i)));
        // AutomationProperties.Name 用预设英文名，供读屏；显示用纯色圆形。
        Windows::UI::Xaml::Automation::AutomationProperties::SetName(
            btn, winrt::to_hstring(presets[i].name));

        // 圆形色块：Ellipse 填充预设色；外层 Border 3px 用于选中态外环。
        Ellipse fill;
        fill.Fill(SolidColorBrush(ToUiColor(presets[i].color)));
        Border swatch;
        swatch.Child(fill);
        swatch.CornerRadius(CornerRadius(18.0));
        swatch.Padding(Thickness(0));
        btn.Content(swatch);
        btn.Padding(Thickness(0));
        btn.MinWidth(0); btn.MinHeight(0);
        btn.Width(36); btn.Height(36);
        btn.CornerRadius(CornerRadius(18.0));
        btn.Click({ this, &AdvancedPage::OnColorButtonClick });
        StrokeColorPanel().Children().Append(btn);
        m_colorButtons.push_back(btn);
    }
    UpdateColorSelectionVisuals();
}

void AdvancedPage::UpdateColorSelectionVisuals() {
    auto const& presets = AdvancedPageViewModel::presetColors();
    for (size_t i = 0; i < m_colorButtons.size(); ++i) {
        bool selected = m_viewModel.isColorSelected(presets[i].color);
        // 选中带外环：Border 双层实现（边框粗 3、选中色环用主题强调色）。
        auto swatch = m_colorButtons[i].Content().as<Border>();
        swatch.BorderThickness(Thickness(selected ? 3.0 : 1.0));
        swatch.BorderBrush(selected
            ? Application::Current().Resources()
                  .Lookup(box_value(L"AccentFillColorDefaultBrush")).as<Brush>()
            : Application::Current().Resources()
                  .Lookup(box_value(L"ControlStrokeColorDefaultBrush")).as<Brush>());
        // 内层色块内缩，保证环不遮色：padding 随边框增减。
        swatch.Padding(Thickness(selected ? 3.0 : 0.0));
    }
}

void AdvancedPage::SyncFromViewModel() {
    StrokeWidthSlider().Value(m_viewModel.strokeWidth());
    TextSizeSlider().Value(m_viewModel.textSize());
    StrokeWidthValueText().Text(StrokeWidthText());
    TextSizeValueText().Text(TextSizeText());
    UpdateColorSelectionVisuals();
}

void AdvancedPage::PersistAndNotifyShell() {
    // [集成点] 落盘：shell 侧负责文件 IO（对齐 Core 纯函数模式——VM 只产 JSON 文本）。
    //          1) 以 m_existingJson 为底，m_viewModel.serializePatchInto(m_existingJson)
    //             生成合并后的全文；
    //          2) tf::core::writeEngineConfigFileAtomic 写 %APPDATA%\TransFrame\engines.json；
    //          3) 更新 m_existingJson 为新内容（下一次合并保留所有未知键）。
    //          [待确认] 写盘节流策略（每拖动 tick 落盘 vs 停止拖动后落盘）：
    //          当前保守选择：即时落盘，节流由 shell 侧实现。
    // [集成点] shell 侧叠加层重绘：描边/文本样式变化后应重绘当前标注
    //          （mac 对应 appDelegate 的 overlay 重绘路径）。
    // [集成点] 全部重置确认后：shell 调 HotkeyManager 重新注册热键
    //          （mac: appDelegate.updateHotkeys()）；本页三个字段本身不影响热键，
    //          但 spec 要求 reset 流程一并调用 updateHotkeys，故统一挂在
    //          PersistAndNotifyShell 之后的 shell 回调里（由 reset 路径触发）。
}

} // namespace winrt::TransFrame::UI::implementation
