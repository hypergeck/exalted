#include "check.hpp"
#include "mm/key_state.hpp"

using namespace mm;

static void run_tests() {
    KeyState ks;
    CHECK(ks.apply(EventKind::KeyDown, 'W', 0x11, 0));
    CHECK(!ks.apply(EventKind::KeyDown, 'W', 0x11, 0));   // auto-repeat is dropped
    CHECK(ks.is_down('W'));
    CHECK_EQ(ks.held_count(), std::size_t{1});
    CHECK(ks.apply(EventKind::KeyDown, 0x25, 0x4B, kExtendedKey));  // LEFT arrow, extended
    CHECK_EQ(ks.held_count(), std::size_t{2});

    int seen = 0; uint16_t ext_flags = 0;
    ks.for_each_down([&](uint16_t vk, const KeyState::Key& k) { ++seen; if (vk == 0x25) ext_flags = k.flags; });
    CHECK_EQ(seen, 2);
    CHECK_EQ(ext_flags, uint16_t{kExtendedKey});

    CHECK(ks.apply(EventKind::KeyUp, 'W'));
    CHECK(!ks.apply(EventKind::KeyUp, 'W'));              // up without down is a no-op
    CHECK(!ks.is_down('W'));
    CHECK(!ks.apply(EventKind::MouseDown, kVkLButton));   // mouse buttons are not keys
    CHECK(!ks.apply(EventKind::KeyDown, 300));            // out of range

    ks.clear();
    CHECK_EQ(ks.held_count(), std::size_t{0});
    CHECK(!ks.is_down(0x25));
}

TEST_MAIN("key_state")
