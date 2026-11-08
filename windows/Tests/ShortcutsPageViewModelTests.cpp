// ShortcutsPageViewModelTests.cpp — 快捷键页签 VM（默认值、组合显示、
// 录制状态机、页内冲突检测、HotkeySettingsStore 形状的持久化往返）。
#include <catch2/catch_amalgamated.hpp>

using Catch::Approx;

#include "ShortcutsPageViewModel.h"

#include "tf/HotkeySettingsStore.hpp"

using tf::ui::FormatCombo;
using tf::ui::ShortcutRow;
using tf::ui::ShortcutsPageViewModel;
using tf::core::HotkeyBinding;

TEST_CASE("shortcut vm defaults use Ctrl+Alt+ family (README defaults)",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    const std::uint32_t ctrlAlt = tf::ui::kModControl | tf::ui::kModAlt;
    CHECK(vm.VkForRow(0) == 0x46);                // F
    CHECK(vm.VkForRow(1) == 0x41);                // A
    CHECK(vm.VkForRow(2) == 0x54);                // T
    CHECK(vm.VkForRow(3) == 0x44);                // D
    CHECK(vm.VkForRow(4) == 0x49);                // I
    CHECK(vm.VkForRow(5) == 0x43);                // C（取色）
    for (int row = 0; row < tf::ui::kShortcutRowCount; ++row) {
        CHECK(vm.ModifiersForRow(row) == ctrlAlt);
    }
}

TEST_CASE("FormatCombo renders modifiers in fixed order", "[shortcuts][settings]") {
    CHECK(FormatCombo(0x41, tf::ui::kModControl | tf::ui::kModAlt) == "Ctrl+Alt+A");
    CHECK(FormatCombo(0x42, tf::ui::kModWin | tf::ui::kModShift | tf::ui::kModAlt |
                                tf::ui::kModControl) == "Ctrl+Alt+Shift+Win+B");
    CHECK(FormatCombo(0x31, 0) == "1");           // 无修饰数字键
    CHECK(FormatCombo(0x70, tf::ui::kModControl) == "Ctrl+F1");
    CHECK(FormatCombo(0x7B, tf::ui::kModShift) == "Shift+F12");
    CHECK(FormatCombo(0xB8, tf::ui::kModControl) == "Ctrl+VK 0xB8");  // 非可打印键
}

TEST_CASE("shortcut recording state machine finishes on valid combo",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    CHECK_FALSE(vm.IsRecording());

    vm.BeginRecording(2);
    CHECK(vm.IsRecording());
    CHECK(vm.RecordingRow() == 2);

    auto noMod = vm.HandleRecordInput(0x51, 0);   // 裸 Q：保持录制 + 提示
    CHECK_FALSE(noMod.finished);
    CHECK(noMod.needsModifier);
    CHECK(vm.IsRecording());
    CHECK(vm.HasStatus());

    auto ok = vm.HandleRecordInput(0x51, tf::ui::kModControl | tf::ui::kModShift);
    CHECK(ok.finished);
    CHECK_FALSE(ok.cancelled);
    CHECK_FALSE(vm.IsRecording());
    CHECK(vm.VkForRow(2) == 0x51);
    CHECK(vm.ModifiersForRow(2) ==
          (tf::ui::kModControl | tf::ui::kModShift));
    CHECK_FALSE(vm.HasStatus());                  // 录制结束清提示
}

TEST_CASE("shortcut recording escape cancels without writing", "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    vm.BeginRecording(1);
    auto esc = vm.HandleRecordInput(0x1B, tf::ui::kModControl);  // Esc 取消
    CHECK(esc.finished);
    CHECK(esc.cancelled);
    CHECK_FALSE(vm.IsRecording());
    CHECK(vm.VkForRow(1) == 0x41);                // 默认值未动
}

TEST_CASE("shortcut recording switches rows abandoning previous state",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    vm.BeginRecording(0);
    vm.BeginRecording(4);                         // 同一时刻仅一行在录
    CHECK(vm.RecordingRow() == 4);
    auto ok = vm.HandleRecordInput(0x47, tf::ui::kModAlt);
    CHECK(ok.finished);
    CHECK(vm.VkForRow(4) == 0x47);
    CHECK(vm.VkForRow(0) == 0x46);                // 行 0 未被写入
}

