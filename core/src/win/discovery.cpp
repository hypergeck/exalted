#include "mm/win/discovery.hpp"

namespace mm::win {

namespace {
struct EnumCtx {
    DWORD pid;
    std::vector<WindowInfo>* out;
};

BOOL CALLBACK enum_proc(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<EnumCtx*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (ctx->pid != 0 && pid != ctx->pid) return TRUE;
    if (!IsWindowVisible(h)) return TRUE;
    if (GetWindow(h, GW_OWNER) != nullptr) return TRUE;

    WindowInfo info;
    info.hwnd = h;
    info.pid = pid;
    wchar_t buf[256];
    if (GetClassNameW(h, buf, 256) > 0) info.class_name = buf;
    if (GetWindowTextW(h, buf, 256) > 0) info.title = buf;
    RECT r{};
    if (GetWindowRect(h, &r)) info.rect = Rect{r.left, r.top, r.right - r.left, r.bottom - r.top};
    ctx->out->push_back(std::move(info));
    return TRUE;
}
}  // namespace

std::vector<WindowInfo> top_level_windows(DWORD pid) {
    std::vector<WindowInfo> out;
    EnumCtx ctx{pid, &out};
    EnumWindows(&enum_proc, reinterpret_cast<LPARAM>(&ctx));
    return out;
}

HWND find_main_window(DWORD pid, const wchar_t* class_hint) {
    const auto windows = top_level_windows(pid);
    if (class_hint && *class_hint) {
        for (const auto& w : windows) if (w.class_name == class_hint) return w.hwnd;
    }
    HWND best = nullptr;
    int64_t best_area = 0;
    for (const auto& w : windows) {
        const int64_t area = static_cast<int64_t>(w.rect.w) * w.rect.h;
        if (area > best_area) { best_area = area; best = w.hwnd; }
    }
    return best;
}

HWND wait_for_main_window(DWORD pid, const wchar_t* class_hint, unsigned timeout_ms, HANDLE process) {
    const uint64_t deadline = qpc_now() + qpc_frequency() * timeout_ms / 1000;
    for (;;) {
        if (HWND h = find_main_window(pid, class_hint)) return h;
        if (process && WaitForSingleObject(process, 0) == WAIT_OBJECT_0) return nullptr;
        if (qpc_now() >= deadline) return nullptr;
        Sleep(100);
    }
}

}  // namespace mm::win
