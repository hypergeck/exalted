#pragma once
// Tracks which keys are physically held. Used twice: on the hook thread to
// drop auto-repeat before events enter the ring, and on the broadcaster thread
// to re-assert the master's held keys after a focus cycle (Unity clears its
// key state on focus loss).
#include <array>
#include <cstddef>
#include <cstdint>

#include "mm/input_event.hpp"

namespace mm {

class KeyState {
public:
    struct Key {
        bool     down = false;
        uint16_t scan = 0;
        uint16_t flags = 0;  // EventFlags relevant for re-injection (extended key)
    };

    // Applies a key event. Returns true when it changes state; false for an
    // auto-repeat KeyDown, a KeyUp of a key that is not held, or a non-key event.
    bool apply(EventKind kind, uint16_t vk, uint16_t scan = 0, uint16_t flags = 0) noexcept {
        if (vk >= keys_.size()) return false;
        Key& k = keys_[vk];
        if (kind == EventKind::KeyDown) {
            if (k.down) return false;
            k.down = true;
            k.scan = scan;
            k.flags = flags;
            ++held_;
            return true;
        }
        if (kind == EventKind::KeyUp) {
            if (!k.down) return false;
            k.down = false;
            --held_;
            return true;
        }
        return false;
    }

    bool apply(const InputEvent& ev) noexcept { return apply(ev.kind, ev.vk, ev.scan, ev.flags); }

    bool is_down(uint16_t vk) const noexcept { return vk < keys_.size() && keys_[vk].down; }
    std::size_t held_count() const noexcept { return held_; }

    template <class F>
    void for_each_down(F&& f) const {
        for (std::size_t vk = 0; vk < keys_.size(); ++vk) {
            if (keys_[vk].down) f(static_cast<uint16_t>(vk), keys_[vk]);
        }
    }

    void clear() noexcept {
        keys_.fill(Key{});
        held_ = 0;
    }

private:
    std::array<Key, 256> keys_{};
    std::size_t held_ = 0;
};

}  // namespace mm
