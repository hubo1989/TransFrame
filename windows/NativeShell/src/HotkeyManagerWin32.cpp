// HotkeyManagerWin32.cpp — RegisterHotKey/WM_HOTKEY plumbing.
#include "pch.h"

#include "tf/HotkeyManager.hpp"

namespace tf {
namespace {

class Win32Registrar final : public IHotkeyRegistrar {
public:
    // RegisterHotKey binds to the thread that owns the message queue; the
    // app runs this on the main UI thread where WM_HOTKEY is pumped.
    bool registerHotkey(int id, std::uint32_t vk, std::uint32_t modifiers) override {
        return ::RegisterHotKey(nullptr, id, modifiers | MOD_NOREPEAT, vk) != FALSE;
    }
    bool unregisterHotkey(int id) override {
        return ::UnregisterHotKey(nullptr, id) != FALSE;
    }
};

} // namespace

std::unique_ptr<IHotkeyRegistrar> makeWin32HotkeyRegistrar() {
    return std::make_unique<Win32Registrar>();
}

bool pumpWmHotkey(MSG* msg, HotkeyManager& manager) {
    if (msg->message != WM_HOTKEY) return false;
    return manager.dispatchWmHotkey(static_cast<int>(msg->wParam));
}

} // namespace tf
