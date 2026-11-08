// tf/ShortcutsPageViewModel.h — 快捷键页签 VM（普通 C++ 类，非 WinRT runtime class）
// 由 ShortcutsPage code-behind 持有（std::unique_ptr），页面事件驱动同步 UI。
//
// 数据模型全部包装 Core 现有类型，禁止重复定义字段：
//   - tf::core::HotkeyBinding（HotkeySettingsStore.hpp）：持久化形状 {actionName,vk,modifiers,enabled}
//   - tf::HotkeySpec / tf::HotkeyConflict / tf::HotkeyRegistrationReport（HotkeyManager.hpp）：
//     注册期冲突报告结构，本 VM 的页内冲突检测与之对齐。
//
// [集成点] 工程需将 windows/Core/include 加入 include 路径（已含），
//           并将 windows/NativeShell/include 加入 UI 工程 include 路径
//           （HotkeyManager.hpp / CoreTypes.hpp 在该目录），或将 HotkeyManager.hpp
//           迁至 Core include。当前按 #include "tf/..." 直接引用。
//
// [集成点] pickColor 动作：HotkeySettingsStore 目前只有 5 个动作（kHotkeyActionNames），
//           pickColorShortcut 需扩展 store（加 "pickColor" + TransFrame.pickColorShortcut 键），
//           或由 shell 层按 settings-config-schema.md §1 的 pickColorShortcut 键单独读写。
//           当前 VM 采用保守选择：pickColor 行在 bindings() 中单独返回，不进 5 动作 store 数组。
#ifndef TF_SHORTCUTS_PAGE_VIEW_MODEL_HPP
#define TF_SHORTCUTS_PAGE_VIEW_MODEL_HPP

#include "tf/HotkeySettingsStore.hpp"   // tf::core::HotkeyBinding, kHotkeyActionNames

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

// [集成点] HotkeyManager.hpp 依赖 NativeShell include 路径（"CoreTypes.hpp" 相对包含）。
#include "tf/HotkeyManager.hpp"         // tf::HotkeySpec / HotkeyConflict / HotkeyRegistrationReport

namespace tf::ui {

/// 页内六行的行号与动作（顺序 = XAML 行序，前五个与 kHotkeyActionNames 枚举序对齐）。
enum class ShortcutRow : int {
    FullScreen = 0,      // kHotkeyActionNames[0] "fullScreen"    TransFrame.fullScreenShortcut
    Region = 1,          // kHotkeyActionNames[1] "region"        TransFrame.selectionShortcut
    TranslateMode = 2,   // kHotkeyActionNames[2] "translate"     TransFrame.translationModeShortcut
    TextSelection = 3,   // kHotkeyActionNames[3] "textSelection" TransFrame.textSelectionTranslationShortcut
    TranslateInsert = 4, // kHotkeyActionNames[4] "translateInsert" TransFrame.translateAndInsertShortcut
    PickColor = 5,       // [集成点] store 目前无此动作，见文件头注释
};
constexpr int kShortcutRowCount = 6;

/// 页内冲突行检测结果（与 tf::HotkeyConflict 字段语义对齐：vk+modifiers+message）。
struct ShortcutConflictRow {
    int row = -1;               // 冲突行号（双方都返回一条）
    std::uint32_t vk = 0;
    std::uint32_t modifiers = 0;
    std::string message;        // 冲突文案（UI 直接显示）
};

/// Win32 MOD_* 修饰位（与 CoreTypes.hpp ModBits 一致：1=Alt 2=Ctrl 4=Shift 8=Win）。
constexpr std::uint32_t kModAlt     = 0x1;
constexpr std::uint32_t kModControl = 0x2;
constexpr std::uint32_t kModShift   = 0x4;
constexpr std::uint32_t kModWin     = 0x8;
constexpr std::uint32_t kModAll     = kModAlt | kModControl | kModShift | kModWin;

/// VK_F1/VK_F12 兜底（CoreTypes.hpp 未导出时的本地常量；值与 Win32 VK 定义一致）。
#ifndef VK_F1
#define VK_F1  0x70
#endif
#ifndef VK_F12
#define VK_F12 0x7B
#endif

/// vk+modifiers -> 本地化显示串，如 (0x41, Ctrl|Alt) -> "Ctrl+Alt+A"。
/// 修饰键顺序固定 Ctrl / Alt / Shift / Win（Win 修饰显示为 "Win"）。
inline std::string FormatCombo(std::uint32_t vk, std::uint32_t modifiers) {
    std::string s;
    if (modifiers & kModControl) s += "Ctrl+";
    if (modifiers & kModAlt)     s += "Alt+";
    if (modifiers & kModShift)   s += "Shift+";
    if (modifiers & kModWin)     s += "Win+";
    // 字母/数字键用 'A'..'Z' / '0'..'9'（VK 0x30-0x39, 0x41-0x5A 与 ASCII 同值）。
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) {
        s += static_cast<char>(vk);
    } else if (vk >= VK_F1 && vk <= VK_F12) {   // VK 0x70-0x7B → F1..F12
        s += "F" + std::to_string(vk - VK_F1 + 1);
    } else {
        s += "VK 0x" + [&] {
            char buf[8]; snprintf(buf, sizeof(buf), "%02X", vk); return std::string(buf);
        }();
    }
    return s;
}

