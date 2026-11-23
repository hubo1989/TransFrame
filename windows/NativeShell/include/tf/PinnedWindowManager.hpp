// tf/PinnedWindowManager.hpp — pinned screenshot windows (PRD FR-14).
//
// macOS parity (TransFrame/Features/Pinned/PinnedWindowsManager.swift): a
// fleet of borderless always-on-top windows, one per pinned capture; the
// default state is click-through (点击穿透, macOS ignoresMouseEvents) with a
// per-window / fleet-wide switch to interactive (可交互: drag to move, ✕ to
// close, right-click returns to click-through); "聚拢到前台" re-tiles and
// raises every pin; max 10 pins; pinning an already-pinned id raises the
// existing window instead of duplicating it.
//
// PinnedWindowState is the pure, Win32-free state machine (inline, Catch2-
// covered); PinnedWindowManager owns the HWND fleet, main thread only. Each
// pin is WS_POPUP | WS_EX_TOPMOST | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
// (PMv2 physical pixels); while click-through it also carries
// WS_EX_LAYERED | WS_EX_TRANSPARENT so mouse input falls through to the
// windows below.
#ifndef TF_PINNED_WINDOW_MANAGER_HPP
#define TF_PINNED_WINDOW_MANAGER_HPP

#include "tf/CoreTypes.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace tf {

/// Fleet snapshot entry (tray submenu + tests).
struct PinnedWindowInfo {
    std::uint64_t id = 0;
    int widthPx = 0;
    int heightPx = 0;
    bool clickThrough = true;
    bool visible = true;
};

/// Pure pinned-fleet state machine (no Win32; unit-testable everywhere).
/// Vector order == z order: the first slot is the topmost window.
class PinnedWindowState {
public:
    static constexpr std::size_t kMaxPinned = 10;

    struct PinOutcome {
        bool ok = false;          // a window with `id` exists after the call
        bool createdNew = false;  // false => an existing window was raised
        std::uint64_t id = 0;     // effective id (caller may pass 0 = auto)
    };

    /// Pin a new id (or raise + show the existing one — macOS pin()). A new
    /// pin fails once the fleet holds kMaxPinned windows.
    PinOutcome pin(std::uint64_t idOrZero, int widthPx, int heightPx) {
        if (idOrZero != 0) {
            if (PinnedWindowInfo* existing = find(idOrZero)) {
                existing->visible = true;
                raise(idOrZero);
                return PinOutcome{/*ok=*/true, /*createdNew=*/false, idOrZero};
            }
        }
        if (pins_.size() >= kMaxPinned) return PinOutcome{};
        const std::uint64_t id = idOrZero != 0 ? idOrZero : nextAutoId_;
        // Auto ids never collide with explicit ids seen so far.
        nextAutoId_ = std::max(nextAutoId_, id + 1);
        PinnedWindowInfo info;
        info.id = id;
        info.widthPx = widthPx;
        info.heightPx = heightPx;
        info.clickThrough = true;   // macOS default: 点击穿透
        info.visible = true;
        pins_.insert(pins_.begin(), Slot{info});  // newest pin on top
        return PinOutcome{true, true, id};
    }

    bool unpin(std::uint64_t id) {
        for (auto it = pins_.begin(); it != pins_.end(); ++it) {
            if (it->info.id == id) {
                pins_.erase(it);
                return true;
            }
        }
        return false;
    }

    void unpinAll() { pins_.clear(); }

    bool setClickThrough(std::uint64_t id, bool through) {
        if (PinnedWindowInfo* info = find(id)) {
            info->clickThrough = through;
            return true;
        }
        return false;
    }

    int setAllClickThrough(bool through) {
        int changed = 0;
        for (auto& slot : pins_) {
            if (slot.info.clickThrough != through) {
                slot.info.clickThrough = through;
                ++changed;
            }
        }
        return changed;
    }

    bool toggleClickThrough(std::uint64_t id) {
        if (PinnedWindowInfo* info = find(id)) {
            info->clickThrough = !info->clickThrough;
            return true;
        }
        return false;
    }

