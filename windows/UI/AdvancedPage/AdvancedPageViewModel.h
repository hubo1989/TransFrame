// windows/UI/AdvancedPage/AdvancedPageViewModel.h
// 高级页签 ViewModel —— 普通 C++ 类（非 WinRT runtime class），由页面 code-behind 持有。
// 包装 Core 的 tf::core::EngineConfig（windows/Core/include/tf/EngineConfigStore.hpp
// 122-128 行：strokeColor / strokeWidth / textSize），禁止重复定义字段。
// 持久化：通过 serializeEngineConfigPatch 纯函数模式合并写入 engines.json
// （%APPDATA%\TransFrame\engines.json）；strokeColor 对齐 mac CodableColor
// （{red,green,blue,opacity} JSON 对象，键 camelCase），strokeWidth / textSize 数值键。
// [集成点] include 路径需已含 windows/Core/include（工程已声明，构建时确认）。
// [集成点] 配置文件的读/写时机（何时加载、何时落盘）由 shell 侧决定：
//          shell 在页面创建前 parseEngineConfig 后把 EngineConfig 传入 VM 构造器。
// [集成点] 全部重置确认后需通知 shell 更新热键并重绘（mac: appDelegate.updateHotkeys()）。

#pragma once

#include "tf/EngineConfigStore.hpp"

#include <string>
#include <utility>
#include <vector>

namespace winrt::TransFrame::UI {

// ColorConfig ↔ Windows::UI::Color 转换器写在 code-behind
// （AdvancedPage.h 的静态函数），VM 保持平台干净，仅持有 Core 类型。

class AdvancedPageViewModel final {
public:
    // 预设色：spec §6 的 9 个（red/orange/yellow/green/blue/purple/pink/white/black）。
    // RGB 取合理值（对齐 mac 系统 .red/.orange/... 的 sRGB 近似）。
    struct PresetColor {
        const char* name;            // 标识符（英文，持久化不使用它）
        tf::core::ColorConfig color; // 插入顺序即 UI 顺序
    };

    static const std::vector<PresetColor>& presetColors() {
        static const std::vector<PresetColor> kPresets = {
            {"red",    {1.00, 0.23, 0.19, 1.0}},   // mac .red
            {"orange", {1.00, 0.58, 0.00, 1.0}},   // mac .orange
            {"yellow", {1.00, 0.80, 0.00, 1.0}},   // mac .yellow
            {"green",  {0.19, 0.82, 0.35, 1.0}},   // mac .green
            {"blue",   {0.04, 0.52, 1.00, 1.0}},   // mac .blue
            {"purple", {0.75, 0.35, 0.95, 1.0}},   // mac .purple
            {"pink",   {1.00, 0.22, 0.61, 1.0}},   // mac .pink
            {"white",  {1.00, 1.00, 1.00, 1.0}},   // mac .white
            {"black",  {0.00, 0.00, 0.00, 1.0}},   // mac .black
        };
        return kPresets;
    }

    // 默认描边色：mac 默认 .red（tf::core::ColorConfig::makeRed()）。
    static tf::core::ColorConfig defaultStrokeColor() {
        return tf::core::ColorConfig::makeRed();
    }
    static constexpr double kDefaultStrokeWidth = 2.0;  // mac: TransFrame.strokeWidth
    static constexpr double kDefaultTextSize    = 14.0; // mac: TransFrame.textSize

    explicit AdvancedPageViewModel(tf::core::EngineConfig config)
        : m_config(std::move(config)) {}

    // --- 只读访问（供 code-behind 呈现） ---
    const tf::core::EngineConfig& config() const { return m_config; }

    tf::core::ColorConfig strokeColor() const { return m_config.strokeColor; }
    double strokeWidth() const { return m_config.strokeWidth; }
    double textSize() const { return m_config.textSize; }

    // 选中态判断：与预设色的分量全等比较（Core 的 ColorConfig 有 operator==）。
    bool isColorSelected(const tf::core::ColorConfig& preset) const {
        return m_config.strokeColor == preset;
    }

    // --- 写入（返回 true 表示值变化；code-behind 决定是否落盘/重绘） ---
    bool setStrokeColor(const tf::core::ColorConfig& color) {
        if (m_config.strokeColor == color) return false;
        m_config.strokeColor = color;
        return true;
    }
    bool setStrokeWidth(double width) {
        if (width < 1.0) width = 1.0;
        if (width > 20.0) width = 20.0;
        if (m_config.strokeWidth == width) return false;
        m_config.strokeWidth = width;
        return true;
    }
    bool setTextSize(double size) {
        if (size < 8.0) size = 8.0;
        if (size > 72.0) size = 72.0;
        if (m_config.textSize == size) return false;
        m_config.textSize = size;
        return true;
    }

    // --- 全部重置（destructive；调用前 UI 必须 ContentDialog 二次确认） ---
    // 只恢复本页三个字段，其余 EngineConfig 字段不动（mac: settings.resetToDefaults()
    // 语义在 mac 上走 AppSettings 默认值；此处按 spec §6 “清空全部上述键”仅限本页键）。
    void resetToDefaults() {
        m_config.strokeColor = defaultStrokeColor();
        m_config.strokeWidth = kDefaultStrokeWidth;
        m_config.textSize    = kDefaultTextSize;
    }

    // --- 持久化序列化 ---
    // 产出只含本页字段的 patch，交给 serializeEngineConfigPatch 与现有 engines.json
    // 合并（保留其它分支的未知键）。序列化格式对齐 Core 纯函数模式：
    // strokeColor → {"red":…,"green":…,"blue":…,"opacity":…}（对齐 mac CodableColor）
    // strokeWidth / textSize → 数值（JSON 键 camelCase，与 EngineConfigStore 注释一致）。
    tf::core::EngineConfig persistencePatch() const {
        tf::core::EngineConfig patch{};
        patch.strokeColor = m_config.strokeColor;
        patch.strokeWidth = m_config.strokeWidth;
        patch.textSize    = m_config.textSize;
        return patch;
    }

    // 生成 patch JSON 文本（等价于 serializeEngineConfigPatch("", persistencePatch())
    // 的本页子集，供调用方合并；实际落盘仍应走 serializeEngineConfigPatch(existing, patch)
    // 以保留未知键）。
    std::string serializePatchInto(const std::string& existingJsonUtf8) const {
        return tf::core::serializeEngineConfigPatch(existingJsonUtf8, persistencePatch());
    }

private:
    tf::core::EngineConfig m_config;
};

} // namespace winrt::TransFrame::UI