/// 行 → UI 动作名（中文文案；与 SettingsWindow.cpp 现有文案风格一致）。
inline const wchar_t* ActionLabelForRow(int row) {
    switch (static_cast<ShortcutRow>(row)) {
        case ShortcutRow::FullScreen:      return L"全屏截图";
        case ShortcutRow::Region:          return L"区域截图";
        case ShortcutRow::TranslateMode:   return L"翻译模式";
        case ShortcutRow::TextSelection:   return L"划词翻译";
        case ShortcutRow::TranslateInsert: return L"翻译插入";
        case ShortcutRow::PickColor:       return L"取色";
    }
    return L"";
}

/// VK_F1/VK_F12 兜底（HotkeyManager.hpp / CoreTypes.hpp 未导出时的本地常量；
//  值与 Win32 VK 定义一致）。
#ifndef VK_F1
#define VK_F1  0x70
#endif
#ifndef VK_F12
#define VK_F12 0x7B
#endif

/// 录制状态机 + 六行绑定数据 + 冲突检测。
/// 单一 recordingRow 状态：-1 = 未录制；同一时刻仅一行在录。
class ShortcutsPageViewModel {
public:
    ShortcutsPageViewModel() { LoadDefaults(); }

    // ---- 默认值（Windows 侧注册默认；参考 windows/README.md 用户流程段）----
    // 注：schema 表格中 mac 默认（⌘⇧3 等）与 windows 建议列（Ctrl+Shift+3）仅作
    // schema parity 文档用途；README 实际用户流程统一用 Ctrl+Alt+* 家族，
    // 本 VM 采用 README 家族：F/A/D/I + T(翻译模式) + C(取色)。
    // mac 默认（⌘⇧3/⌘⇧4/⌘⇧T/⌘⇧Y/⌘⇧I/⌃⌘C）不用于 Windows 注册。
    void LoadDefaults() {
        SetBinding(0, 0x46, kModControl | kModAlt);  // 全屏截图 Ctrl+Alt+F
        SetBinding(1, 0x41, kModControl | kModAlt);  // 区域截图 Ctrl+Alt+A
        SetBinding(2, 0x54, kModControl | kModAlt);  // 翻译模式 Ctrl+Alt+T
        SetBinding(3, 0x44, kModControl | kModAlt);  // 划词翻译 Ctrl+Alt+D
        SetBinding(4, 0x49, kModControl | kModAlt);  // 翻译插入 Ctrl+Alt+I
        SetBinding(5, 0x43, kModControl | kModAlt);  // 取色     Ctrl+Alt+C
        recordingRow_ = -1;
        conflicts_.clear();
        statusMessage_.clear();
    }

