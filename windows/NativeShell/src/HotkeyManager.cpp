// HotkeyManager policy implementation (portable; no Win32).
// RegisterHotKey/WM_HOTKEY plumbing lives in HotkeyManagerWin32.cpp.
#include "tf/HotkeyManager.hpp"

#include <algorithm>
#include <sstream>

namespace tf {

bool isHotkeySpecValid(const HotkeySpec& spec) {
    if (!spec.enabled) return false;
    if (spec.vk == 0) return false;
    // At least one modifier, mirroring the macOS validation rule (FR-33):
    // a bare key would swallow normal typing globally.
    return (spec.modifiers & (kModAlt | kModControl | kModShift | kModWin)) != 0;
}

std::optional<HotkeyAction> findDuplicateSpec(const std::vector<HotkeySpec>& specs) {
    std::vector<const HotkeySpec*> seen;
    for (const auto& s : specs) {
        if (!s.enabled) continue;
        for (const auto* p : seen) {
            if (p->vk == s.vk && p->modifiers == s.modifiers) {
                return s.action;
            }
        }
        seen.push_back(&s);
    }
    return std::nullopt;
}

int HotkeyManager::idForAction(HotkeyAction action) {
    return static_cast<int>(action);
}

HotkeyRegistrationReport HotkeyManager::applySpecs(const std::vector<HotkeySpec>& specs) {
    // Full unregister first: runtime re-registration must release the old
    // OS slots before acquiring new ones (immediate effect, FR-33).
    unregisterAll();

    HotkeyRegistrationReport report;
    report.allRegistered = true;

    for (const auto& spec : specs) {
        if (!spec.enabled) continue;

        if (!isHotkeySpecValid(spec)) {
            HotkeyConflict c;
            c.action = spec.action;
            c.vk = spec.vk;
            c.modifiers = spec.modifiers;
            c.message = L"Invalid hotkey: needs a key and at least one modifier.";
            report.conflicts.push_back(std::move(c));
            report.allRegistered = false;
            continue;
        }

        const int id = idForAction(spec.action);
        if (registrar_->registerHotkey(id, spec.vk, spec.modifiers)) {
            idToAction_[id] = spec.action;
            active_.push_back(spec);
            report.registered.push_back(spec.action);
        } else {
            // RegisterHotKey failure == combination already claimed by
            // another application: surface as conflict, never silent.
            HotkeyConflict c;
            c.action = spec.action;
            c.vk = spec.vk;
            c.modifiers = spec.modifiers;
            c.message = L"Hotkey already in use by another application.";
            report.conflicts.push_back(std::move(c));
            report.allRegistered = false;
        }
    }
    return report;
}

bool HotkeyManager::dispatchWmHotkey(int id) {
    const auto it = idToAction_.find(id);
    if (it == idToAction_.end()) return false;
    if (handler_) handler_(it->second);
    return true;
}

void HotkeyManager::unregisterAll() {
    for (const auto& spec : active_) {
        registrar_->unregisterHotkey(idForAction(spec.action));
    }
    active_.clear();
    idToAction_.clear();
}

bool HotkeyManager::isRegistered(HotkeyAction action) const {
    return idToAction_.count(idForAction(action)) != 0;
}

std::vector<HotkeySpec> defaultHotkeySpecs() {
    // PRD Appendix A: Ctrl+Alt+F/A/T/D/I (Win+Shift+S reserved for Snipping).
    constexpr std::uint32_t kCtrlAlt = kModControl | kModAlt;
    return {
        {HotkeyAction::FullScreen,      0x46 /*'F'*/, kCtrlAlt, true},  // Ctrl+Alt+F
        {HotkeyAction::Region,          0x41 /*'A'*/, kCtrlAlt, true},  // Ctrl+Alt+A
        {HotkeyAction::Translate,       0x54 /*'T'*/, kCtrlAlt, true},  // Ctrl+Alt+T
        {HotkeyAction::TextSelection,   0x44 /*'D'*/, kCtrlAlt, true},  // Ctrl+Alt+D
        {HotkeyAction::TranslateInsert, 0x49 /*'I'*/, kCtrlAlt, true},  // Ctrl+Alt+I
    };
}

std::wstring formatHotkeySpec(const HotkeySpec& spec) {
    std::wostringstream ss;
    auto addMod = [&](std::uint32_t bit, const wchar_t* name) {
        if (spec.modifiers & bit) ss << name << L"+";
    };
    addMod(kModControl, L"Ctrl");
    addMod(kModAlt, L"Alt");
    addMod(kModShift, L"Shift");
    addMod(kModWin, L"Win");
    ss << static_cast<wchar_t>(spec.vk);
    return ss.str();
}

} // namespace tf
