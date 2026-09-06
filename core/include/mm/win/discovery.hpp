#pragma once
// Finds the game's top-level window for a PID. Unity standalone players use
// the window class "UnityWndClass"; verify on the current Exalt build.
#include <string>
#include <vector>

#include "mm/geometry.hpp"
#include "mm/win/win_common.hpp"

namespace mm::win {

struct WindowInfo {
    HWND hwnd = nullptr;
    DWORD pid = 0;
    std::wstring class_name;
    std::wstring title;
    Rect rect;
};

// Visible, unowned top-level windows; pid = 0 lists every process.
std::vector<WindowInfo> top_level_windows(DWORD pid = 0);

// Best candidate for the main window: class match first, else the largest.
HWND find_main_window(DWORD pid, const wchar_t* class_hint);

// Polls until a main window appears, the timeout elapses, or (if `process`
// is given) the process exits.
HWND wait_for_main_window(DWORD pid, const wchar_t* class_hint, unsigned timeout_ms, HANDLE process = nullptr);

}  // namespace mm::win
