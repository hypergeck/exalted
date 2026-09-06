#include "mm/win/window.hpp"

namespace mm::win {

SavedStyle make_borderless(HWND hwnd) {
    SavedStyle saved;
    if (!hwnd || !IsWindow(hwnd)) return saved;
    saved.style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    saved.ex_style = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    saved.rect = window_rect(hwnd);
    saved.valid = true;

    LONG_PTR style = saved.style;
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
    style |= WS_POPUP;
    LONG_PTR ex = saved.ex_style;
    ex &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE);

    SetWindowLongPtrW(hwnd, GWL_STYLE, style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    return saved;
}

void restore_style(HWND hwnd, const SavedStyle& saved) {
    if (!saved.valid || !hwnd || !IsWindow(hwnd)) return;
    SetWindowLongPtrW(hwnd, GWL_STYLE, saved.style);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, saved.ex_style);
    SetWindowPos(hwnd, nullptr, saved.rect.x, saved.rect.y, saved.rect.w, saved.rect.h,
                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

namespace {
BOOL CALLBACK monitor_proc(HMONITOR hm, HDC, LPRECT, LPARAM lp) {
    auto* out = reinterpret_cast<std::vector<Monitor>*>(lp);
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof mi;
    if (!GetMonitorInfoW(hm, &mi)) return TRUE;
    Monitor m;
    m.work_area = Rect{mi.rcWork.left, mi.rcWork.top, mi.rcWork.right - mi.rcWork.left, mi.rcWork.bottom - mi.rcWork.top};
    m.bounds = Rect{mi.rcMonitor.left, mi.rcMonitor.top, mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top};
    m.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
    if (m.primary) out->insert(out->begin(), m);
    else out->push_back(m);
    return TRUE;
}
}  // namespace

std::vector<Monitor> monitors() {
    std::vector<Monitor> out;
    EnumDisplayMonitors(nullptr, nullptr, &monitor_proc, reinterpret_cast<LPARAM>(&out));
    return out;
}

Rect window_rect(HWND hwnd) {
    RECT r{};
    if (!GetWindowRect(hwnd, &r)) return Rect{};
    return Rect{r.left, r.top, r.right - r.left, r.bottom - r.top};
}

Rect client_rect_screen(HWND hwnd) {
    RECT r{};
    if (!GetClientRect(hwnd, &r)) return Rect{};
    POINT origin{0, 0};
    ClientToScreen(hwnd, &origin);
    return Rect{origin.x, origin.y, r.right - r.left, r.bottom - r.top};
}

bool apply_layout(const std::vector<std::pair<HWND, Rect>>& placements) {
    if (placements.empty()) return true;
    HDWP hdwp = BeginDeferWindowPos(static_cast<int>(placements.size()));
    if (!hdwp) return false;
    for (const auto& [hwnd, r] : placements) {
        if (!hwnd || !IsWindow(hwnd)) continue;
        hdwp = DeferWindowPos(hdwp, hwnd, nullptr, r.x, r.y, r.w, r.h, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
        if (!hdwp) return false;
    }
    return EndDeferWindowPos(hdwp) != 0;
}

bool enable_per_monitor_dpi() {
    return SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2) != 0;
}

}  // namespace mm::win
