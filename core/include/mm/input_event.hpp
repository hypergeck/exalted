#pragma once
// Portable input event ABI. Shared verbatim between the capture thread, the
// broadcaster, and (in M3) the shared-memory telemetry ring, so it must stay
// a 32-byte trivially-copyable POD. No <windows.h> here.
#include <cstdint>
#include <type_traits>

namespace mm {

enum class EventKind : uint16_t {
    None      = 0,
    KeyDown   = 1,
    KeyUp     = 2,
    MouseMove = 3,
    MouseDown = 4,
    MouseUp   = 5,
    Wheel     = 6,
};

// Bits carried in InputEvent::flags.
enum EventFlags : uint16_t {
    kExtendedKey = 1u << 0,  // E0-prefixed scan code (arrows, right ctrl, numpad enter, ...)
    kInjected    = 1u << 1,  // OS marked the event as injected by software
    kOurs        = 1u << 2,  // injected by MultiMadness itself (magic extra-info matched)
    kSysKey      = 1u << 3,  // WM_SYSKEY* (Alt held while the key was pressed)
};

// Mouse button identifiers, numerically equal to the Win32 VK_* values so the
// two layers agree without including the SDK.
enum MouseVk : uint16_t {
    kVkLButton  = 0x01,
    kVkRButton  = 0x02,
    kVkMButton  = 0x04,
    kVkXButton1 = 0x05,
    kVkXButton2 = 0x06,
};

struct InputEvent {
    uint64_t  ts;        // monotonic capture timestamp (QueryPerformanceCounter ticks on Windows)
    EventKind kind;
    uint16_t  vk;        // virtual key, or MouseVk for button events
    uint16_t  scan;      // hardware scan code, 0 if unknown
    uint16_t  flags;     // EventFlags
    int32_t   x;         // screen coords for mouse events; horizontal wheel delta for Wheel
    int32_t   y;         // screen coords for mouse events; vertical wheel delta for Wheel
    uint32_t  source;    // raw-input device id, 0 if unknown
    uint32_t  reserved;
};

static_assert(sizeof(InputEvent) == 32, "InputEvent is a shared-memory ABI; keep it at 32 bytes");
static_assert(std::is_trivially_copyable_v<InputEvent>);

constexpr bool is_key(EventKind k) noexcept { return k == EventKind::KeyDown || k == EventKind::KeyUp; }
constexpr bool is_mouse_button(EventKind k) noexcept { return k == EventKind::MouseDown || k == EventKind::MouseUp; }

}  // namespace mm
