#pragma once
// Synthetic input. Two channels:
//   send_*  -> SendInput: real system input, delivered to the foreground window.
//   post_*  -> PostMessage: window messages to a specific HWND, no focus needed,
//              but Unity's Raw-Input-based gameplay keys usually ignore them.
#include <cstdint>

#include "mm/input_event.hpp"
#include "mm/geometry.hpp"
#include "mm/win/win_common.hpp"

namespace mm::win {

// Scan-code based key event (KEYEVENTF_SCANCODE), which is what Unity reads.
bool send_key(uint16_t vk, uint16_t scan, bool down, uint16_t flags);
bool send_mouse_button(uint16_t vk, bool down);
bool send_mouse_move_abs(AbsPoint p);              // MOUSEEVENTF_ABSOLUTE | VIRTUALDESK
bool send_wheel(int32_t vertical, int32_t horizontal);

bool post_key(HWND hwnd, uint16_t vk, uint16_t scan, bool down, uint16_t flags);
bool post_mouse_button(HWND hwnd, uint16_t vk, bool down, int32_t client_x, int32_t client_y);
bool post_wheel(HWND hwnd, int32_t vertical, int32_t horizontal, int32_t screen_x, int32_t screen_y);

Rect virtual_screen();

}  // namespace mm::win
