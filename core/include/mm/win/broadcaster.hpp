#pragma once
// Consumer of the capture ring. Plans each event with mm::plan() and executes
// the plan against real windows. Configuration (routes, targets, master) is
// published from other threads through a versioned snapshot; the hot loop
// only takes the lock when the version changes.
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "mm/dispatch.hpp"
#include "mm/latency_stats.hpp"
#include "mm/routing.hpp"
#include "mm/win/capture.hpp"

namespace mm::win {

struct Target {
    HWND hwnd = nullptr;
};

class Broadcaster {
public:
    Broadcaster(EventRing& ring, HANDLE wake_event);
    ~Broadcaster();
    Broadcaster(const Broadcaster&) = delete;
    Broadcaster& operator=(const Broadcaster&) = delete;

    void set_routes(std::shared_ptr<const Routes> routes);
    void set_targets(std::vector<Target> targets, uint32_t master);

    // Milliseconds to wait between moving the cursor and clicking on a
    // target (Unity samples the cursor once per frame). 0 = none.
    void set_click_settle_ms(unsigned ms) noexcept { click_settle_ms_.store(ms, std::memory_order_relaxed); }

    // Invoked on the broadcaster thread when the swap hotkey is pressed.
    void set_on_swap(std::function<void()> cb);

    bool start();
    void stop();

    struct Snapshot {
        LatencyStats latency;      // capture -> delivered, per target delivery
        uint64_t deliveries = 0;
        uint64_t focus_failures = 0;
        uint64_t inject_failures = 0;
    };
    Snapshot snapshot() const;
    void reset_stats();

private:
    void run();
    void refresh_config();
    void handle(const InputEvent& ev);
    void execute(const Plan& p, const InputEvent& ev);
    void deliver(const Action& a, const InputEvent& ev);
    void reassert_held();

    EventRing& ring_;
    HANDLE wake_;
    std::thread thread_;
    std::atomic<bool> stop_{false};

    // Published config (guarded by config_mu_), pulled into the thread-local copies below.
    mutable std::mutex config_mu_;
    std::shared_ptr<const Routes> pending_routes_;
    std::vector<Target> pending_targets_;
    uint32_t pending_master_ = 0;
    std::atomic<uint64_t> config_version_{1};
    std::function<void()> on_swap_;

    // Broadcaster-thread state.
    std::shared_ptr<const Routes> routes_;
    std::vector<Target> targets_;
    uint32_t master_ = 0;
    uint64_t seen_version_ = 0;
    KeyState master_keys_;
    std::atomic<unsigned> click_settle_ms_{0};

    mutable std::mutex stats_mu_;
    Snapshot stats_;
};

}  // namespace mm::win
