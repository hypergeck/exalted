#include "check.hpp"
#include "mm/dispatch.hpp"

using namespace mm;

static InputEvent key(EventKind k, uint16_t vk) { InputEvent e{}; e.kind = k; e.vk = vk; return e; }
static InputEvent mouse(EventKind k, uint16_t vk, int x, int y) { InputEvent e{}; e.kind = k; e.vk = vk; e.x = x; e.y = y; return e; }

static int count(const Plan& p, ActionKind k) {
    int n = 0; for (const auto& a : p.actions) if (a.kind == k) ++n; return n;
}

static void run_tests() {
    Routes r;
    r.add_key(KeyBinding{'1', kAllTargets, Strategy::FocusCycle, false});
    r.add_key(KeyBinding{'2', target_bit(2), Strategy::WindowMessage, false});
    r.add_key(KeyBinding{'H', kAllTargets, Strategy::FocusCycle, true});
    r.swap_hotkey_vk = 0x7B;  // F12
    r.mouse_targets = kAllTargets;
    r.mouse_modifier_vk = 0x14;  // CAPSLOCK

    CHECK(r.find('1') != nullptr);
    CHECK(r.find('Z') == nullptr);
    CHECK(r.swallows('H'));
    CHECK(r.swallows(0x7B));
    CHECK(!r.swallows('1'));
    r.add_key(KeyBinding{'1', target_bit(1), Strategy::FocusCycle, false});  // replace keeps one entry
    CHECK_EQ(r.keys().size(), std::size_t{3});
    r.add_key(KeyBinding{'1', kAllTargets, Strategy::FocusCycle, false});

    const TargetMask alive = target_bit(0) | target_bit(1) | target_bit(2) | target_bit(3);
    const uint32_t master = 0;
    KeyState keys;

    // Unbound key: nothing happens.
    CHECK(plan(key(EventKind::KeyDown, 'Z'), r, alive, master, keys).actions.empty());

    // Bound broadcast key, nothing held: deliver to 1,2,3 (never the master), then restore focus, no reassert.
    keys.apply(EventKind::KeyDown, '1');
    Plan p = plan(key(EventKind::KeyDown, '1'), r, alive, master, keys);
    CHECK_EQ(count(p, ActionKind::Deliver), 3);
    CHECK_EQ(count(p, ActionKind::RestoreFocus), 1);
    CHECK_EQ(count(p, ActionKind::ReassertHeld), 1);   // '1' itself is held
    CHECK(p.actions.back().kind == ActionKind::ReassertHeld);
    for (const auto& a : p.actions) if (a.kind == ActionKind::Deliver) CHECK(a.target != master);
    keys.apply(EventKind::KeyUp, '1');
    p = plan(key(EventKind::KeyUp, '1'), r, alive, master, keys);
    CHECK_EQ(count(p, ActionKind::ReassertHeld), 0);   // nothing held after the release

    // Dead target is skipped.
    p = plan(key(EventKind::KeyDown, '1'), r, alive & ~target_bit(2), master, keys);
    CHECK_EQ(count(p, ActionKind::Deliver), 2);

    // Master rotates: instance 2 as master is excluded, 0 is included.
    p = plan(key(EventKind::KeyDown, '1'), r, alive, 2, keys);
    bool has0 = false, has2 = false;
    for (const auto& a : p.actions) if (a.kind == ActionKind::Deliver) { has0 |= a.target == 0; has2 |= a.target == 2; }
    CHECK(has0); CHECK(!has2);

    // PostMessage strategy: no focus dance.
    p = plan(key(EventKind::KeyDown, '2'), r, alive, master, keys);
    CHECK_EQ(count(p, ActionKind::Deliver), 1);
    CHECK_EQ(count(p, ActionKind::RestoreFocus), 0);
    CHECK(p.actions[0].strategy == Strategy::WindowMessage);

    // Swap hotkey: only on key down, always swallowed.
    p = plan(key(EventKind::KeyDown, 0x7B), r, alive, master, keys);
    CHECK_EQ(count(p, ActionKind::SwapMaster), 1);
    CHECK(p.swallowed);
    CHECK(plan(key(EventKind::KeyUp, 0x7B), r, alive, master, keys).actions.empty());

    // Mouse: gated by the modifier.
    p = plan(mouse(EventKind::MouseDown, kVkLButton, 100, 100), r, alive, master, keys);
    CHECK(p.actions.empty());
    keys.apply(EventKind::KeyDown, 0x14);
    p = plan(mouse(EventKind::MouseDown, kVkLButton, 100, 100), r, alive, master, keys);
    CHECK_EQ(count(p, ActionKind::Deliver), 3);
    CHECK_EQ(count(p, ActionKind::RestoreCursor), 1);
    CHECK_EQ(count(p, ActionKind::RestoreFocus), 1);
    CHECK_EQ(count(p, ActionKind::ReassertHeld), 1);   // CAPSLOCK is held
    // Cursor restored before focus, focus before reassert.
    int ic = -1, ifo = -1, ir = -1;
    for (int i = 0; i < static_cast<int>(p.actions.size()); ++i) {
        if (p.actions[i].kind == ActionKind::RestoreCursor) ic = i;
        if (p.actions[i].kind == ActionKind::RestoreFocus) ifo = i;
        if (p.actions[i].kind == ActionKind::ReassertHeld) ir = i;
    }
    CHECK(ic < ifo && ifo < ir);

    // Wheel follows the mouse rules; MouseMove never broadcasts.
    CHECK_EQ(count(plan(mouse(EventKind::Wheel, 0, 0, 120), r, alive, master, keys), ActionKind::Deliver), 3);
    CHECK(plan(mouse(EventKind::MouseMove, 0, 5, 5), r, alive, master, keys).actions.empty());

    // No mouse targets configured: nothing.
    Routes quiet;
    CHECK(plan(mouse(EventKind::MouseDown, kVkLButton, 1, 1), quiet, alive, master, keys).actions.empty());
}

TEST_MAIN("dispatch")
