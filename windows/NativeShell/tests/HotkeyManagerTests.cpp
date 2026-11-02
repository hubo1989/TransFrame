// tests/HotkeyManagerTests.cpp — registration lifecycle + conflict matrix.
#include "catch_amalgamated.hpp"

#include "tf/HotkeyManager.hpp"

#include <map>
#include <set>

using namespace tf;

namespace {

/// Records RegisterHotKey attempts; can simulate OS rejections
/// (conflicts with another application).
class FakeRegistrar final : public IHotkeyRegistrar {
public:
    bool registerHotkey(int id, std::uint32_t vk, std::uint32_t modifiers) override {
        if (denyAll) return false;
        if (rejectKeys.count(vk) && (modifiers & rejectedModifiers)) return false;
        const auto key = std::make_pair(vk, modifiers);
        if (claimed.find(key) != claimed.end()) return false;  // already ours (bug guard)
        claimed.insert(key);
        live.insert(id);
        liveToKey[id] = key;
        return true;
    }

    bool unregisterHotkey(int id) override {
        live.erase(id);
        // Mirror the OS: unregistering releases the global combination so a
        // later applySpecs (runtime re-registration) can reclaim it.
        if (const auto it = liveToKey.find(id); it != liveToKey.end()) {
            claimed.erase(it->second);
            liveToKey.erase(it);
        }
        return true;
    }

    std::set<std::pair<std::uint32_t, std::uint32_t>> claimed;
    std::set<int> live;
    bool denyAll = false;
    std::set<std::uint32_t> rejectKeys;
    std::uint32_t rejectedModifiers = 0xFFFFFFFFu;

private:
    std::map<int, std::pair<std::uint32_t, std::uint32_t>> liveToKey;
};

HotkeySpec spec(HotkeyAction action, std::uint32_t vk, std::uint32_t mods, bool enabled = true) {
    return HotkeySpec{action, vk, mods, enabled};
}

} // namespace

TEST_CASE("default specs are the PRD Appendix A set", "[hotkey][defaults]") {
    const auto specs = defaultHotkeySpecs();
    REQUIRE(specs.size() == 5);

    const std::uint32_t ctrlAlt = kModControl | kModAlt;
    const auto same = [](const HotkeySpec& a, const HotkeySpec& b) {
        return a.action == b.action && a.vk == b.vk &&
               a.modifiers == b.modifiers && a.enabled == b.enabled;
    };
    CHECK(same(specs[0], spec(HotkeyAction::FullScreen, 0x46, ctrlAlt)));
    CHECK(same(specs[1], spec(HotkeyAction::Region, 0x41, ctrlAlt)));
    CHECK(same(specs[2], spec(HotkeyAction::Translate, 0x54, ctrlAlt)));
    CHECK(same(specs[3], spec(HotkeyAction::TextSelection, 0x44, ctrlAlt)));
    CHECK(same(specs[4], spec(HotkeyAction::TranslateInsert, 0x49, ctrlAlt)));
}

TEST_CASE("all defaults register cleanly with no conflicts", "[hotkey][register]") {
    FakeRegistrar registrar;
    HotkeyManager mgr(registrar);

    const auto report = mgr.applySpecs(defaultHotkeySpecs());
    CHECK(report.allRegistered);
    CHECK(report.registered.size() == 5);
    CHECK(report.conflicts.empty());
    CHECK(mgr.isRegistered(HotkeyAction::Region));
    CHECK(registrar.live.size() == 5);
}

TEST_CASE("OS rejection surfaces as a conflict report, never silent", "[hotkey][conflict]") {
    FakeRegistrar registrar;
    registrar.rejectKeys.insert(0x41);  // another app owns Ctrl+Alt+A
    HotkeyManager mgr(registrar);

    auto specs = defaultHotkeySpecs();
    const auto report = mgr.applySpecs(specs);

    CHECK_FALSE(report.allRegistered);
    REQUIRE(report.conflicts.size() == 1);
    CHECK(report.conflicts[0].action == HotkeyAction::Region);
    CHECK(report.conflicts[0].vk == 0x41);
    CHECK_FALSE(report.conflicts[0].message.empty());
    CHECK_FALSE(mgr.isRegistered(HotkeyAction::Region));
    // Others still live.
    CHECK(mgr.isRegistered(HotkeyAction::FullScreen));
}

