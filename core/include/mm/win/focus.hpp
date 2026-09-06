#pragma once
// Foreground control. Windows refuses SetForegroundWindow from a process that
// did not receive the last input; these helpers apply the standard workarounds
// and always verify the result instead of assuming it.
#include "mm/win/win_common.hpp"

namespace mm::win {

// Returns true once `target` is the foreground window (waits up to wait_ms).
bool bring_to_foreground(HWND target, unsigned wait_ms = 5);

// Sets SPI_SETFOREGROUNDLOCKTIMEOUT to 0 for the lifetime of the object and
// restores the user's previous value on destruction.
class ForegroundLockGuard {
public:
    ForegroundLockGuard();
    ~ForegroundLockGuard();
    ForegroundLockGuard(const ForegroundLockGuard&) = delete;
    ForegroundLockGuard& operator=(const ForegroundLockGuard&) = delete;

private:
    DWORD previous_ = 0;
    bool applied_ = false;
};

}  // namespace mm::win
