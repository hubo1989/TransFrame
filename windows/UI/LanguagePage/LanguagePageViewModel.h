#pragma once
// LanguagePageViewModel.h — 语言页签 ViewModel（普通 C++ 类，非 WinRT runtime class）。
//
// 数据模型全部包装 Core 现有类型，不重复定义语言枚举或字段：
//   - 全局翻译对  ↔ tf::core::EngineConfig::sourceLang / targetLang
//     （持久化键 TransFrame.translationSourceLanguage / translationTargetLanguage）
//   - 翻译插入对  ↔ tf::core::EngineConfig::translateAndInsert.sourceLang / .targetLang
//     （见 tf/EngineConfigStore.hpp 85-92 行；键 TransFrame.translateAndInsert*）
//   本 VM 只额外维护一份 rawValue→显示名 常量表（中文文案硬编码，对齐
//   mac zh-Hans.lproj；特例：auto → “自动检测”、nil → “跟随系统”）。
//
// 序列化语义（spec §4 / settings-config-schema.md §2.2）：
//   目标语言选“跟随系统”（nil）时，Core 侧以空串 targetLang 表示
//   （EngineConfigStore.hpp：Empty targetLang = "follow system"）。
//   serializeEngineConfigPatch 落盘为空串键值（put(...)，非删键），
//   读取时空串/缺键均回退默认——语义等价 mac 的“nil 时移除键”。
//   本 VM 不直接落盘。
//
// [待确认] TransFrame.translationAutoDetect（Core: EngineConfig::autoDetect，
//   默认 true）：spec §4 标注当前设置 UI 是否暴露该开关未定 —— 本页不渲染该
//   开关，本 VM 不提供修改入口，Core 字段保持原值不动。
//
// [集成点] #include "tf/EngineConfigStore.hpp" 依赖工程 include 路径已含
//   windows/Core/include（wt-cfgmodel 已确认）；本文件随页面目录加入 vcxproj。
// [集成点] 配置文件读写时机由宿主决定：读取 = 页面构造/AttachEngineConfig 时
//   传入已加载的 EngineConfig；写回 = 值变化后由宿主调用 tf::core 的
//   writeEngineConfigFileAtomic 并通知翻译管线重载语言（对应 mac 的引擎刷新）。

#include "tf/EngineConfigStore.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace tf::ui {

// 单个语言选项：rawValue（持久化值）+ 显示名。
struct LanguageOption {
    std::string raw;
    std::wstring displayName;
};

class LanguagePageViewModel {
public:
    // 两个特殊 raw 值：auto 仅对源语言合法；空串 = 跟随系统（nil），仅目标语言首项。
    static constexpr char const* kAutoRawValue = "auto";
    static constexpr char const* kFollowSystemRawValue = "";

    // 绑定宿主（设置窗口）持有的配置对象；VM 不拥有，宿主生命周期须覆盖页面。
    void Attach(tf::core::EngineConfig& config) { m_config = &config; }
    bool IsAttached() const { return m_config != nullptr; }

    // —— 选项列表（函数内静态表：源 = 自动检测(auto) + 语言全集，共 30 项；
    //    目标 = 跟随系统(空串) + 非 auto 语言，共 30 项；顺序与常量表一致，
    //    下标即 ComboBox 顺序）——
    std::vector<LanguageOption> const& SourceLanguageOptions() const {
        static std::vector<LanguageOption> const kOptions = BuildSourceOptions();
        return kOptions;
    }
    std::vector<LanguageOption> const& TargetLanguageOptions() const {
        static std::vector<LanguageOption> const kOptions = BuildTargetOptions();
        return kOptions;
    }

    // —— 全局翻译对（EngineConfig::sourceLang / targetLang）——
    std::string GlobalSourceLanguage() const {
        // 未 Attach 时取 schema 默认 auto
        return m_config ? m_config->sourceLang : std::string(kAutoRawValue);
    }
    std::string GlobalTargetLanguage() const {
        // 空串 = 跟随系统（nil，schema 默认）
        return m_config ? m_config->targetLang : std::string{};
    }
    void SetGlobalSourceLanguage(std::string const& raw) {
        // 防御：未收录的值不落库（写盘/通知时机见文件头 [集成点]）
        if (!m_config || IndexOfSourceOption(raw) < 0) return;
        m_config->sourceLang = raw;
    }
    // 传 kFollowSystemRawValue（空串）= 跟随系统：Core 以空串表示 nil，
    // 序列化时按“移除 TransFrame.translationTargetLanguage 键”落盘（见文件头）。
    void SetGlobalTargetLanguage(std::string const& raw) {
        if (!m_config || IndexOfTargetOption(raw) < 0) return;
        m_config->targetLang = raw;
    }

