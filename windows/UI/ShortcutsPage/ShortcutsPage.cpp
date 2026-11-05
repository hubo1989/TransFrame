// ShortcutsPage.cpp — 快捷键页签 code-behind 实现。
// 键盘捕获：UserControl KeyDown（XAML 已设 IsTabStop=True + UseSystemFocusVisuals），
// 录制开始时 GrabFocus 拿到键盘焦点。
// [集成点] 本页嵌入 shell 窗口（NavigationView Content）时，若 shell 层有
// win32 加速键表（TranslateAccelerator），录制期间需让路：焦点在本控件时
// shell 不 TranslateAccelerator（或注册时排除该消息），否则 WM_KEYDOWN 会被
// 加速键表吞掉，KeyDown 不触发。
// [集成点] 持久化：写盘用 Core 的 HotkeySettingsStore（serialize/parse 纯函数，
// IO 在调用方），写盘后需通知 shell 调 HotkeyManager::reconfigure 重注册并
// 订阅其 HotkeyRegistrationReport 回调，把 report.conflicts 映射回本页行标红
// （注册期冲突，页内 DetectConflicts 检测不到系统级冲突）。
// [集成点] 本页加入 vcxproj（XAML 项 + 本 .cpp），include 路径需含
// windows/Core/include 与 windows/NativeShell/include。

#include "ShortcutsPage.h"

namespace winrt::TransFrame::UI::implementation {

using namespace winrt::Windows::UI::Xaml;
using namespace winrt::Windows::UI::Xaml::Controls;
using namespace winrt::Windows::System;

ShortcutsPage::ShortcutsPage()
    : viewModel_(std::make_unique<tf::ui::ShortcutsPageViewModel>()) {
    // [集成点] 若采用 XAML 编译器生成头（ShortcutsPage.g.h），
    // InitializeComponent() 由生成代码提供；当前手工声明版在此加载 XAML：
    //   winrt::Windows::UI::Xaml::Controls::UserControl 内容由
    //   Application::LoadComponent(this, Uri(L"ms-appx:///UI/ShortcutsPage/ShortcutsPage.xaml"))
    //   加载（集成时取消注释）。
    // InitializeComponent();
    // RefreshAllRows();  // InitializeComponent 之后刷新动态状态（x:Bind OneTime 初值之外）
}

// ---- x:Bind 目标实现 ----

winrt::hstring ShortcutsPage::ActionLabelFor(int row) const {
    return tf::ui::ActionLabelForRow(row);
}

winrt::hstring ShortcutsPage::ComboTextFor(int row) const {
    return winrt::to_hstring(viewModel_->ComboTextForRow(row));
}

winrt::hstring ShortcutsPage::RecordButtonTextFor(int row) const {
    return viewModel_->RecordingRow() == row ? winrt::hstring(L"录制中…")
                                             : winrt::hstring(L"点击录制");
}

// ---- 事件处理器 ----

void ShortcutsPage::OnKeyDown(winrt::Windows::Foundation::IInspectable const&,
                              winrt::Windows::UI::Xaml::Input::KeyRoutedEventArgs const& e) {
    if (!viewModel_->IsRecording()) return;

    // VirtualKey → Win32 VK：字母/数字/F 键区 VK 与 VirtualKey 枚举数值一致
    // （VirtualKey::A=0x41…Z, Number0=0x30…9, F1=0x70…F12=0x7B），直接取
    // static_cast<uint32_t>(e.Key()) 即为 Win32 VK 码。其余键位按需在此扩展映射。
    std::uint32_t vk = static_cast<std::uint32_t>(e.Key());

    // 修饰位收集：CoreTypes.hpp 语义 MOD_ALT=1 / MOD_CONTROL=2 / MOD_SHIFT=4 / MOD_WIN=8
    // （与 Win32 RegisterHotKey 的 MOD_* 值一致）。
    std::uint32_t mods = 0;
    // XAML KeyDown 的修饰状态由 KeyRoutedEventArgs.KeyModifiers()（VirtualKeyModifiers）给出。
    auto km = e.KeyModifiers();
    if ((km & VirtualKeyModifiers::Control) == VirtualKeyModifiers::Control) mods |= tf::ui::kModControl;
    if ((km & VirtualKeyModifiers::Menu)    == VirtualKeyModifiers::Menu)    mods |= tf::ui::kModAlt;
    if ((km & VirtualKeyModifiers::Shift)   == VirtualKeyModifiers::Shift)   mods |= tf::ui::kModShift;
    if ((km & VirtualKeyModifiers::Windows) == VirtualKeyModifiers::Windows) mods |= tf::ui::kModWin;

    // 纯修饰键按下本身不构成组合（等待非修饰键）。
    if (vk == 0x10 || vk == 0x11 || vk == 0x12 || vk == 0x5B || vk == 0x5C) return;

    auto result = viewModel_->HandleRecordInput(vk, mods);
    e.Handled(true);  // 录制期间吞掉按键（含 Esc），不向 shell 冒泡。

    if (!result.finished) {
        // 无修饰键：行内提示"需至少一个修饰键"，不退出录制。
        RefreshStatusText();
        return;  // 保持录制，继续捕获
    }
    if (result.cancelled) {
        RefreshAllRows();  // Esc 取消：退出录制态，恢复按钮文案
        RefreshStatusText();
        return;
    }
    // 合法组合：结束录制并写入 → 冲突检测 → 刷新 → 持久化。
    viewModel_->DetectConflicts();
    RefreshAllRows();
    RefreshStatusText();
    PersistBindings();
}

void ShortcutsPage::OnRecordButton0Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleRecordClick(0); }
void ShortcutsPage::OnRecordButton1Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleRecordClick(1); }
void ShortcutsPage::OnRecordButton2Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleRecordClick(2); }
void ShortcutsPage::OnRecordButton3Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleRecordClick(3); }
void ShortcutsPage::OnRecordButton4Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleRecordClick(4); }
void ShortcutsPage::OnRecordButton5Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleRecordClick(5); }

