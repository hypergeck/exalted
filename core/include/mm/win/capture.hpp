#pragma once
// Global input capture: WH_KEYBOARD_LL + WH_MOUSE_LL on a dedicated
// message-pump thread. The hook callbacks do four things only: filter, stamp,
// push into the ring, wake the consumer. Anything slower risks the 300 ms
// LowLevelHooksTimeout after which Windows silently unhooks us.
#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "mm/input_event.hpp"
#include "mm/key_state.hpp"
#include "mm/spsc_ring.hpp"
#include "mm/win/win_common.hpp"

namespace mm::win {

// Stamped into dwExtraInfo of everything we inject, so the hook can drop our
// own events even when a driver strips the LLKHF_INJECTED flag.
constexpr ULONG_PTR kInjectMagic = 0x4D4D0001;  // "MM", v1

using EventRing = SpscRing<InputEvent, 4096>;

class Capture {
public:
    Capture(EventRing& ring, HANDLE wake_event);
    ~Capture();
    Capture(const Capture&) = delete;
    Capture& operator=(const Capture&) = delete;

    // Installs the hooks. Returns false (with `error` set) if either hook failed.
    bool start(std::string* error);
    void stop();
    bool running() const noexcept { return thread_.joinable(); }

    // Keys the master must never see (hotkeys). Thread-safe.
    void set_swallow(uint16_t vk, bool on) noexcept;
    void clear_swallow() noexcept;

    // Off by default: mouse moves are the noisiest event on the system and
    // are never broadcast.
    void set_capture_mouse_moves(bool on) noexcept { capture_moves_.store(on, std::memory_order_relaxed); }

    struct Stats {
        uint64_t captured = 0;
        uint64_t dropped = 0;        // ring was full
        uint64_t injected_seen = 0;  // our own or other software's injected events, ignored
    };
    Stats stats() const noexcept;

private:
    void run(HANDLE ready);
    bool on_key(WPARAM msg, const KBDLLHOOKSTRUCT& k) noexcept;
    bool on_mouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept;
    void publish(const InputEvent& ev) noexcept;

    static LRESULT CALLBACK kb_proc(int code, WPARAM w, LPARAM l);
    static LRESULT CALLBACK mouse_proc(int code, WPARAM w, LPARAM l);

    EventRing& ring_;
    HANDLE wake_;
    std::thread thread_;
    DWORD tid_ = 0;
    HHOOK kb_hook_ = nullptr;
    HHOOK mouse_hook_ = nullptr;
    bool started_ok_ = false;
    std::string start_error_;

    KeyState keys_;  // hook-thread only: auto-repeat filter
    std::array<std::atomic<bool>, 256> swallow_{};
    std::atomic<bool> capture_moves_{false};
    std::atomic<uint64_t> captured_{0}, dropped_{0}, injected_seen_{0};
};

}  // namespace mm::win
