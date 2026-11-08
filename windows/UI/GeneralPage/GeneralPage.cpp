// windows/UI/GeneralPage/GeneralPage.cpp
// FR-35 通用页签 code-behind 实现（纯代码构建；卡片清单见 .h 文件头）。
#include "GeneralPage.h"
#include "SettingsFileStore.hpp"
#include "SettingsUiKit.hpp"

#include <initguid.h>  // DEFINE_GUID 就地展开（CLSID_FileOpenDialog / FOLDERID_*）
#include <windows.h>
#include <shlobj.h>
#include <shobjidl_core.h>
#include <knownfolders.h>  // FOLDERID_Desktop（WIN32_LEAN_AND_MEAN 下不随 shlobj 进来）
#include <shellapi.h>

#include <filesystem>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace tf::ui {
namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
namespace Controls = winrt::Microsoft::UI::Xaml::Controls;

// ---- HKCU Run 键（开机自启：注册表即持久化，无 JSON 键）----
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"TransFrame";

bool LaunchAtLoginRegistered() {
    HKEY key = nullptr;
    if (::RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS) {
        return false;
    }
    const LSTATUS status =
        ::RegQueryValueExW(key, kRunValueName, nullptr, nullptr, nullptr, nullptr);
    ::RegCloseKey(key);
    return status == ERROR_SUCCESS;
}

void SetLaunchAtLogin(bool on) {
    HKEY key = nullptr;
    if (::RegCreateKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, nullptr, 0, KEY_SET_VALUE,
                          nullptr, &key, nullptr) != ERROR_SUCCESS) {
        return;
    }
    if (on) {
        wchar_t path[1024]{};
        if (::GetModuleFileNameW(nullptr, path, 1024) > 0) {
            const std::wstring quoted = L"\"" + std::wstring(path) + L"\"";
            ::RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(quoted.c_str()),
                             static_cast<DWORD>((quoted.size() + 1) * sizeof(wchar_t)));
        }
    } else {
        ::RegDeleteValueW(key, kRunValueName);
    }
    ::RegCloseKey(key);
}

// ---- 更新检查偏好（settings.json，PRD FR-60 .win 命名空间键）[报告项] ----
constexpr const char* kCheckUpdatesKey = "TransFrame.checkForUpdates.win";

bool ReadCheckForUpdates() {
    auto v = store::SettingsKey(kCheckUpdatesKey);
    if (v && v->is_boolean()) return v->get<bool>();
    return true;  // 默认开
}

void WriteCheckForUpdates(bool on) {
    store::WriteSettingsJsonAtomic(
        store::MergeSettingsKey(store::ReadSettingsJson(), kCheckUpdatesKey, on));
}

/// ComboBoxItem.Tag 里存的 raw 值。
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
    combo.SelectedIndex(0);  // 未知持久化值的保守回退（首项）
}

}  // namespace

GeneralPage::GeneralPage() {
    GeneralPageViewModel::Persistence persistence;
    persistence.loadEnginesJson = [] { return store::ReadEnginesJson(); };
    persistence.saveEnginesJson = [](const std::string& json) {
        store::WriteEnginesJsonAtomic(json);
    };
    vm_ = GeneralPageViewModel(std::move(persistence));
    vm_.SetLaunchAtLogin(LaunchAtLoginRegistered());
    vm_.SetCheckForUpdates(ReadCheckForUpdates());
    BuildUi();
    SyncFromViewModel();
}

void GeneralPage::OnShown() {
    vm_.Load();
    vm_.SetLaunchAtLogin(LaunchAtLoginRegistered());
    vm_.SetCheckForUpdates(ReadCheckForUpdates());
    SyncFromViewModel();
}

