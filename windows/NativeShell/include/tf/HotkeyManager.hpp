// tf/HotkeyManager.hpp
// Global hotkeys (FR-33): RegisterHotKey / WM_HOTKEY x5
// (full-screen / region / translate / text-selection / translate-insert).
// Registration failure = conflict report (no silent ignore). Re-registration
// at runtime applies immediately (settings change -> unregister -> register).
//
// The Win32 RegisterHotKey call is isolated behind IHotkeyRegistrar so the
// conflict matrix, dedup and re-registration state machine are unit-testable.
#ifndef TF_HOTKEY_MANAGER_HPP
#define TF_HOTKEY_MANAGER_HPP

#include "CoreTypes.hpp"

#include <functional>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace tf {

/// Virtual-key codes live in the Core KeyboardShortcut model (task A).
/// NativeShell consumes (vk, modifiers) pairs with Win32 MOD_* bit values.
struct HotkeySpec {
    HotkeyAction action = HotkeyAction::Region;
    std::uint32_t vk = 0;          // VK_* code
    std::uint32_t modifiers = 0;   // MOD_ALT|MOD_CONTROL|MOD_SHIFT|MOD_WIN
    bool enabled = true;
};

struct HotkeyConflict {
    HotkeyAction action = HotkeyAction::Region;
    std::uint32_t vk = 0;
    std::uint32_t modifiers = 0;
    std::wstring message;   // user-facing reason (registration failure text)
};

struct HotkeyRegistrationReport {
    bool allRegistered = false;
    std::vector<HotkeyAction> registered;      // actions now live
    std::vector<HotkeyConflict> conflicts;     // failed actions with reason
};

using HotkeyHandler = std::function<void(HotkeyAction action)>;

/// Platform hook over RegisterHotKey/UnregisterHotKey. Returns false when the
/// OS rejects the combination (conflict with another app or invalid spec).
class IHotkeyRegistrar {
public:
    virtual ~IHotkeyRegistrar() = default;
    virtual bool registerHotkey(int id, std::uint32_t vk, std::uint32_t modifiers) = 0;
    virtual bool unregisterHotkey(int id) = 0;
};

/// Win32 MOD_* modifier bit helpers (used by the App settings layer too).
enum ModBits : std::uint32_t {
    kModNone   = 0,
    kModAlt    = 0x0001,
    kModControl= 0x0002,
    kModShift  = 0x0004,
    kModWin    = 0x0008,
};

/// Pure validation: a spec is registerable when it has a non-zero VK and at
/// least one modifier (matches macOS validation rule, PRD FR-33).
bool isHotkeySpecValid(const HotkeySpec& spec);

/// Pure conflict detection within a spec set: same (vk, modifiers) twice.
std::optional<HotkeyAction> findDuplicateSpec(const std::vector<HotkeySpec>& specs);

class HotkeyManager {
public:
    explicit HotkeyManager(IHotkeyRegistrar& registrar) : registrar_(&registrar) {}

    /// Register all enabled specs; unregisters previous set first so runtime
    /// re-registration takes effect immediately. Idempotent.
    HotkeyRegistrationReport applySpecs(const std::vector<HotkeySpec>& specs);

    /// Dispatch WM_HOTKEY id -> action -> handler. Returns true when handled.
    bool dispatchWmHotkey(int id);

    void setHandler(HotkeyHandler handler) { handler_ = std::move(handler); }

    /// Stop everything (app teardown / suspend).
    void unregisterAll();

    bool isRegistered(HotkeyAction action) const;

    /// Stable hotkey ids: index over HotkeyAction (0..4).
    static int idForAction(HotkeyAction action);

private:
    IHotkeyRegistrar* registrar_;
    HotkeyHandler handler_;
    std::unordered_map<int, HotkeyAction> idToAction_;
    std::vector<HotkeySpec> active_;
};

/// Default specs (PRD Appendix A): Ctrl+Alt+F/A/T/D/I.
std::vector<HotkeySpec> defaultHotkeySpecs();

/// Map a HotkeySpec to a short display string like "Ctrl+Alt+A" (settings UI).
std::wstring formatHotkeySpec(const HotkeySpec& spec);

} // namespace tf

#endif // TF_HOTKEY_MANAGER_HPP
