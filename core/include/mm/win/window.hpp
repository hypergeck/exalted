#pragma once
// Window styling and placement: borderless conversion, monitor work areas,
// atomic multi-window layout via DeferWindowPos, client-rect queries.
#include <utility>
#include <vector>

#include "mm/geometry.hpp"
#include "mm/win/win_common.hpp"

namespace mm::win {

struct SavedStyle {
    LONG_PTR style = 0;
    LONG_PTR ex_style = 0;
    Rect rect;
    bool valid = false;
};

// Strips caption/frame so the client area is the whole window.
SavedStyle make_borderless(HWND hwnd);
void restore_style(HWND hwnd, const SavedStyle& saved);

struct Monitor {
    Rect work_area;   // excludes the taskbar
    Rect bounds;
    bool primary = false;
};
std::vector<Monitor> monitors();  // primary first

Rect window_rect(HWND hwnd);
Rect client_rect_screen(HWND hwnd);  // client area in screen coordinates

// Moves/resizes every window in one deferred batch (single repaint).
bool apply_layout(const std::vector<std::pair<HWND, Rect>>& placements);

// Opt this process into per-monitor DPI v2 so coordinates are physical pixels.
bool enable_per_monitor_dpi();

}  // namespace mm::win