    /// 聚拢到前台: every window becomes visible; z order stays list order.
    /// (The Win32 layer adds the grid tiling over the work area.)
    void gatherAll() {
        for (auto& slot : pins_) slot.info.visible = true;
    }

    void bringToFront(std::uint64_t id) {
        if (PinnedWindowInfo* info = find(id)) {
            info->visible = true;
            raise(id);
        }
    }

    bool isPinned(std::uint64_t id) const {
        for (const auto& slot : pins_) {
            if (slot.info.id == id) return true;
        }
        return false;
    }

    std::size_t count() const { return pins_.size(); }

    std::vector<PinnedWindowInfo> snapshot() const {
        std::vector<PinnedWindowInfo> out;
        out.reserve(pins_.size());
        for (const auto& slot : pins_) out.push_back(slot.info);
        return out;
    }

private:
    struct Slot {
        PinnedWindowInfo info;
    };

    PinnedWindowInfo* find(std::uint64_t id) {
        for (auto& slot : pins_) {
            if (slot.info.id == id) return &slot.info;
        }
        return nullptr;
    }

    const PinnedWindowInfo* find(std::uint64_t id) const {
        for (const auto& slot : pins_) {
            if (slot.info.id == id) return &slot.info;
        }
        return nullptr;
    }

    /// Move `id`'s slot to the front (top of the z order).
    void raise(std::uint64_t id) {
        for (auto it = pins_.begin(); it != pins_.end(); ++it) {
            if (it->info.id == id) {
                Slot slot = *it;
                pins_.erase(it);
                pins_.insert(pins_.begin(), std::move(slot));
                return;
            }
        }
    }

    std::vector<Slot> pins_;  // front = topmost
    std::uint64_t nextAutoId_ = 1;
};

/// Win32 fleet manager (main thread only, like every HWND owner here).
class PinnedWindowManager {
public:
    /// Process-wide fleet.
    static PinnedWindowManager& instance();
    /// Nullptr before the first instance() call (window-proc lookup).
    static PinnedWindowManager* instanceIfExists();
    /// Test/teardown hook: destroys the fleet (AppMain calls unpinAll first).
    static void destroyInstance();

    PinnedWindowManager(const PinnedWindowManager&) = delete;
    PinnedWindowManager& operator=(const PinnedWindowManager&) = delete;

    /// Pin `pixels` at screen position `originPhysical` (physical px; centered
    /// on the primary work area when nullopt). Scaled down to fit the work
    /// area when the capture is larger. idOrZero == 0 auto-assigns. Returns
    /// false at the 10-pin limit or on window-creation failure.
    bool pin(std::uint64_t idOrZero, std::shared_ptr<PixelBuffer> pixels,
             const std::optional<std::pair<int, int>>& originPhysical);

    void unpin(std::uint64_t id);
    void unpinAll();
    void bringToFront(std::uint64_t id);
    bool setClickThrough(std::uint64_t id, bool through);

    /// Tray 切换穿透: if any window is click-through the whole fleet becomes
    /// interactive (macOS resetAllInteractions), otherwise all go back to
    /// click-through. Deterministic; semantics tested on PinnedWindowState.
    void toggleClickThroughFleet();

    /// 聚拢到前台: grid-tile every pin over the primary work area, then show
    /// and raise them (no activation — pins never steal focus).
    void gatherAll();

    std::size_t count() const;
    std::vector<PinnedWindowInfo> snapshot() const;
    /// HWND for id (smoke assertions on the extended style); void* keeps the
    /// header Win32-free. Nullptr when not pinned.
    void* hwndForId(std::uint64_t id) const;

    /// Fires after every fleet mutation (AppMain refreshes the tray submenu).
    void setOnChanged(std::function<void()> callback);

private:
    PinnedWindowManager();
    ~PinnedWindowManager();

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace tf

#endif  // TF_PINNED_WINDOW_MANAGER_HPP
