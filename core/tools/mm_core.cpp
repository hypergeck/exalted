// mm-core: MultiMadness core CLI (Windows only).
//
//   mm-core list          [--pid N] [--class UnityWndClass]
//   mm-core spike-launch  --exe PATH [--args "..."] [--count 2] [--wait-ms 20000] [--observe-ms 10000]
//   mm-core spike-input   --hwnd 0x.. --channel focus|post (--key W [--hold-ms 1000] [--blur-hwnd 0x..] | --click X Y)
//                         [--delay-ms 2000] [--repeat 1]
//   mm-core run           (--adopt PID | --launch "EXE|ARGS")... [--class UnityWndClass]
//                         [--layout grid|stacked|pip] [--grid RxC] [--monitor 0] [--borderless]
//                         [--keys W,A,S,D,1,2,3] [--strategy focus|post] [--mouse-modifier CAPSLOCK]
//                         [--swap-key F12] [--slave-priority normal|below|idle] [--slave-eco]
//                         [--slave-cpu-cap PCT] [--kill-on-exit] [--click-settle-ms 0] [--stats-every 5]
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "mm/args.hpp"
#include "mm/key_names.hpp"
#include "mm/layout.hpp"
#include "mm/routing.hpp"
#include "mm/win/broadcaster.hpp"
#include "mm/win/capture.hpp"
#include "mm/win/discovery.hpp"
#include "mm/win/focus.hpp"
#include "mm/win/inject.hpp"
#include "mm/win/launcher.hpp"
#include "mm/win/window.hpp"

using namespace mm;
using namespace mm::win;

namespace {

constexpr const wchar_t* kDefaultClass = L"UnityWndClass";

HWND parse_hwnd(const std::string& s) {
    return reinterpret_cast<HWND>(static_cast<uintptr_t>(std::strtoull(s.c_str(), nullptr, 0)));
}

void print_window(const WindowInfo& w) {
    std::printf("  hwnd=0x%08llX pid=%-6lu class=%-24ls rect=%d,%d %dx%d  \"%ls\"\n",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(w.hwnd)), static_cast<unsigned long>(w.pid),
                w.class_name.c_str(), w.rect.x, w.rect.y, w.rect.w, w.rect.h, w.title.c_str());
}

int usage() {
    std::fputs("usage: mm-core <list|spike-launch|spike-input|run> [options]\n"
               "See the header of core/tools/mm_core.cpp and README.md for options.\n", stderr);
    return 2;
}

// ---------------------------------------------------------------- list
int cmd_list(const Args& a) {
    const DWORD pid = static_cast<DWORD>(a.get_int("pid", 0));
    const std::wstring cls = widen(a.get_or("class", ""), CP_ACP);
    auto windows = top_level_windows(pid);
    std::printf("%zu visible top-level windows%s\n", windows.size(), pid ? " for pid" : "");
    for (const auto& w : windows) {
        if (!cls.empty() && w.class_name != cls) continue;
        print_window(w);
    }
    return 0;
}

