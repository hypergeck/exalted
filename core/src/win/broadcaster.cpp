#include "mm/win/broadcaster.hpp"

#include "mm/geometry.hpp"
#include "mm/win/focus.hpp"
#include "mm/win/inject.hpp"
#include "mm/win/window.hpp"

namespace mm::win {

Broadcaster::Broadcaster(EventRing& ring, HANDLE wake_event)
    : ring_(ring), wake_(wake_event), pending_routes_(std::make_shared<Routes>()), routes_(pending_routes_) {}

Broadcaster::~Broadcaster() { stop(); }

void Broadcaster::set_routes(std::shared_ptr<const Routes> routes) {
    std::lock_guard lock(config_mu_);
    pending_routes_ = routes ? std::move(routes) : std::make_shared<Routes>();
    config_version_.fetch_add(1, std::memory_order_release);
}

void Broadcaster::set_targets(std::vector<Target> targets, uint32_t master) {
    std::lock_guard lock(config_mu_);
    pending_targets_ = std::move(targets);
    pending_master_ = master;
    config_version_.fetch_add(1, std::memory_order_release);
}

void Broadcaster::set_on_swap(std::function<void()> cb) {
    std::lock_guard lock(config_mu_);
    on_swap_ = std::move(cb);
}

bool Broadcaster::start() {
    if (thread_.joinable()) return true;
    stop_.store(false);
    thread_ = std::thread([this] { run(); });
    return true;
}

bool Broadcaster::stop(unsigned join_timeout_ms) {
    if (!thread_.joinable()) return true;
    stop_.store(true);
    SetEvent(wake_);
    if (join_timeout_ms == 0 ||
        WaitForSingleObject(reinterpret_cast<HANDLE>(thread_.native_handle()), join_timeout_ms) == WAIT_OBJECT_0) {
        thread_.join();
        return true;
    }
    thread_.detach();  // stuck inside a focus/inject call; the caller decides what to do
    return false;
}

Broadcaster::Snapshot Broadcaster::snapshot() const {
    std::lock_guard lock(stats_mu_);
    return stats_;
}

void Broadcaster::reset_stats() {
    std::lock_guard lock(stats_mu_);
    stats_ = Snapshot{};
}

void Broadcaster::run() {
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    while (!stop_.load(std::memory_order_relaxed)) {
        WaitForSingleObject(wake_, 20);
        refresh_config();
        InputEvent ev{};
        while (ring_.pop(ev)) handle(ev);
    }
}

void Broadcaster::refresh_config() {
    const uint64_t v = config_version_.load(std::memory_order_acquire);
    if (v == seen_version_) return;
    std::lock_guard lock(config_mu_);
    routes_ = pending_routes_;
    targets_ = pending_targets_;
    master_ = pending_master_;
    seen_version_ = v;
}

void Broadcaster::handle(const InputEvent& ev) {
    master_keys_.apply(ev);  // no-op for mouse events; mirrors the physical keyboard
    if (targets_.empty()) return;

    TargetMask alive = 0;
    for (uint32_t i = 0; i < targets_.size() && i < 32; ++i) {
        if (targets_[i].hwnd && IsWindow(targets_[i].hwnd)) alive |= target_bit(i);
    }
    const Plan p = plan(ev, *routes_, alive, master_, master_keys_);
    if (!p.actions.empty()) execute(p, ev);
}

void Broadcaster::execute(const Plan& p, const InputEvent& ev) {
    for (const Action& a : p.actions) {
        switch (a.kind) {
        case ActionKind::Deliver:
            deliver(a, ev);
            break;
        case ActionKind::RestoreFocus:
            if (master_ < targets_.size() && !bring_to_foreground(targets_[master_].hwnd, focus_wait_ms_.load(std::memory_order_relaxed))) {
                std::lock_guard lock(stats_mu_);
                ++stats_.focus_failures;
            }
            break;
        case ActionKind::RestoreCursor:
            send_mouse_move_abs(to_absolute(virtual_screen(), ev.x, ev.y));
            break;
        case ActionKind::ReassertHeld:
            reassert_held();
            break;
        case ActionKind::SwapMaster: {
            std::function<void()> cb;
            {
                std::lock_guard lock(config_mu_);
                cb = on_swap_;
            }
            if (cb) cb();
            refresh_config();
            break;
        }
        }
    }
}

void Broadcaster::deliver(const Action& a, const InputEvent& ev) {
    if (a.target >= targets_.size()) return;
    const HWND hwnd = targets_[a.target].hwnd;
    bool ok = true;

    if (a.strategy == Strategy::WindowMessage) {
        if (is_key(ev.kind)) {
            ok = post_key(hwnd, ev.vk, ev.scan, ev.kind == EventKind::KeyDown, ev.flags);
        } else {
            const Rect master_client = master_ < targets_.size() ? client_rect_screen(targets_[master_].hwnd) : Rect{};
            const Rect target_client = client_rect_screen(hwnd);
            const Point p = denormalize(target_client, normalize(master_client, ev.x, ev.y));
            if (is_mouse_button(ev.kind)) {
                ok = post_mouse_button(hwnd, ev.vk, ev.kind == EventKind::MouseDown, p.x - target_client.x, p.y - target_client.y);
            } else if (ev.kind == EventKind::Wheel) {
                ok = post_wheel(hwnd, ev.y, ev.x, p.x, p.y);
            }
        }
    } else {
        if (!bring_to_foreground(hwnd, focus_wait_ms_.load(std::memory_order_relaxed))) {
            std::lock_guard lock(stats_mu_);
            ++stats_.focus_failures;
            return;  // injecting now would hit whatever is foreground instead
        }
        if (is_key(ev.kind)) {
            ok = send_key(ev.vk, ev.scan, ev.kind == EventKind::KeyDown, ev.flags);
        } else {
            const Rect master_client = master_ < targets_.size() ? client_rect_screen(targets_[master_].hwnd) : Rect{};
            const Point p = denormalize(client_rect_screen(hwnd), normalize(master_client, ev.x, ev.y));
            ok = send_mouse_move_abs(to_absolute(virtual_screen(), p.x, p.y));
            const unsigned settle = click_settle_ms_.load(std::memory_order_relaxed);
            if (settle) Sleep(settle);
            if (is_mouse_button(ev.kind)) ok &= send_mouse_button(ev.vk, ev.kind == EventKind::MouseDown);
            else if (ev.kind == EventKind::Wheel) ok &= send_wheel(ev.y, ev.x);
        }
    }

    const uint32_t us = qpc_to_us(qpc_now() - ev.ts);
    std::lock_guard lock(stats_mu_);
    stats_.latency.record(us);
    ++stats_.deliveries;
    if (!ok) ++stats_.inject_failures;
}

void Broadcaster::reassert_held() {
    master_keys_.for_each_down([](uint16_t vk, const KeyState::Key& k) { send_key(vk, k.scan, true, k.flags); });
}

}  // namespace mm::win
