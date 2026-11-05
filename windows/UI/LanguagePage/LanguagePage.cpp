// LanguagePage.cpp — 语言页签 code-behind 实现。
//
// 职责：把 VM 的两张选项表（源/目标）填进四个 ComboBox，Tag 存 rawValue，
// SelectionChanged 时按角色写回 VM 包装的 tf::core::EngineConfig 字段。
// [集成点] XAML 编译器按 x:Class 生成的连接代码（LanguagePage.g.cpp /
// LanguagePage.XamlTypeInfo.*）在构建时并入本编译单元；目录加入 vcxproj 后
// 生效。若并入带 pch 的工程，可将下面的 winrt 头移入 pch。
#include "LanguagePage.h"

#if __has_include("LanguagePage.g.cpp")
#include "LanguagePage.g.cpp"
#endif

#include <cstdint>
#include <string>
#include <vector>

namespace winrt::TransFrame::UI::implementation {

namespace {

// raw 值均为 ASCII 短码（"auto"/"en"/"zh-Hans"/…），逐字节加宽/收窄即可，
// 不依赖 locale 或 cppwinrt 的窄宽转换语义。
winrt::hstring RawToHString(std::string const& raw) {
    return winrt::hstring{std::wstring(raw.begin(), raw.end())};
}

std::string HStringToRaw(winrt::hstring const& value) {
    std::wstring const wide{value};
    return std::string(wide.begin(), wide.end());
}

void FillPicker(Microsoft::UI::Xaml::Controls::ComboBox const& picker,
                std::vector<tf::ui::LanguageOption> const& options) {
    picker.Items().Clear();
    for (tf::ui::LanguageOption const& option : options) {
        Microsoft::UI::Xaml::Controls::ComboBoxItem item;
        item.Content(winrt::box_value(winrt::hstring{option.displayName}));
        item.Tag(winrt::box_value(RawToHString(option.raw)));  // Tag=rawValue，不依赖显示文本反查
        picker.Items().Append(item);
    }
}

void SelectRaw(Microsoft::UI::Xaml::Controls::ComboBox const& picker,
               std::string const& raw, std::int32_t fallbackIndex) {
    for (std::uint32_t i = 0; i < picker.Items().Size(); ++i) {
        auto item = picker.Items().GetAt(i).as<Microsoft::UI::Xaml::Controls::ComboBoxItem>();
        if (item.Tag() &&
            HStringToRaw(winrt::unbox_value<winrt::hstring>(item.Tag())) == raw) {
            picker.SelectedIndex(i);
            return;
        }
    }
    // 未知持久化值的保守回退：下标 0（源=自动检测 / 目标=跟随系统）；
    // 原值保留在 EngineConfig 中，用户不改动就不丢失。
    picker.SelectedIndex(fallbackIndex);
}

}  // namespace

LanguagePage::LanguagePage() {
    InitializeComponent();
    RebuildPickers();  // 未 Attach 时按默认值显示（源 auto / 目标“跟随系统”）
}

void LanguagePage::AttachEngineConfig(tf::core::EngineConfig& config) {
    m_viewModel.Attach(config);
    RebuildPickers();
}

void LanguagePage::OnGlobalSourceLanguageSelected(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& /*args*/) {
    ApplySelectionFromPicker(sender, PickerRole::GlobalSource);
}

void LanguagePage::OnGlobalTargetLanguageSelected(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& /*args*/) {
    ApplySelectionFromPicker(sender, PickerRole::GlobalTarget);
}

void LanguagePage::OnInsertSourceLanguageSelected(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& /*args*/) {
    ApplySelectionFromPicker(sender, PickerRole::InsertSource);
}

void LanguagePage::OnInsertTargetLanguageSelected(
    Windows::Foundation::IInspectable const& sender,
    Microsoft::UI::Xaml::Controls::SelectionChangedEventArgs const& /*args*/) {
    ApplySelectionFromPicker(sender, PickerRole::InsertTarget);
}

void LanguagePage::RebuildPickers() {
    m_updating = true;
    // x:Name 访问器（GlobalSourceLanguagePicker() 等）来自 LanguagePage.g.h
    FillPicker(GlobalSourceLanguagePicker(), m_viewModel.SourceLanguageOptions());
    FillPicker(GlobalTargetLanguagePicker(), m_viewModel.TargetLanguageOptions());
    FillPicker(InsertSourceLanguagePicker(), m_viewModel.SourceLanguageOptions());
    FillPicker(InsertTargetLanguagePicker(), m_viewModel.TargetLanguageOptions());

    SelectRaw(GlobalSourceLanguagePicker(), m_viewModel.GlobalSourceLanguage(), 0);
    SelectRaw(GlobalTargetLanguagePicker(), m_viewModel.GlobalTargetLanguage(), 0);
    SelectRaw(InsertSourceLanguagePicker(), m_viewModel.InsertSourceLanguage(), 0);
    SelectRaw(InsertTargetLanguagePicker(), m_viewModel.InsertTargetLanguage(), 0);
    m_updating = false;
}

void LanguagePage::ApplySelectionFromPicker(
    Windows::Foundation::IInspectable const& sender, PickerRole role) {
    if (m_updating) return;  // 初始回填引发的选中变化不落库
    auto combo = sender.try_as<Microsoft::UI::Xaml::Controls::ComboBox>();
    if (!combo || !m_viewModel.IsAttached()) return;
    auto item = combo.SelectedItem().try_as<Microsoft::UI::Xaml::Controls::ComboBoxItem>();
    if (!item || !item.Tag()) return;
    std::string const raw = HStringToRaw(winrt::unbox_value<winrt::hstring>(item.Tag()));

    switch (role) {
        case PickerRole::GlobalSource: m_viewModel.SetGlobalSourceLanguage(raw); break;
        case PickerRole::GlobalTarget: m_viewModel.SetGlobalTargetLanguage(raw); break;
        case PickerRole::InsertSource: m_viewModel.SetInsertSourceLanguage(raw); break;
        case PickerRole::InsertTarget: m_viewModel.SetInsertTargetLanguage(raw); break;
    }
    // [集成点] 值已写入宿主持有的 EngineConfig；此后写盘（tf::core
    // writeEngineConfigFileAtomic，目标选“跟随系统”时以空串 targetLang 落 JSON——
    // Core put(...) 写空串值而非删键，读取时空串/缺键均回退默认，语义等价）
    // 与翻译管线语言刷新的时机由宿主统一决定（对应 mac 的 settings 持久化 + 引擎重载）。
}

}  // namespace winrt::TransFrame::UI::implementation