// ---------------------------------------------------------------- spike-launch
int cmd_spike_launch(const Args& a) {
    const auto exe = a.get("exe");
    if (!exe) { std::fputs("--exe is required\n", stderr); return 2; }
    const int count = static_cast<int>(a.get_int("count", 2));
    const unsigned wait_ms = static_cast<unsigned>(a.get_int("wait-ms", 20000));
    const unsigned observe_ms = static_cast<unsigned>(a.get_int("observe-ms", 10000));
    const std::wstring cls = widen(a.get_or("class", ""), CP_ACP);

    struct Row { Launched proc; HWND hwnd = nullptr; DWORD exit_code = 0; bool exited = false; };
    std::vector<Row> rows;

    for (int i = 0; i < count; ++i) {
        LaunchOptions opt;
        opt.exe = widen(*exe, CP_ACP);
        opt.args = widen(a.get_or("args", ""), CP_ACP);
        std::string err;
        auto l = launch(opt, err);
        if (!l) { std::printf("[%d] launch failed: %s\n", i, err.c_str()); continue; }
        if (!err.empty()) std::printf("[%d] warning: %s\n", i, err.c_str());
        std::printf("[%d] pid %lu started, waiting for window...\n", i, static_cast<unsigned long>(l->pid));
        Row r;
        r.hwnd = wait_for_main_window(l->pid, cls.empty() ? kDefaultClass : cls.c_str(), wait_ms, l->process.get());
        if (!r.hwnd) r.hwnd = find_main_window(l->pid, nullptr);  // any window, class hint may be stale
        r.proc = std::move(*l);
        if (r.hwnd) {
            for (const auto& w : top_level_windows(r.proc.pid)) if (w.hwnd == r.hwnd) print_window(w);
        } else {
            DWORD code = 0;
            const bool exited = has_exited(r.proc.process.get(), &code);
            std::printf("[%d] no window after %u ms%s\n", i, wait_ms,
                        exited ? (" (process exited, code " + std::to_string(code) + ")").c_str() : "");
        }
        rows.push_back(std::move(r));
    }

    std::printf("observing for %u ms (an instance that exits here usually means a single-instance lock)...\n", observe_ms);
    Sleep(observe_ms);
    int alive = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].exited = has_exited(rows[i].proc.process.get(), &rows[i].exit_code);
        if (rows[i].exited) std::printf("[%zu] pid %lu EXITED code %lu\n", i, static_cast<unsigned long>(rows[i].proc.pid), static_cast<unsigned long>(rows[i].exit_code));
        else { ++alive; std::printf("[%zu] pid %lu alive, hwnd=0x%llX\n", i, static_cast<unsigned long>(rows[i].proc.pid), static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(rows[i].hwnd))); }
    }
    std::printf("result: %d of %zu instances alive. Clients are left running.\n", alive, rows.size());
    return 0;
}

// ---------------------------------------------------------------- spike-input
int cmd_spike_input(const Args& a) {
    const auto hs = a.get("hwnd");
    if (!hs) { std::fputs("--hwnd is required\n", stderr); return 2; }
    const HWND hwnd = parse_hwnd(*hs);
    if (!IsWindow(hwnd)) { std::fputs("--hwnd is not a window\n", stderr); return 2; }
    const bool post = a.get_or("channel", "focus") == "post";
    const unsigned delay = static_cast<unsigned>(a.get_int("delay-ms", 2000));
    const unsigned hold = static_cast<unsigned>(a.get_int("hold-ms", 1000));
    const int repeat = static_cast<int>(a.get_int("repeat", 1));
    const HWND blur = a.has("blur-hwnd") ? parse_hwnd(*a.get("blur-hwnd")) : nullptr;

    enable_per_monitor_dpi();
    ForegroundLockGuard lock_guard;
    std::printf("channel=%s target=0x%llX; starting in %u ms\n", post ? "PostMessage" : "focus+SendInput",
                static_cast<unsigned long long>(reinterpret_cast<uintptr_t>(hwnd)), delay);
    Sleep(delay);

    const auto positional = a.positional();
    if (a.has("click")) {
        // --click X Y : client-relative pixel coordinates in the target.
        const long cx = std::strtol(a.get("click")->c_str(), nullptr, 0);
        const long cy = positional.empty() ? 0 : std::strtol(positional[0].c_str(), nullptr, 0);
        for (int i = 0; i < repeat; ++i) {
            if (post) {
                post_mouse_button(hwnd, kVkLButton, true, cx, cy);
                post_mouse_button(hwnd, kVkLButton, false, cx, cy);
            } else {
                const bool focused = bring_to_foreground(hwnd);
                const Rect client = client_rect_screen(hwnd);
                send_mouse_move_abs(to_absolute(virtual_screen(), client.x + cx, client.y + cy));
                Sleep(static_cast<DWORD>(a.get_int("settle-ms", 0)));
                send_mouse_button(kVkLButton, true);
                send_mouse_button(kVkLButton, false);
                std::printf("click %d: focus %s\n", i, focused ? "ok" : "FAILED");
            }
            Sleep(250);
        }
        return 0;
    }

    const auto key = parse_key(a.get_or("key", "W"));
    if (!key) { std::fputs("--key not recognised\n", stderr); return 2; }
    const uint16_t scan = static_cast<uint16_t>(MapVirtualKeyW(*key, MAPVK_VK_TO_VSC));
    for (int i = 0; i < repeat; ++i) {
        if (post) {
            post_key(hwnd, *key, scan, true, 0);
            Sleep(hold);
            post_key(hwnd, *key, scan, false, 0);
            std::printf("posted %s down/up (hold %u ms)\n", key_name(*key).c_str(), hold);
        } else {
            const bool focused = bring_to_foreground(hwnd);
            send_key(*key, scan, true, 0);
            if (blur) {
                // Does the held key survive the target losing focus?
                Sleep(hold / 2);
                const bool blurred = bring_to_foreground(blur);
                std::printf("  key down, focus moved away (%s); watch whether the target keeps moving\n", blurred ? "ok" : "FAILED");
                Sleep(hold / 2);
                bring_to_foreground(hwnd);
            } else {
                Sleep(hold);
            }
            send_key(*key, scan, false, 0);
            std::printf("sent %s down/up (hold %u ms), focus %s\n", key_name(*key).c_str(), hold, focused ? "ok" : "FAILED");
        }
        Sleep(250);
    }
    return 0;
}

