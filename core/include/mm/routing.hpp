#pragma once
// Immutable routing table: which captured keys go to which target instances,
// via which delivery strategy. Built off the hot path, then published as a
// shared_ptr<const Routes> that the broadcaster swaps in between events.
#include <array>
#include <cstdint>
#include <vector>

namespace mm {

enum class Strategy : uint8_t {
    FocusCycle  = 0,  // SetForegroundWindow + SendInput (works for Unity; costs a focus switch per target)
    WindowMessage = 1,  // PostMessage to the HWND: no focus change, but Unity usually ignores it for gameplay keys
                        // (not named PostMessage because the Win32 macro would rewrite the enumerator)
};

using TargetMask = uint32_t;                     // bit i = instance i (max 32 instances)
constexpr TargetMask kAllTargets = ~TargetMask{0};

constexpr TargetMask target_bit(uint32_t index) noexcept { return TargetMask{1} << index; }

struct KeyBinding {
    uint16_t   vk = 0;
    TargetMask targets = kAllTargets;
    Strategy   strategy = Strategy::FocusCycle;
    bool       swallow = false;                  // true: the master never sees the key (hotkeys)
};

class Routes {
public:
    // Adds or replaces the binding for `b.vk`.
    void add_key(const KeyBinding& b) {
        if (b.vk >= index_.size()) return;
        if (index_[b.vk] >= 0) {
            keys_[static_cast<std::size_t>(index_[b.vk])] = b;
        } else {
            index_[b.vk] = static_cast<int16_t>(keys_.size());
            keys_.push_back(b);
        }
    }

    const KeyBinding* find(uint16_t vk) const noexcept {
        if (vk >= index_.size() || index_[vk] < 0) return nullptr;
        return &keys_[static_cast<std::size_t>(index_[vk])];
    }

    bool swallows(uint16_t vk) const noexcept {
        if (vk == swap_hotkey_vk && vk != 0) return true;
        const KeyBinding* b = find(vk);
        return b != nullptr && b->swallow;
    }

    const std::vector<KeyBinding>& keys() const noexcept { return keys_; }

    // Mouse buttons and wheel are broadcast only when `mouse_targets` is
    // non-zero and (if set) `mouse_modifier_vk` is held on the master.
    TargetMask mouse_targets = 0;
    uint16_t   mouse_modifier_vk = 0;
    Strategy   mouse_strategy = Strategy::FocusCycle;

    // Pressing this key rotates the master instance. Always swallowed.
    uint16_t swap_hotkey_vk = 0;

private:
    std::vector<KeyBinding> keys_;
    std::array<int16_t, 256> index_ = filled(-1);

    static std::array<int16_t, 256> filled(int16_t v) {
        std::array<int16_t, 256> a{};
        a.fill(v);
        return a;
    }
};

}  // namespace mm