void GeneralPage::BuildUi() {
    Controls::StackPanel root = kit::MakePageRoot(*this);

    // ---------------- 卡片 1：保存位置（spec §1.2） ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("settings.save.location", L"保存位置").c_str(),
            kit::Lx("settings.save.location.message", L"截图与导出文件的默认目录").c_str()));

        savePathText_ = Controls::TextBlock();
        savePathText_.TextWrapping(TextWrapping::NoWrap);
        savePathText_.TextTrimming(TextTrimming::CharacterEllipsis);
        savePathText_.FontSize(13.0);
        savePathText_.Opacity(0.8);
        card.panel.Children().Append(savePathText_);

        Controls::StackPanel buttons;
        buttons.Orientation(Controls::Orientation::Horizontal);
        buttons.Spacing(8.0);
        Controls::Button pick;
        pick.Content(winrt::box_value(kit::Lx("settings.save.location.choose", L"选择…")));
        pick.Click([this](auto&&, auto&&) { PickSaveFolder(); });
        Controls::Button reveal;
        reveal.Content(winrt::box_value(kit::Lx("win.settings.save.reveal", L"在资源管理器中显示")));
        reveal.Click([this](auto&&, auto&&) { RevealSaveLocation(); });
        buttons.Children().Append(pick);
        buttons.Children().Append(reveal);
        card.panel.Children().Append(buttons);

        root.Children().Append(
            kit::MakeSectionHeader(kit::Lx("settings.section.general", L"通用").c_str()));
        root.Children().Append(card.border);
    }

    // ---------------- 卡片 2：导出格式与质量 ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("win.settings.export.title", L"导出格式与质量").c_str()));

        formatCombo_ = Controls::ComboBox();
        formatCombo_.Header(winrt::box_value(kit::Lx("settings.format", L"默认格式")));
        formatCombo_.MinWidth(200.0);
        for (const auto& [raw, key, fallback] :
             {std::tuple{"png", "settings.format.png", L"PNG"},
              std::tuple{"jpeg", "settings.format.jpeg", L"JPEG"},
              std::tuple{"heic", "settings.format.heic", L"HEIC"}}) {
            Controls::ComboBoxItem item;
            item.Content(winrt::box_value(kit::Lx(key, fallback)));
            item.Tag(winrt::box_value(winrt::to_hstring(raw)));
            formatCombo_.Items().Append(item);
        }
        formatCombo_.SelectionChanged([this](auto&&, auto&&) {
            if (updating_) return;
            vm_.SetDefaultFormat(TagOf(formatCombo_.SelectedItem()));
            SyncFromViewModel();  // 质量行可见性/取值随格式变化
            PersistWithStatus();
        });
        card.panel.Children().Append(formatCombo_);

        Controls::StackPanel qualityRow;
        qualityRow.Orientation(Controls::Orientation::Horizontal);
        qualityRow.Spacing(12.0);
        qualitySlider_ = Controls::Slider();
        qualitySlider_.Header(winrt::box_value(kit::Lx("win.settings.export.quality", L"导出质量")));
        qualitySlider_.Minimum(10.0);
        qualitySlider_.Maximum(100.0);
        qualitySlider_.StepFrequency(5.0);
        qualitySlider_.Width(260.0);
        qualityValueText_ = Controls::TextBlock();
        qualityValueText_.VerticalAlignment(VerticalAlignment::Center);
        qualityValueText_.MinWidth(48.0);
        qualityRow.Children().Append(qualitySlider_);
        qualityRow.Children().Append(qualityValueText_);
        qualitySlider_.ValueChanged([this](auto&&, auto&& args) {
            if (updating_) return;
            vm_.SetActiveQuality(args.NewValue() / 100.0);
            wchar_t buf[16]{};
            swprintf(buf, 16, L"%d%%", static_cast<int>(args.NewValue() + 0.5));
            qualityValueText_.Text(buf);
        });
        // 拖动结束才落盘（Slider 拖动中捕获指针，释放即 PointerCaptureLost；
        // 避免每 tick 一次文件写——mac UserDefaults 写内存无此顾虑）。
        qualitySlider_.PointerCaptureLost([this](auto&&, auto&&) { PersistWithStatus(); });
        card.panel.Children().Append(qualityRow);

        root.Children().Append(card.border);
    }

    // ---------------- 卡片 3：界面语言（spec §1.2；FR-37 即时生效） ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("settings.language", L"界面语言").c_str(),
            kit::Lx("win.settings.language.hint", L"设置窗口与托盘菜单的显示语言").c_str()));
        languageCombo_ = Controls::ComboBox();
        languageCombo_.MinWidth(200.0);
        for (const auto& opt : GeneralPageViewModel::AppLanguageOptions()) {
            const std::wstring display =
                opt.raw == "system"
                    ? std::wstring(kit::Lx("settings.language.system", L"跟随系统").c_str())
                    : opt.displayName;
            Controls::ComboBoxItem item;
            item.Content(winrt::box_value(display));
            item.Tag(winrt::box_value(winrt::to_hstring(opt.raw)));
            languageCombo_.Items().Append(item);
        }
        languageCombo_.SelectionChanged([this](auto&&, auto&&) {
            if (updating_) return;
            vm_.SetAppLanguage(TagOf(languageCombo_.SelectedItem()));
            PersistWithStatus();
            // FR-37 即时生效（与语言页共用 engines.json appLanguage 键）。
            core::loc::Localization::instance().setLanguage(vm_.AppLanguage());
        });
        card.panel.Children().Append(languageCombo_);
        root.Children().Append(card.border);
    }

    // ---------------- 卡片 4：启动与更新 ----------------
    {
        kit::Card card = kit::MakeCard();
        card.panel.Children().Append(kit::MakeLabelBlock(
            kit::Lx("win.settings.startupUpdates.title", L"启动与更新").c_str()));

        launchAtLoginToggle_ = Controls::ToggleSwitch();
        launchAtLoginToggle_.Header(
            winrt::box_value(kit::Lx("win.settings.autostart", L"开机自启")));
        launchAtLoginToggle_.OnContent(winrt::box_value(kit::Lx("win.common.on", L"开")));
        launchAtLoginToggle_.OffContent(winrt::box_value(kit::Lx("win.common.off", L"关")));
        launchAtLoginToggle_.Toggled([this](auto&&, auto&&) {
            if (updating_) return;
            const bool on = launchAtLoginToggle_.IsOn();
            SetLaunchAtLogin(on);
            vm_.SetLaunchAtLogin(LaunchAtLoginRegistered());
            statusText_.Text(on
                                 ? kit::Lx("win.settings.autostart.added", L"已写入开机自启（HKCU Run）")
                                 : kit::Lx("win.settings.autostart.removed", L"已移除开机自启"));
        });
        card.panel.Children().Append(launchAtLoginToggle_);

        checkUpdatesToggle_ = Controls::ToggleSwitch();
        checkUpdatesToggle_.Header(
            winrt::box_value(kit::Lx("win.settings.checkUpdates", L"更新检查")));
        checkUpdatesToggle_.OnContent(winrt::box_value(kit::Lx("win.common.on", L"开")));
        checkUpdatesToggle_.OffContent(winrt::box_value(kit::Lx("win.common.off", L"关")));
        checkUpdatesToggle_.Toggled([this](auto&&, auto&&) {
            if (updating_) return;
            WriteCheckForUpdates(checkUpdatesToggle_.IsOn());
            statusText_.Text(kit::Lx("win.common.saved", L"已保存"));
        });
        card.panel.Children().Append(checkUpdatesToggle_);

        root.Children().Append(card.border);
    }

    statusText_ = Controls::TextBlock();
    statusText_.FontSize(12.0);
    statusText_.Opacity(0.7);
    root.Children().Append(statusText_);
}