    // ---- 行数据 ----
    std::uint32_t VkForRow(int row) const { return rows_[row].vk; }
    std::uint32_t ModifiersForRow(int row) const { return rows_[row].modifiers; }
    std::string ComboTextForRow(int row) const {
        return FormatCombo(rows_[row].vk, rows_[row].modifiers);
    }

    // ---- 录制状态机 ----
    bool IsRecording() const { return recordingRow_ >= 0; }
    int RecordingRow() const { return recordingRow_; }
    void BeginRecording(int row) {
        // 仅一行在录：切到新行即放弃旧行的录制态。
        recordingRow_ = row;
        ClearStatus();
    }
    void CancelRecording() { recordingRow_ = -1; ClearStatus(); }

    /// 录制输入处理。返回值 = 录制是否结束：
    ///   - 无修饰键且非 Esc → 返回 false（保持录制），statusMessage 置
    ///     "需至少一个修饰键"提示（hasModifier==false 时由调用方展示）。
    ///   - 合法（含 Esc 取消）→ 返回 true（录制结束；Esc 时 cancelled=true 不写入）。
    struct RecordResult {
        bool finished;      // 录制流程结束
        bool cancelled;     // Esc 取消（不写入）
        bool needsModifier; // 单键无修饰，行内提示"需至少一个修饰键"，仍保持录制
    };
    RecordResult HandleRecordInput(std::uint32_t vk, std::uint32_t modifiers) {
        constexpr std::uint32_t VK_Escape = 0x1B;
        if (recordingRow_ < 0) {
            // 防御：未在录制（页面仅 IsRecording() 为真时喂键），按取消处理。
            return {true, true, false};
        }
        if (vk == VK_Escape) {
            CancelRecording();
            return {true, true, false};
        }
        if ((modifiers & kModAll) == 0) {
            // [待确认] spec 仅说"行内提示"，当前保守选择：不退出录制，提示文案
            // 置于页脚 StatusText（与 SettingsWindow.cpp 通用提示行同位置）。
            SetStatus(L"快捷键需包含至少一个修饰键（Ctrl/Alt/Shift/Win）");
            return {false, false, true};
        }
        int row = recordingRow_;
        rows_[row].vk = vk;
        rows_[row].modifiers = modifiers;
        recordingRow_ = -1;
        ClearStatus();
        return {true, false, false};
    }

    /// 每行重置按钮：恢复该行动作默认值。
    void ResetRow(int row) {
        // [待确认] 重置后是否立即写盘/重注册，spec 未明；当前保守选择：仅更新内存，
        // 由 code-behind 统一走 Persist()（与录制写入同一路径）。
        rows_[row].vk = defaults_[row].vk;
        rows_[row].modifiers = defaults_[row].modifiers;
        recordingRow_ = -1;
        ClearStatus();
    }

