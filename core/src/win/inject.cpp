#include "mm/win/inject.hpp"

#include "mm/win/capture.hpp"  // kInjectMagic

namespace mm::win {

namespace {
bool send_one(INPUT& in) {
    in.mi.dwExtraInfo = kInjectMagic;  // mouse-only helper: MOUSEINPUT and KEYBDINPUT place dwExtraInfo at different offsets
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}
}  // namespace

bool send_key(uint16_t vk, uint16_t scan, bool down, uint16_t flags) {
    if (scan == 0) scan = static_cast<uint16_t>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    INPUT in{};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = 0;
    in.ki.wScan = scan;
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (down ? 0 : KEYEVENTF_KEYUP) | ((flags & kExtendedKey) ? KEYEVENTF_EXTENDEDKEY : 0);
    in.ki.dwExtraInfo = kInjectMagic;
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}

bool send_mouse_button(uint16_t vk, bool down) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    switch (vk) {
    case kVkLButton:  in.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP; break;
    case kVkRButton:  in.mi.dwFlags = down ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP; break;
    case kVkMButton:  in.mi.dwFlags = down ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP; break;
    case kVkXButton1: in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON1; break;
    case kVkXButton2: in.mi.dwFlags = down ? MOUSEEVENTF_XDOWN : MOUSEEVENTF_XUP; in.mi.mouseData = XBUTTON2; break;
    default: return false;
    }
    in.mi.dwExtraInfo = kInjectMagic;
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}

bool send_mouse_move_abs(AbsPoint p) {
    INPUT in{};
    in.type = INPUT_MOUSE;
    in.mi.dx = p.ax;
    in.mi.dy = p.ay;
    in.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
    in.mi.dwExtraInfo = kInjectMagic;
    return SendInput(1, &in, sizeof(INPUT)) == 1;
}

bool send_wheel(int32_t vertical, int32_t horizontal) {
    bool ok = true;
    if (vertical != 0) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_WHEEL;
        in.mi.mouseData = static_cast<DWORD>(vertical);
        ok &= send_one(in);
    }
    if (horizontal != 0) {
        INPUT in{};
        in.type = INPUT_MOUSE;
        in.mi.dwFlags = MOUSEEVENTF_HWHEEL;
        in.mi.mouseData = static_cast<DWORD>(horizontal);
        ok &= send_one(in);
    }
    return ok;
}

bool post_key(HWND hwnd, uint16_t vk, uint16_t scan, bool down, uint16_t flags) {
    if (scan == 0) scan = static_cast<uint16_t>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
    LPARAM lp = 1;                                              // repeat count
    lp |= static_cast<LPARAM>(scan & 0xFF) << 16;
    if (flags & kExtendedKey) lp |= LPARAM{1} << 24;
    if (flags & kSysKey) lp |= LPARAM{1} << 29;
    if (!down) lp |= (LPARAM{1} << 30) | (LPARAM{1} << 31);   // previous state + transition
    const UINT msg = (flags & kSysKey) ? (down ? WM_SYSKEYDOWN : WM_SYSKEYUP) : (down ? WM_KEYDOWN : WM_KEYUP);
    return PostMessageW(hwnd, msg, vk, lp) != 0;
}

bool post_mouse_button(HWND hwnd, uint16_t vk, bool down, int32_t client_x, int32_t client_y) {
    UINT msg = 0;
    WPARAM wp = 0;
    switch (vk) {
    case kVkLButton:  msg = down ? WM_LBUTTONDOWN : WM_LBUTTONUP; wp = down ? MK_LBUTTON : 0; break;
    case kVkRButton:  msg = down ? WM_RBUTTONDOWN : WM_RBUTTONUP; wp = down ? MK_RBUTTON : 0; break;
    case kVkMButton:  msg = down ? WM_MBUTTONDOWN : WM_MBUTTONUP; wp = down ? MK_MBUTTON : 0; break;
    case kVkXButton1: msg = down ? WM_XBUTTONDOWN : WM_XBUTTONUP; wp = MAKEWPARAM(down ? MK_XBUTTON1 : 0, XBUTTON1); break;
    case kVkXButton2: msg = down ? WM_XBUTTONDOWN : WM_XBUTTONUP; wp = MAKEWPARAM(down ? MK_XBUTTON2 : 0, XBUTTON2); break;
    default: return false;
    }
    const LPARAM lp = MAKELPARAM(static_cast<WORD>(client_x), static_cast<WORD>(client_y));
    return PostMessageW(hwnd, msg, wp, lp) != 0;
}

bool post_wheel(HWND hwnd, int32_t vertical, int32_t horizontal, int32_t screen_x, int32_t screen_y) {
    const LPARAM lp = MAKELPARAM(static_cast<WORD>(screen_x), static_cast<WORD>(screen_y));
    bool ok = true;
    if (vertical != 0) ok &= PostMessageW(hwnd, WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<int16_t>(vertical))), lp) != 0;
    if (horizontal != 0) ok &= PostMessageW(hwnd, WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(static_cast<int16_t>(horizontal))), lp) != 0;
    return ok;
}

Rect virtual_screen() {
    return Rect{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_CXVIRTUALSCREEN), GetSystemMetrics(SM_CYVIRTUALSCREEN)};
}

}  // namespace mm::win
