#pragma once
// Turns one captured event into the list of actions the broadcaster executes.
// Pure function of (event, routes, live targets, master key state): the whole
// policy of "what gets mirrored where, and what has to be repaired afterwards"
// lives here and is unit-tested without a window in sight.
#include <cstdint>
#include <vector>

#include "mm/input_event.hpp"
#include "mm/key_state.hpp"
#include "mm/routing.hpp"

namespace mm {

enum class ActionKind : uint8_t {
    Deliver,       // send `ev` to instance `target` using `strategy`
    RestoreFocus,  // bring the master back to the foreground
    RestoreCursor, // put the cursor back where the master had it (ev.x, ev.y)
    ReassertHeld,  // re-inject the master's held keys (Unity dropped them on focus loss)
    SwapMaster,    // the swap hotkey was pressed
};

struct Action {
    ActionKind kind;
    uint32_t   target = 0;   // instance index for Deliver
    Strategy   strategy = Strategy::FocusCycle;
};

struct Plan {
    std::vector<Action> actions;
    bool swallowed = false;  // informational: the capture layer already blocked the key for the master
};

namespace detail {
inline bool add_deliveries(Plan& plan, TargetMask targets, TargetMask alive, uint32_t master, Strategy s) {
    const TargetMask mask = targets & alive & ~target_bit(master);
    bool any = false;
    for (uint32_t i = 0; i < 32; ++i) {
        if (mask & target_bit(i)) {
            plan.actions.push_back(Action{ActionKind::Deliver, i, s});
            any = true;
        }
    }
    return any;
}
}  // namespace detail

// `master_keys` must already reflect `ev` (the caller applies the event first).
inline Plan plan(const InputEvent& ev, const Routes& routes, TargetMask alive, uint32_t master,
                 const KeyState& master_keys) {
    Plan p;

    if (is_key(ev.kind)) {
        if (routes.swap_hotkey_vk != 0 && ev.vk == routes.swap_hotkey_vk) {
            p.swallowed = true;
            if (ev.kind == EventKind::KeyDown) p.actions.push_back(Action{ActionKind::SwapMaster});
            return p;
        }
        const KeyBinding* b = routes.find(ev.vk);
        if (!b) return p;
        p.swallowed = b->swallow;
        const bool delivered = detail::add_deliveries(p, b->targets, alive, master, b->strategy);
        if (delivered && b->strategy == Strategy::FocusCycle) {
            p.actions.push_back(Action{ActionKind::RestoreFocus});
            if (master_keys.held_count() > 0) p.actions.push_back(Action{ActionKind::ReassertHeld});
        }
        return p;
    }

    if (is_mouse_button(ev.kind) || ev.kind == EventKind::Wheel) {
        if (routes.mouse_targets == 0) return p;
        if (routes.mouse_modifier_vk != 0 && !master_keys.is_down(routes.mouse_modifier_vk)) return p;
        const bool delivered = detail::add_deliveries(p, routes.mouse_targets, alive, master, routes.mouse_strategy);
        if (delivered && routes.mouse_strategy == Strategy::FocusCycle) {
            p.actions.push_back(Action{ActionKind::RestoreCursor});
            p.actions.push_back(Action{ActionKind::RestoreFocus});
            if (master_keys.held_count() > 0) p.actions.push_back(Action{ActionKind::ReassertHeld});
        }
        return p;
    }

    // MouseMove is never broadcast: only the foreground window would see it,
    // and it is the highest-frequency event on the system.
    return p;
}

}  // namespace mm