TEST_CASE("re-registration at runtime replaces the active set immediately",
          "[hotkey][reregister]") {
    FakeRegistrar registrar;
    HotkeyManager mgr(registrar);

    const auto first = mgr.applySpecs(defaultHotkeySpecs());
    REQUIRE(first.allRegistered);

    // User remaps Region from Ctrl+Alt+A to Ctrl+Alt+R (0x52).
    auto remapped = defaultHotkeySpecs();
    remapped[1].vk = 0x52;
    const auto second = mgr.applySpecs(remapped);

    CHECK(second.allRegistered);
    CHECK(registrar.live.size() == 5);  // old set fully unregistered first
    // Dispatch under the new binding works, old id too (ids are stable).
    HotkeyAction got{};
    mgr.setHandler([&](HotkeyAction a) { got = a; });
    CHECK(mgr.dispatchWmHotkey(HotkeyManager::idForAction(HotkeyAction::Region)));
    CHECK(got == HotkeyAction::Region);
}

TEST_CASE("intra-set duplicate combination is detected before the OS call",
          "[hotkey][conflict]") {
    auto specs = defaultHotkeySpecs();
    specs[4].vk = specs[0].vk;         // TranslateInsert == FullScreen binding
    const auto dup = findDuplicateSpec(specs);
    REQUIRE(dup.has_value());
    CHECK(*dup == HotkeyAction::TranslateInsert);

    // The OS registrar would also reject it; report shape stays consistent.
    FakeRegistrar registrar;
    registrar.rejectKeys.insert(specs[0].vk);  // second claim of same combo
    HotkeyManager mgr(registrar);
    const auto report = mgr.applySpecs(specs);
    CHECK_FALSE(report.allRegistered);
}

TEST_CASE("invalid specs: bare key without modifier is rejected", "[hotkey][validate]") {
    CHECK_FALSE(isHotkeySpecValid(spec(HotkeyAction::Region, 0x41, 0)));
    CHECK_FALSE(isHotkeySpecValid(spec(HotkeyAction::Region, 0, kModAlt)));
    CHECK_FALSE(isHotkeySpecValid(spec(HotkeyAction::Region, 0x41, kModAlt, /*enabled=*/false)));
    CHECK(isHotkeySpecValid(spec(HotkeyAction::Region, 0x41, kModAlt)));
    CHECK(isHotkeySpecValid(spec(HotkeyAction::Region, 0x41, kModShift)));
    CHECK(isHotkeySpecValid(spec(HotkeyAction::Region, 0x41, kModWin)));
}

TEST_CASE("disabled specs are skipped entirely", "[hotkey][register]") {
    FakeRegistrar registrar;
    HotkeyManager mgr(registrar);

    auto specs = defaultHotkeySpecs();
    specs[2].enabled = false;  // Translate off
    const auto report = mgr.applySpecs(specs);

    CHECK(report.allRegistered);
    CHECK(report.registered.size() == 4);
    CHECK_FALSE(mgr.isRegistered(HotkeyAction::Translate));
}

TEST_CASE("dispatch of an unknown id returns false (not our hotkey)", "[hotkey][dispatch]") {
    FakeRegistrar registrar;
    HotkeyManager mgr(registrar);
    (void)mgr.applySpecs(defaultHotkeySpecs());
    CHECK_FALSE(mgr.dispatchWmHotkey(99));
}

TEST_CASE("unregisterAll releases every OS slot", "[hotkey][teardown]") {
    FakeRegistrar registrar;
    HotkeyManager mgr(registrar);
    (void)mgr.applySpecs(defaultHotkeySpecs());
    REQUIRE(registrar.live.size() == 5);
    mgr.unregisterAll();
    CHECK(registrar.live.empty());
    CHECK_FALSE(mgr.isRegistered(HotkeyAction::FullScreen));
}

TEST_CASE("total failure keeps the report actionable", "[hotkey][conflict]") {
    FakeRegistrar registrar;
    registrar.denyAll = true;
    HotkeyManager mgr(registrar);
    const auto report = mgr.applySpecs(defaultHotkeySpecs());
    CHECK_FALSE(report.allRegistered);
    CHECK(report.registered.empty());
    REQUIRE(report.conflicts.size() == 5);
    for (const auto& c : report.conflicts) {
        CHECK_FALSE(c.message.empty());
    }
}

TEST_CASE("formatHotkeySpec renders PRD defaults", "[hotkey][format]") {
    const auto specs = defaultHotkeySpecs();
    CHECK(formatHotkeySpec(specs[0]) == L"Ctrl+Alt+F");
    CHECK(formatHotkeySpec(specs[1]) == L"Ctrl+Alt+A");
    CHECK(formatHotkeySpec(specs[4]) == L"Ctrl+Alt+I");
}