TEST_CASE("shortcut row reset restores that row's default only",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    vm.BeginRecording(3);
    vm.HandleRecordInput(0x50, tf::ui::kModWin);
    CHECK(vm.ComboTextForRow(3) == "Win+P");
    vm.ResetRow(3);
    CHECK(vm.ComboTextForRow(3) == "Ctrl+Alt+D");
    CHECK(vm.ComboTextForRow(2) == "Ctrl+Alt+T");  // 其它行不动
}

TEST_CASE("shortcut record input without active recording is a no-op",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;                    // 未 BeginRecording 直接喂键（防御）
    auto result = vm.HandleRecordInput(0x51, tf::ui::kModControl);
    CHECK(result.finished);
    CHECK(vm.ComboTextForRow(0) == "Ctrl+Alt+F");  // 默认值未被写坏
}

TEST_CASE("shortcut conflicts flag both rows pairwise", "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    vm.BeginRecording(0);
    vm.HandleRecordInput(0x41, tf::ui::kModControl | tf::ui::kModAlt);  // 与行 1 同组合
    auto conflicts = vm.DetectConflicts();
    REQUIRE(conflicts.size() == 2);               // 双方各一条
    CHECK(vm.RowHasConflict(0));
    CHECK(vm.RowHasConflict(1));
    CHECK_FALSE(vm.RowHasConflict(2));
    CHECK(vm.ConflictMessageForRow(0).find("冲突") != std::string::npos);

    vm.ResetRow(0);                               // 解除冲突
    vm.DetectConflicts();
    CHECK_FALSE(vm.RowHasConflict(0));
    CHECK_FALSE(vm.RowHasConflict(1));
}

TEST_CASE("shortcut store round trip keeps 5 actions and pickColor separate",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    vm.BeginRecording(2);
    vm.HandleRecordInput(0x58, tf::ui::kModShift);
    vm.BeginRecording(5);
    vm.HandleRecordInput(0x59, tf::ui::kModWin);

    auto bindings = vm.BindingsForStore();        // HotkeySettingsStore 形状
    REQUIRE(bindings.size() == 5);
    CHECK(bindings[0].actionName == tf::core::kHotkeyActionNames[0]);
    CHECK(bindings[2].vk == 0x58);
    CHECK(bindings[2].modifiers == tf::ui::kModShift);
    auto pick = vm.PickColorBinding();            // 取色单独键（schema §1）
    CHECK(pick.actionName == "pickColor");
    CHECK(pick.vk == 0x59);
    CHECK(pick.modifiers == tf::ui::kModWin);

    ShortcutsPageViewModel reloaded;              // 从 store 载入（缺省行保留默认）
    reloaded.LoadFromBindings(bindings, pick);
    CHECK(reloaded.ComboTextForRow(2) == "Shift+X");
    CHECK(reloaded.ComboTextForRow(5) == "Win+Y");
    CHECK(reloaded.ComboTextForRow(0) == "Ctrl+Alt+F");

    ShortcutsPageViewModel empty;                 // 空 store：全部默认
    empty.LoadFromBindings({}, std::nullopt);
    CHECK(empty.ComboTextForRow(2) == "Ctrl+Alt+T");
    CHECK(empty.ComboTextForRow(5) == "Ctrl+Alt+C");
}

TEST_CASE("shortcut HotkeySpec rows map 5 actions; pickColor is nullopt",
          "[shortcuts][settings]") {
    ShortcutsPageViewModel vm;
    const tf::HotkeyAction expected[5] = {
        tf::HotkeyAction::FullScreen, tf::HotkeyAction::Region,
        tf::HotkeyAction::Translate, tf::HotkeyAction::TextSelection,
        tf::HotkeyAction::TranslateInsert,
    };
    for (int row = 0; row < 5; ++row) {
        auto spec = vm.HotkeySpecForRow(row);
        REQUIRE(spec.has_value());
        CHECK(spec->action == expected[row]);     // 行序与 HotkeyAction 枚举对齐
        CHECK(spec->enabled);
    }
    CHECK_FALSE(vm.HotkeySpecForRow(5).has_value());  // [集成点] pickColor 待扩 store
    CHECK_FALSE(vm.HotkeySpecForRow(-1).has_value());
    CHECK_FALSE(vm.HotkeySpecForRow(9).has_value());
}