// ---------------------------------------------------------------- run
struct Instance {
    DWORD pid = 0;
    HWND hwnd = nullptr;
    UniqueHandle process;
    UniqueHandle job;
    SavedStyle saved;
    bool launched = false;
    bool exited = false;
};

struct Session {
    std::vector<Instance> instances;
    Assignment assignment;
    LayoutSpec layout;
    Rect area;
    ProcessPolicy slave_policy;
    std::mutex mu;

    std::size_t master() { return assignment.master(); }

    void apply_layout_and_policy() {
        std::lock_guard lock(mu);
        const auto rects = compute_layout(layout, area, instances.size());
        std::vector<std::pair<HWND, Rect>> placements;
        for (std::size_t i = 0; i < instances.size(); ++i) {
            if (!instances[i].hwnd) continue;
            placements.emplace_back(instances[i].hwnd, rects[assignment.slot_of(i)]);
        }
        if (!apply_layout(placements)) std::printf("warning: DeferWindowPos batch failed\n");
        // Stacked/PiP: the master must be on top of the pile.
        const std::size_t m = assignment.master();
        if (m < instances.size() && instances[m].hwnd) SetWindowPos(instances[m].hwnd, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

        for (std::size_t i = 0; i < instances.size(); ++i) {
            if (!instances[i].process) continue;
            const ProcessPolicy policy = (i == m) ? ProcessPolicy{} : slave_policy;
            std::string err;
            if (!apply_policy(instances[i].process.get(), policy, &err)) std::printf("warning: policy for pid %lu: %s\n", static_cast<unsigned long>(instances[i].pid), err.c_str());
        }
    }

    std::vector<Target> targets() {
        std::lock_guard lock(mu);
        std::vector<Target> t;
        for (const auto& inst : instances) t.push_back(Target{inst.exited ? nullptr : inst.hwnd});
        return t;
    }
};

std::atomic<bool> g_stop{false};
HANDLE g_stop_event = nullptr;

BOOL WINAPI ctrl_handler(DWORD) {
    g_stop.store(true);
    if (g_stop_event) SetEvent(g_stop_event);
    return TRUE;
}

int cmd_run(const Args& a) {
    enable_per_monitor_dpi();
    const std::wstring cls_str = widen(a.get_or("class", ""), CP_ACP);
    const wchar_t* cls = cls_str.empty() ? kDefaultClass : cls_str.c_str();
    const unsigned wait_ms = static_cast<unsigned>(a.get_int("wait-ms", 30000));

    Session s;
    const std::string prio = a.get_or("slave-priority", "below");
    s.slave_policy.priority = prio == "idle" ? Priority::Idle : prio == "normal" ? Priority::Normal : Priority::BelowNormal;
    s.slave_policy.eco_qos = a.has("slave-eco");

    // --- instances
    for (const auto& spec : a.all("launch")) {
        const auto bar = spec.find('|');
        LaunchOptions opt;
        opt.exe = widen(spec.substr(0, bar), CP_ACP);
        if (bar != std::string::npos) opt.args = widen(spec.substr(bar + 1), CP_ACP);
        opt.policy = s.slave_policy;
        opt.cpu_cap_percent = static_cast<unsigned>(a.get_int("slave-cpu-cap", 0));
        opt.kill_on_job_close = a.has("kill-on-exit");
        std::string err;
        auto l = launch(opt, err);
        if (!l) { std::printf("launch failed: %s\n", err.c_str()); continue; }
        if (!err.empty()) std::printf("warning: %s\n", err.c_str());
        Instance inst;
        inst.pid = l->pid;
        inst.process = std::move(l->process);
        inst.job = std::move(l->job);
        inst.launched = true;
        std::printf("launched pid %lu, waiting up to %u ms for its window...\n", static_cast<unsigned long>(inst.pid), wait_ms);
        inst.hwnd = wait_for_main_window(inst.pid, cls, wait_ms, inst.process.get());
        if (!inst.hwnd) { std::printf("  no window found; skipping pid %lu\n", static_cast<unsigned long>(inst.pid)); continue; }
        s.instances.push_back(std::move(inst));
    }
    for (const auto& pid_s : a.all("adopt")) {
        Instance inst;
        inst.pid = static_cast<DWORD>(std::strtoul(pid_s.c_str(), nullptr, 0));
        inst.process = open_process_for_policy(inst.pid);
        inst.hwnd = wait_for_main_window(inst.pid, cls, 2000, inst.process.get());
        if (!inst.hwnd) { std::printf("adopt: no window for pid %lu\n", static_cast<unsigned long>(inst.pid)); continue; }
        s.instances.push_back(std::move(inst));
    }
    for (const auto& h : a.all("adopt-hwnd")) {
        Instance inst;
        inst.hwnd = parse_hwnd(h);
        if (!IsWindow(inst.hwnd)) { std::printf("adopt-hwnd: 0x%s is not a window\n", h.c_str()); continue; }
        GetWindowThreadProcessId(inst.hwnd, &inst.pid);
        inst.process = open_process_for_policy(inst.pid);
        s.instances.push_back(std::move(inst));
    }
    if (s.instances.empty()) { std::fputs("no instances: use --launch and/or --adopt\n", stderr); return 1; }
    if (s.instances.size() > 32) { std::fputs("at most 32 instances are supported\n", stderr); return 1; }

    // --- layout
    const auto mons = monitors();
    const std::size_t mon = static_cast<std::size_t>(a.get_int("monitor", 0));
    if (mons.empty()) { std::fputs("no monitors?\n", stderr); return 1; }
    s.area = mons[mon < mons.size() ? mon : 0].work_area;
    const std::string layout = a.get_or("layout", "grid");
    s.layout.kind = layout == "stacked" ? LayoutKind::Stacked : layout == "pip" ? LayoutKind::Pip : LayoutKind::Grid;
    if (auto g = a.get("grid")) {
        const auto x = g->find('x');
        if (x != std::string::npos) { s.layout.rows = std::atoi(g->substr(0, x).c_str()); s.layout.cols = std::atoi(g->substr(x + 1).c_str()); }
    }
    s.layout.thumb = static_cast<int>(a.get_int("thumb", 0));
    s.assignment.reset(s.instances.size());
    s.assignment.set_master(static_cast<std::size_t>(a.get_int("master", 0)));
    if (a.has("borderless")) for (auto& inst : s.instances) inst.saved = make_borderless(inst.hwnd);
    s.apply_layout_and_policy();

    // --- routes
    auto routes = std::make_shared<Routes>();
    const Strategy strategy = a.get_or("strategy", "focus") == "post" ? Strategy::WindowMessage : Strategy::FocusCycle;
    for (const auto& k : split_list(a.get_or("keys", ""))) {
        const auto vk = parse_key(k);
        if (!vk) { std::printf("warning: unknown key '%s'\n", k.c_str()); continue; }
        routes->add_key(KeyBinding{*vk, kAllTargets, strategy, false});
    }
    if (auto m = a.get("mouse-modifier")) {
        const auto vk = parse_key(*m);
        if (vk) { routes->mouse_targets = kAllTargets; routes->mouse_modifier_vk = *vk; routes->mouse_strategy = strategy; }
    }
    if (auto sk = parse_key(a.get_or("swap-key", "F12"))) routes->swap_hotkey_vk = *sk;

    // --- input pipeline
    EventRing ring;
    UniqueHandle wake(CreateEventW(nullptr, FALSE, FALSE, nullptr));
    Capture capture(ring, wake.get());
    Broadcaster broadcaster(ring, wake.get());
    ForegroundLockGuard lock_guard;

    for (const auto& b : routes->keys()) if (b.swallow) capture.set_swallow(b.vk, true);
    if (routes->swap_hotkey_vk) capture.set_swallow(routes->swap_hotkey_vk, true);
    broadcaster.set_routes(routes);
    broadcaster.set_targets(s.targets(), static_cast<uint32_t>(s.master()));
    broadcaster.set_click_settle_ms(static_cast<unsigned>(a.get_int("click-settle-ms", 0)));
    broadcaster.set_on_swap([&] {
        std::size_t next;
        {
            std::lock_guard lock(s.mu);
            const std::size_t n = s.instances.size();
            next = s.assignment.master();
            for (std::size_t i = 1; i <= n; ++i) {            // next live instance after the current master
                const std::size_t c = (s.assignment.master() + i) % n;
                if (!s.instances[c].exited) { next = c; break; }
            }
            s.assignment.set_master(next);
        }
        s.apply_layout_and_policy();
        broadcaster.set_targets(s.targets(), static_cast<uint32_t>(next));
        bring_to_foreground(s.instances[next].hwnd, 20);
        std::printf("master -> instance %zu (pid %lu)\n", next, static_cast<unsigned long>(s.instances[next].pid));
    });

    std::string err;
    if (!capture.start(&err)) { std::printf("capture failed: %s\n", err.c_str()); return 1; }
    broadcaster.start();
    bring_to_foreground(s.instances[s.master()].hwnd, 20);

    UniqueHandle stop_event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
    g_stop_event = stop_event.get();
    SetConsoleCtrlHandler(&ctrl_handler, TRUE);

    std::printf("running: %zu instances, master=%zu, %zu keys mirrored, swap=%s, strategy=%s. Ctrl+C to stop.\n",
                s.instances.size(), s.master(), routes->keys().size(), key_name(routes->swap_hotkey_vk).c_str(),
                strategy == Strategy::WindowMessage ? "PostMessage" : "focus+SendInput");

    const DWORD stats_every = static_cast<DWORD>(a.get_int("stats-every", 5)) * 1000;
    while (!g_stop.load()) {
        if (WaitForSingleObject(stop_event.get(), stats_every ? stats_every : 1000) == WAIT_OBJECT_0) break;

        // Watchdog: notice clients that died, free their slot in the broadcaster.
        bool changed = false;
        {
            std::lock_guard lock(s.mu);
            for (std::size_t i = 0; i < s.instances.size(); ++i) {
                auto& inst = s.instances[i];
                if (inst.exited) continue;
                DWORD code = 0;
                if ((inst.process && has_exited(inst.process.get(), &code)) || !IsWindow(inst.hwnd)) {
                    inst.exited = true;
                    changed = true;
                    std::printf("instance %zu (pid %lu) exited (code %lu)\n", i, static_cast<unsigned long>(inst.pid), static_cast<unsigned long>(code));
                }
            }
        }
        if (changed) broadcaster.set_targets(s.targets(), static_cast<uint32_t>(s.master()));

        if (stats_every) {
            const auto b = broadcaster.snapshot();
            const auto c = capture.stats();
            std::printf("[stats] captured=%llu dropped=%llu deliveries=%llu p50=%uus p99=%uus max=%uus focus_fail=%llu inject_fail=%llu\n",
                        static_cast<unsigned long long>(c.captured), static_cast<unsigned long long>(c.dropped),
                        static_cast<unsigned long long>(b.deliveries), b.latency.percentile_us(50), b.latency.percentile_us(99),
                        b.latency.max_us(), static_cast<unsigned long long>(b.focus_failures), static_cast<unsigned long long>(b.inject_failures));
        }
    }

    std::puts("stopping...");
    capture.stop();
    broadcaster.stop();
    for (auto& inst : s.instances) if (!inst.exited) restore_style(inst.hwnd, inst.saved);
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) return usage();
    const std::string cmd = argv[1];
    Args a(argc, argv, 2);
    if (cmd == "list") return cmd_list(a);
    if (cmd == "spike-launch") return cmd_spike_launch(a);
    if (cmd == "spike-input") return cmd_spike_input(a);
    if (cmd == "run") return cmd_run(a);
    return usage();
}