    // ---- 冲突检测（页内）----
    // [待确认] spec：冲突 UI 呈现形式。当前保守选择：冲突行红色前景 +
    // 行下方 ConflictMessage TextBlock（本页 XAML 已含隐藏 TextBlock）。
    // 页内规则：六行两两比较 (vk, modifiers) 相同 → 双方行标红。
    // [集成点] 注册期真实冲突（与其他程序/系统热键撞车）由 shell 调
    // HotkeyManager::reconfigure 返回 HotkeyRegistrationReport；页订阅回调
    // （shell 持页引用或事件令牌），把 report.conflicts 映射到对应行标红。
    std::vector<ShortcutConflictRow> DetectConflicts() {
        conflicts_.clear();
        for (int i = 0; i < kShortcutRowCount; ++i) {
            for (int j = i + 1; j < kShortcutRowCount; ++j) {
                if (rows_[i].vk == rows_[j].vk &&
                    rows_[i].modifiers == rows_[j].modifiers) {
                    std::string combo = ComboTextForRow(i);
                    conflicts_.push_back({i, rows_[i].vk, rows_[i].modifiers,
                        "与「" + Narrow(ActionLabelForRow(j)) + "」冲突（" + combo + "）"});
                    conflicts_.push_back({j, rows_[j].vk, rows_[j].modifiers,
                        "与「" + Narrow(ActionLabelForRow(i)) + "」冲突（" + combo + "）"});
                }
            }
        }
        return conflicts_;
    }
    bool RowHasConflict(int row) const {
        for (const auto& c : conflicts_) if (c.row == row) return true;
        return false;
    }
    std::string ConflictMessageForRow(int row) const {
        for (const auto& c : conflicts_) if (c.row == row) return c.message;
        return {};
    }
    /// 行号 → HotkeySpec（供 shell 注册；PickColor 无 HotkeyAction 枚举值，返回 nullopt）。
    std::optional<HotkeySpec> HotkeySpecForRow(int row) const {
        if (row < 0 || row >= 5) return std::nullopt;  // [集成点] pickColor 需扩 HotkeyAction
        static const HotkeyAction kActions[5] = {
            HotkeyAction::FullScreen, HotkeyAction::Region, HotkeyAction::Translate,
            HotkeyAction::TextSelection, HotkeyAction::TranslateInsert,
        };
        return HotkeySpec{kActions[row], rows_[row].vk, rows_[row].modifiers, true};
    }

    // ---- 持久化（包装 HotkeySettingsStore 形状）----
    /// 前 5 行 → HotkeyBinding（actionName 用 kHotkeyActionNames 枚举序名）。
    std::vector<core::HotkeyBinding> BindingsForStore() const {
        std::vector<core::HotkeyBinding> out;
        for (int i = 0; i < 5; ++i) {
            out.push_back({core::kHotkeyActionNames[i], rows_[i].vk, rows_[i].modifiers, true});
        }
        return out;
    }
    /// 第 6 行（pickColor）→ 单独的 settings 键值（schema §1 pickColorShortcut）。
    core::HotkeyBinding PickColorBinding() const {
        return {"pickColor", rows_[5].vk, rows_[5].modifiers, true};
    }
    /// 从 store 载入（缺省行保留默认）。
    void LoadFromBindings(const std::vector<core::HotkeyBinding>& bindings,
                          std::optional<core::HotkeyBinding> pickColor) {
        for (const auto& b : bindings) {
            for (int i = 0; i < 5; ++i) {
                if (b.actionName == core::kHotkeyActionNames[i]) {
                    rows_[i].vk = b.vk; rows_[i].modifiers = b.modifiers;
                }
            }
        }
        if (pickColor) { rows_[5].vk = pickColor->vk; rows_[5].modifiers = pickColor->modifiers; }
        recordingRow_ = -1;
        conflicts_.clear();
    }

    // ---- 页脚提示 ----
    const std::wstring& StatusMessage() const { return statusMessage_; }
    bool HasStatus() const { return !statusMessage_.empty(); }

private:
    struct Row { std::uint32_t vk = 0; std::uint32_t modifiers = 0; };
    Row rows_[kShortcutRowCount];
    Row defaults_[kShortcutRowCount];
    int recordingRow_ = -1;   // 单一录制状态，-1 = 未录制
    std::vector<ShortcutConflictRow> conflicts_;
    std::wstring statusMessage_;

    void SetBinding(int row, std::uint32_t vk, std::uint32_t mods) {
        rows_[row] = defaults_[row] = {vk, mods};
    }
    void SetStatus(const wchar_t* msg) { statusMessage_ = msg; }
    void ClearStatus() { statusMessage_.clear(); }
    static std::string Narrow(const wchar_t* w) {  // 冲突文案拼接用（本地化字面量 ASCII 兼容降级）
        std::string s;
        for (; *w; ++w) s += static_cast<char>(*w < 0x80 ? *w : '?');
        return s;
    }
};

}  // namespace tf::ui

#endif  // TF_SHORTCUTS_PAGE_VIEW_MODEL_HPP