void GeneralPage::SyncFromViewModel() {
    updating_ = true;

    // 保存位置：空串 = 平台默认（桌面），解析为实际路径显示。
    std::wstring display;
    if (vm_.HasCustomSaveLocation()) {
        display = kit::ToWide(vm_.SaveLocation());
    } else {
        wchar_t* desktop = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) && desktop) {
            display = desktop;
            ::CoTaskMemFree(desktop);
        } else {
            display = kit::Lx("win.settings.save.desktop", L"桌面");
        }
    }
    savePathText_.Text(display);

    SelectComboByTag(formatCombo_, vm_.DefaultFormat());
    qualitySlider_.Visibility(vm_.QualityVisible() ? Visibility::Visible
                                                   : Visibility::Collapsed);
    qualityValueText_.Visibility(qualitySlider_.Visibility());
    qualitySlider_.Value(vm_.ActiveQuality() * 100.0);
    wchar_t buf[16]{};
    swprintf(buf, 16, L"%d%%", static_cast<int>(vm_.ActiveQuality() * 100.0 + 0.5));
    qualityValueText_.Text(buf);

    SelectComboByTag(languageCombo_, vm_.AppLanguage());
    launchAtLoginToggle_.IsOn(vm_.LaunchAtLogin());
    checkUpdatesToggle_.IsOn(vm_.CheckForUpdates());
    updating_ = false;
}

void GeneralPage::PersistWithStatus() {
    if (vm_.Save()) {
        statusText_.Text(kit::Lx("win.common.saved", L"已保存"));
    } else {
        statusText_.Text(kit::Lx("win.common.saveFailedEngines", L"保存失败（engines.json 不可写）"));
    }
}

void GeneralPage::PickSaveFolder() {
    IFileOpenDialog* dialog = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&dialog)))) {
        return;
    }
    DWORD options = 0;
    dialog->GetOptions(&options);
    dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    HWND owner = ::GetActiveWindow();
    if (SUCCEEDED(dialog->Show(owner))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item)) && item) {
            PWSTR path = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path)) && path) {
                vm_.SetSaveLocation(winrt::to_string(path));
                ::CoTaskMemFree(path);
                PersistWithStatus();
                SyncFromViewModel();
            }
            item->Release();
        }
    }
    dialog->Release();
}

void GeneralPage::RevealSaveLocation() {
    std::filesystem::path dir;
    if (vm_.HasCustomSaveLocation()) {
        dir = kit::ToWide(vm_.SaveLocation());
    } else {
        wchar_t* desktop = nullptr;
        if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktop)) &&
            desktop) {
            dir = desktop;
            ::CoTaskMemFree(desktop);
        }
    }
    if (dir.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);  // 自定义目录可能尚未创建
    ::ShellExecuteW(nullptr, L"open", dir.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace tf::ui