    // —— 翻译插入对（EngineConfig::translateAndInsert.sourceLang / .targetLang）——
    std::string InsertSourceLanguage() const {
        return m_config ? m_config->translateAndInsert.sourceLang : std::string(kAutoRawValue);
    }
    std::string InsertTargetLanguage() const {
        // 空串 = 跟随系统（nil，schema 默认）；移除键语义同上
        return m_config ? m_config->translateAndInsert.targetLang : std::string{};
    }
    void SetInsertSourceLanguage(std::string const& raw) {
        if (!m_config || IndexOfSourceOption(raw) < 0) return;
        m_config->translateAndInsert.sourceLang = raw;
    }
    void SetInsertTargetLanguage(std::string const& raw) {
        if (!m_config || IndexOfTargetOption(raw) < 0) return;
        m_config->translateAndInsert.targetLang = raw;
    }

    // —— 查找（code-behind 回填初始选中用）——
    // 未收录返回 -1：持久化出现未知值时的保守选择 —— 源回“自动检测”(下标 0)、
    // 目标回“跟随系统”(下标 0) 显示；用户未改动选择前，原值保留在 EngineConfig
    // 中不丢失（不擅自改写库里的未知值）。
    int IndexOfSourceOption(std::string const& raw) const {
        return IndexOf(SourceLanguageOptions(), raw);
    }
    int IndexOfTargetOption(std::string const& raw) const {
        return IndexOf(TargetLanguageOptions(), raw);
    }

    // raw → 显示名；空串 → “跟随系统”；未知值保守显示 raw 本身（raw 均为 ASCII）。
    static std::wstring DisplayNameForRaw(std::string const& raw) {
        if (raw.empty()) return L"跟随系统";
        for (auto const& entry : LanguageNameTable()) {
            if (raw == entry.first) return entry.second;
        }
        return std::wstring(raw.begin(), raw.end());
    }

private:
    // 语言全集常量表：30 项 rawValue 与 spec §4 一字不差、顺序一致
    // （auto,en,zh-Hans,...,ro）。显示名固定为中文（mac 用 locale 相关
    // localizedName，中文系统显示中文名；WinUI 侧按 zh-Hans 文案硬编码，
    // 避免依赖系统语言）。特例：auto → “自动检测”。
    // [集成点] 若需跟随系统语言，可在运行时用
    //   Windows::Globalization::Language(to_hstring(raw)).DisplayName()
    // 替换本表（BCP-47 tag 直接可用），auto/跟随系统 两个特例仍走本表。
    static std::vector<std::pair<std::string, std::wstring>> const& LanguageNameTable() {
        static std::vector<std::pair<std::string, std::wstring>> const kTable = {
            {"auto", L"自动检测"},
            {"en", L"英语"},
            {"zh-Hans", L"简体中文"},
            {"zh-Hant", L"繁体中文"},
            {"ja", L"日语"},
            {"ko", L"韩语"},
            {"fr", L"法语"},
            {"de", L"德语"},
            {"es", L"西班牙语"},
            {"it", L"意大利语"},
            {"pt", L"葡萄牙语"},
            {"ru", L"俄语"},
            {"ar", L"阿拉伯语"},
            {"hi", L"印地语"},
            {"th", L"泰语"},
            {"vi", L"越南语"},
            {"nl", L"荷兰语"},
            {"pl", L"波兰语"},
            {"tr", L"土耳其语"},
            {"uk", L"乌克兰语"},
            {"cs", L"捷克语"},
            {"sv", L"瑞典语"},
            {"da", L"丹麦语"},
            {"fi", L"芬兰语"},
            {"no", L"挪威语"},
            {"el", L"希腊语"},
            {"he", L"希伯来语"},
            {"id", L"印尼语"},
            {"ms", L"马来语"},
            {"ro", L"罗马尼亚语"},
        };
        return kTable;
    }

    static std::vector<LanguageOption> BuildSourceOptions() {
        std::vector<LanguageOption> options;
        for (auto const& entry : LanguageNameTable()) {
            options.push_back({entry.first, entry.second});  // 全表，auto 在首
        }
        return options;
    }

    static std::vector<LanguageOption> BuildTargetOptions() {
        std::vector<LanguageOption> options;
        options.push_back({std::string(kFollowSystemRawValue), L"跟随系统"});  // 首项 = nil
        for (auto const& entry : LanguageNameTable()) {
            if (entry.first == kAutoRawValue) continue;  // 目标语言不含 auto
            options.push_back({entry.first, entry.second});
        }
        return options;
    }

    static int IndexOf(std::vector<LanguageOption> const& options, std::string const& raw) {
        for (std::size_t i = 0; i < options.size(); ++i) {
            if (options[i].raw == raw) return static_cast<int>(i);
        }
        return -1;
    }

    tf::core::EngineConfig* m_config = nullptr;
};

}  // namespace tf::ui