void ShortcutsPage::OnResetButton0Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleResetClick(0); }
void ShortcutsPage::OnResetButton1Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleResetClick(1); }
void ShortcutsPage::OnResetButton2Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleResetClick(2); }
void ShortcutsPage::OnResetButton3Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleResetClick(3); }
void ShortcutsPage::OnResetButton4Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleResetClick(4); }
void ShortcutsPage::OnResetButton5Click(winrt::Windows::Foundation::IInspectable const&, RoutedEventArgs const&) { HandleResetClick(5); }

// ---- 内部流程 ----

void ShortcutsPage::HandleRecordClick(int row) {
    // 同一时刻仅一行在录：点击新行时 VM 内部已切换单一 recordingRow。
    viewModel_->BeginRecording(row);
    // GrabFocus：拿键盘焦点以便接收 KeyDown（UserControl IsTabStop=True）。
    // [集成点] 若嵌入 hwnd 宿主且焦点在窗口外，需 SetFocus 到承载本页的
    // HWND（XamlIsland 场景），否则 KeyDown 收不到。
    Focus(winrt::Windows::UI::Xaml::FocusState::Programmatic);
    RefreshAllRows();
    RefreshStatusText();
}

void ShortcutsPage::HandleResetClick(int row) {
    viewModel_->ResetRow(row);
    viewModel_->DetectConflicts();
    RefreshAllRows();
    RefreshStatusText();
    PersistBindings();
}

void ShortcutsPage::RefreshRow(int row) {
    // 组合文本（录制动效：录制中该行显示"录制中…"提示由按钮文案 + 高亮体现）
    if (auto combo = ComboTextAt(row)) {
        combo.Text(winrt::to_hstring(viewModel_->ComboTextForRow(row)));
    }
    // 录制按钮文案 + 该行高亮
    if (auto btn = RecordButtonAt(row)) {
        bool recording = viewModel_->RecordingRow() == row;
        btn.Content(box_value(recording ? winrt::hstring(L"录制中…")
                                        : winrt::hstring(L"点击录制")));
    }
    // 冲突标红：红色前景 + 下方 ConflictMessage
    if (auto conflict = ConflictTextAt(row)) {
        if (viewModel_->RowHasConflict(row)) {
            conflict.Text(winrt::to_hstring(viewModel_->ConflictMessageForRow(row)));
            conflict.Visibility(Visibility::Visible);
        } else {
            conflict.Visibility(Visibility::Collapsed);
        }
    }
}

void ShortcutsPage::RefreshAllRows() {
    for (int i = 0; i < tf::ui::kShortcutRowCount; ++i) RefreshRow(i);
}

void ShortcutsPage::RefreshStatusText() {
    if (auto status = StatusTextControl()) {
        if (viewModel_->HasStatus()) {
            status.Text(viewModel_->StatusMessage());
            status.Visibility(Visibility::Visible);
        } else {
            status.Visibility(Visibility::Collapsed);
        }
    }
}

void ShortcutsPage::PersistBindings() {
    // [集成点] 持久化链路（shell 侧实现）：
    //   1. 读 %APPDATA%\TransFrame\settings.json 全文（IO 在调用方）；
    //   2. core::serializeHotkeyBindings(existingJson, viewModel_->BindingsForStore())
    //      合并写回（纯函数，保留未知键）；
    //   3. pickColor：store 目前只有 5 动作，pickColorShortcut 需扩展 store 或
    //      按 schema §1 键单独写：{ "TransFrame.pickColorShortcut":
    //      {"vk":…, "modifiers":…} }（PickColorBinding() 已给出该形状）；
    //   4. 通知 shell 调 HotkeyManager::reconfigure(specs)（HotkeySpecForRow 0-4），
    //      订阅其返回的 HotkeyRegistrationReport，把 report.conflicts（HotkeyConflict
    //      {action,vk,modifiers,message}）映射回对应行标红（注册期系统级冲突）。
    // 当前页面侧仅持有内存态；PersistBindings 是集成时注入 IO 的挂点。
}

}  // namespace winrt::TransFrame::UI::implementation
