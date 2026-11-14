// windows/UI/GeneralPage/GeneralPageViewModel.hpp
// FR-35 通用页签 ViewModel —— 纯 C++ / WinRT-free（可进 Catch2 单测）。
//
// 字段对照（spec §1.2 + PRD FR-35 通用页签）：
//   * 保存位置 saveLocation      —— Core EngineConfig 已收录（键 saveLocation）
//   * 导出格式 defaultFormat     —— Core 已收录（png/jpeg/heic）
//   * 导出质量 jpeg/heicQuality  —— Core 已收录（0.1–1.0，随格式路由）
//   * 界面语言 appLanguage       —— schema §2.1 已定义、Core EngineConfigStore
//     未收录 [报告项]：VM 以 engines.json 顶层 "appLanguage" 键读写，Core
//     的 parse 容忍未知键、serialize 以既有文本为合并基，键可存活。
//   * 开机自启 / 更新检查 —— 非 Core 字段：前者由页面直连 HKCU Run 键
//     （注册表即持久化，无 JSON 键），后者由页面写 settings.json 的
//     "TransFrame.checkForUpdates.win"（PRD FR-60 .win 命名空间）[报告项]。
//     VM 只持bool镜像供 UI 显示，两键的读写均在页面层。
//
// 持久化协议与 EnginesPageViewModel 一致：显式 Save()（mac 无“即存即落”
// 假设的同一结论）；闭包注入，纯内存模式下页面可用、不落盘。
#ifndef TF_UI_GENERAL_PAGE_VIEW_MODEL_HPP
#define TF_UI_GENERAL_PAGE_VIEW_MODEL_HPP

#include "tf/EngineConfigStore.hpp"
#include "tf/Json.hpp"

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tf::ui {

/// 界面语言下拉项（raw = schema §2.1 appLanguage 取值）。
struct AppLanguageOption {
    std::string raw;
    std::wstring displayName;
};

class GeneralPageViewModel {
public:
    struct Persistence {
        std::function<std::string()> loadEnginesJson;
        std::function<void(const std::string&)> saveEnginesJson;
    };

    explicit GeneralPageViewModel(Persistence persistence = {});

    // —— 保存位置（空串 = 平台默认[桌面]；页面层解析显示路径）——
    std::string SaveLocation() const { return saveLocation_; }
    void SetSaveLocation(std::string_view path);
    bool HasCustomSaveLocation() const { return !saveLocation_.empty(); }

    // —— 导出格式 / 质量 ——
    std::string DefaultFormat() const { return defaultFormat_; }
    bool IsValidFormat(std::string_view v) const;
    void SetDefaultFormat(std::string_view v);          // 非法值拒绝
    static constexpr double kQualityMin = 0.1;
    static constexpr double kQualityMax = 1.0;
    double ActiveQuality() const;                        // png → 0（无效占位）
    void SetActiveQuality(double q);                     // 随格式路由 + clamp
    bool QualityVisible() const { return defaultFormat_ != "png"; }
    double EffectiveJpegQuality() const { return jpegQuality_; }
    double EffectiveHeicQuality() const { return heicQuality_; }

    // —— 界面语言 ——
    std::string AppLanguage() const { return appLanguage_; }
    void SetAppLanguage(std::string_view raw);           // 非法 raw 拒绝
    static const std::vector<AppLanguageOption>& AppLanguageOptions();

    // —— 开机自启 / 更新检查（页面层落 HKCU Run / settings.json，VM 仅镜像）——
    bool LaunchAtLogin() const { return launchAtLogin_; }
    void SetLaunchAtLogin(bool v) { launchAtLogin_ = v; }
    bool CheckForUpdates() const { return checkForUpdates_; }
    void SetCheckForUpdates(bool v) { checkForUpdates_ = v; }

    // —— 持久化 ——
    /// 只含本页 store 字段的 patch（saveLocation/defaultFormat/双质量）。
    tf::core::EngineConfig Patch() const;
    /// 从 load 闭包重载（构造时已调；页面 OnShown 复用）。
    void Load();
    /// patch 合并进 engines.json + appLanguage 顶层键 → save 闭包。
    /// 纯内存模式（无 save 闭包）返回 false。
    bool Save();

private:
    std::string saveLocation_;
    std::string defaultFormat_ = "png";
    double jpegQuality_ = 0.9;
    double heicQuality_ = 0.9;
    std::string appLanguage_ = "system";
    bool launchAtLogin_ = false;
    bool checkForUpdates_ = true;
    Persistence persistence_;
};

}  // namespace tf::ui

#endif  // TF_UI_GENERAL_PAGE_VIEW_MODEL_HPP
