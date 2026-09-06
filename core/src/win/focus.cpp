#include "mm/win/focus.hpp"

namespace mm::win {

namespace {
bool wait_until_foreground(HWND target, unsigned wait_ms) {
    const uint64_t deadline = qpc_now() + qpc_frequency() * wait_ms / 1000;
    do {
        if (GetForegroundWindow() == target) return true;
        SwitchToThread();
    } while (qpc_now() < deadline);
    return GetForegroundWindow() == target;
}
}  // namespace

bool bring_to_foreground(HWND target, unsigned wait_ms) {
    if (!target || !IsWindow(target)) return false;
    if (GetForegroundWindow() == target) return true;
    if (IsIconic(target)) ShowWindow(target, SW_RESTORE);

    const HWND fg = GetForegroundWindow();
    const DWORD me = GetCurrentThreadId();
    const DWORD fg_tid = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    const bool attached = fg_tid != 0 && fg_tid != me && AttachThreadInput(me, fg_tid, TRUE);
    SetForegroundWindow(target);
    if (attached) AttachThreadInput(me, fg_tid, FALSE);
    if (wait_until_foreground(target, wait_ms)) return true;

    // Last resort: bypasses the foreground lock outright.
    SwitchToThisWindow(target, TRUE);
    return wait_until_foreground(target, wait_ms);
}

ForegroundLockGuard::ForegroundLockGuard() {
    if (SystemParametersInfoW(SPI_GETFOREGROUNDLOCKTIMEOUT, 0, &previous_, 0)) {
        applied_ = SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, reinterpret_cast<PVOID>(0), SPIF_SENDCHANGE) != 0;
    }
}

ForegroundLockGuard::~ForegroundLockGuard() {
    if (applied_) {
        SystemParametersInfoW(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, reinterpret_cast<PVOID>(static_cast<UINT_PTR>(previous_)), SPIF_SENDCHANGE);
    }
}

}  // namespace mm::win
