#include "mm/win/capture.hpp"

namespace mm::win {

namespace {
Capture* g_capture = nullptr;  // hooks are process-global; one Capture per process
}

Capture::Capture(EventRing& ring, HANDLE wake_event) : ring_(ring), wake_(wake_event) {}

Capture::~Capture() { stop(); }

bool Capture::start(std::string* error) {
    if (running()) return true;
    if (g_capture != nullptr) {
        if (error) *error = "another Capture is already active in this process";
        return false;
    }
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    thread_ = std::thread([this, ready] { run(ready); });
    WaitForSingleObject(ready, INFINITE);
    CloseHandle(ready);
    if (!started_ok_) {
        thread_.join();
        if (error) *error = start_error_;
    }
    return started_ok_;
}

void Capture::stop() {
    if (!running()) return;
    PostThreadMessageW(tid_, WM_QUIT, 0, 0);
    thread_.join();
}

void Capture::set_swallow(uint16_t vk, bool on) noexcept {
    if (vk < swallow_.size()) swallow_[vk].store(on, std::memory_order_relaxed);
}

void Capture::clear_swallow() noexcept {
    for (auto& s : swallow_) s.store(false, std::memory_order_relaxed);
}

Capture::Stats Capture::stats() const noexcept {
    return Stats{captured_.load(std::memory_order_relaxed), dropped_.load(std::memory_order_relaxed),
                 injected_seen_.load(std::memory_order_relaxed)};
}

void Capture::run(HANDLE ready) {
    tid_ = GetCurrentThreadId();
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    // Force creation of this thread's message queue so PostThreadMessage from
    // stop() cannot race ahead of our first GetMessage.
    MSG msg;
    PeekMessageW(&msg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    g_capture = this;
    const HINSTANCE self = GetModuleHandleW(nullptr);
    kb_hook_ = SetWindowsHookExW(WH_KEYBOARD_LL, &Capture::kb_proc, self, 0);
    if (!kb_hook_) start_error_ = "SetWindowsHookEx(WH_KEYBOARD_LL): " + last_error_message();
    mouse_hook_ = kb_hook_ ? SetWindowsHookExW(WH_MOUSE_LL, &Capture::mouse_proc, self, 0) : nullptr;
    if (kb_hook_ && !mouse_hook_) start_error_ = "SetWindowsHookEx(WH_MOUSE_LL): " + last_error_message();
    started_ok_ = kb_hook_ != nullptr && mouse_hook_ != nullptr;
    SetEvent(ready);

    if (started_ok_) {
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (mouse_hook_) UnhookWindowsHookEx(mouse_hook_);
    if (kb_hook_) UnhookWindowsHookEx(kb_hook_);
    mouse_hook_ = kb_hook_ = nullptr;
    g_capture = nullptr;
}

LRESULT CALLBACK Capture::kb_proc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && g_capture) {
        const auto* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(l);
        if (g_capture->on_key(w, *k)) return 1;  // swallow: the foreground app never sees it
    }
    return CallNextHookEx(nullptr, code, w, l);
}

LRESULT CALLBACK Capture::mouse_proc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION && g_capture) {
        const auto* m = reinterpret_cast<const MSLLHOOKSTRUCT*>(l);
        if (g_capture->on_mouse(w, *m)) return 1;
    }
    return CallNextHookEx(nullptr, code, w, l);
}

void Capture::publish(const InputEvent& ev) noexcept {
    if (ring_.push(ev)) captured_.fetch_add(1, std::memory_order_relaxed);
    else dropped_.fetch_add(1, std::memory_order_relaxed);
    SetEvent(wake_);
}

bool Capture::on_key(WPARAM msg, const KBDLLHOOKSTRUCT& k) noexcept {
    if (k.dwExtraInfo == kInjectMagic ||
        ((k.flags & LLKHF_INJECTED) && !accept_foreign_.load(std::memory_order_relaxed))) {
        injected_seen_.fetch_add(1, std::memory_order_relaxed);
        return false;  // never loop our own output back, never block it
    }
    const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
    const auto vk = static_cast<uint16_t>(k.vkCode & 0xFF);
    uint16_t flags = 0;
    if (k.flags & LLKHF_EXTENDED) flags |= kExtendedKey;
    if (k.flags & LLKHF_ALTDOWN) flags |= kSysKey;

    const bool swallow = swallow_[vk].load(std::memory_order_relaxed);
    if (!keys_.apply(down ? EventKind::KeyDown : EventKind::KeyUp, vk, static_cast<uint16_t>(k.scanCode), flags)) {
        return swallow;  // auto-repeat (or stray key-up): not forwarded, still blocked if it is a hotkey
    }

    InputEvent ev{};
    ev.ts = qpc_now();
    ev.kind = down ? EventKind::KeyDown : EventKind::KeyUp;
    ev.vk = vk;
    ev.scan = static_cast<uint16_t>(k.scanCode);
    ev.flags = flags;
    publish(ev);
    return swallow;
}

bool Capture::on_mouse(WPARAM msg, const MSLLHOOKSTRUCT& m) noexcept {
    if (m.dwExtraInfo == kInjectMagic ||
        ((m.flags & LLMHF_INJECTED) && !accept_foreign_.load(std::memory_order_relaxed))) {
        injected_seen_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    InputEvent ev{};
    ev.ts = qpc_now();
    ev.x = m.pt.x;
    ev.y = m.pt.y;
    const auto hi = static_cast<int16_t>(HIWORD(m.mouseData));

    switch (msg) {
    case WM_LBUTTONDOWN: ev.kind = EventKind::MouseDown; ev.vk = kVkLButton; break;
    case WM_LBUTTONUP:   ev.kind = EventKind::MouseUp;   ev.vk = kVkLButton; break;
    case WM_RBUTTONDOWN: ev.kind = EventKind::MouseDown; ev.vk = kVkRButton; break;
    case WM_RBUTTONUP:   ev.kind = EventKind::MouseUp;   ev.vk = kVkRButton; break;
    case WM_MBUTTONDOWN: ev.kind = EventKind::MouseDown; ev.vk = kVkMButton; break;
    case WM_MBUTTONUP:   ev.kind = EventKind::MouseUp;   ev.vk = kVkMButton; break;
    case WM_XBUTTONDOWN: ev.kind = EventKind::MouseDown; ev.vk = hi == XBUTTON1 ? kVkXButton1 : kVkXButton2; break;
    case WM_XBUTTONUP:   ev.kind = EventKind::MouseUp;   ev.vk = hi == XBUTTON1 ? kVkXButton1 : kVkXButton2; break;
    case WM_MOUSEWHEEL:  ev.kind = EventKind::Wheel; ev.x = 0; ev.y = hi; break;
    case WM_MOUSEHWHEEL: ev.kind = EventKind::Wheel; ev.x = hi; ev.y = 0; break;
    case WM_MOUSEMOVE:
        if (!capture_moves_.load(std::memory_order_relaxed)) return false;
        ev.kind = EventKind::MouseMove;
        break;
    default:
        return false;
    }
    publish(ev);
    return false;  // mouse is never swallowed
}

}  // namespace mm::win
