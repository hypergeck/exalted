# MultiMadness — Technical Architecture & Phased Plan

**Target:** Windows 10/11 x64 multiboxing controller for the Realm of the Mad God *Exalt* client (Unity).
**Scope:** launch, arrange, throttle, and drive N Exalt clients from one keyboard/mouse.
**Author's note:** Written 2026-09-06. Everything below that touches Exalt internals (command line, window class, input path) must be re-verified against the current build during Milestone 0. The client updates often.

---

## 0. Ground rules and risk register

1. **Terms of Service.** DECA permits owning several accounts, but third-party input automation is a grey-to-black area of the RotMG ToS and Exalt is a permadeath game. Treat every design decision as "could this get an account banned?" and let the user opt in per feature. Nothing in this document reads game memory, decodes network traffic, or attempts to hide from client integrity checks. Those are deliberately out of scope and should stay that way.
2. **Platform.** Development machine is macOS; the target is Windows. Plan for a physical Windows box or a Windows VM with GPU passthrough. A plain Parallels/VMware VM will not sustain 4+ Unity clients and will produce misleading performance numbers.
3. **The fundamental Windows constraint.** Only one window per session is the *foreground* window, and Unity reads keyboard and mouse through **Raw Input**, which is delivered only to the foreground window. Unity also resets its input state when a window loses focus. Every input-broadcasting design is a negotiation with those two facts. Section 3 is built around them.
4. **Hardware budget.** Each Exalt client costs roughly 1–2 GB RAM, 1 CPU core at 60 FPS, and several hundred MB of VRAM. Six clients on a 16 GB machine is the ceiling; the resource governor (Section 4) exists so the machine degrades gracefully instead of crashing.

---

## 1. Technology stack

### 1.1 Decision

| Layer | Choice | Why |
|---|---|---|
| **Core service** (hooks, broadcast, process/window mgmt, governor) | **C++20**, Win32 directly, `wil` for RAII handles, `spdlog` for logging | The entire problem is Win32 API surface. C++ has zero marshalling cost on the hot path, no GC, and all of the prior art (HotkeyNet, ISBoxer's Inner Space, AutoHotkey internals) is C/C++. **Rust with the `windows` crate is an equally valid choice** if the team prefers it; the design below is language-agnostic at the module boundary. |
| **UI** | **C# / .NET 8 WPF** with `CommunityToolkit.Mvvm` | Fastest path to a polished, data-bound Windows desktop UI. WPF hosts DWM thumbnails and Win32 child HWNDs cleanly (`HwndHost`). WinUI 3 is acceptable but its Win32 interop is rougher. |
| **IPC** | Named pipe (control plane) + shared-memory ring buffer (telemetry/event plane) | See Section 3.5. |
| **Serialization** | Protobuf (`protobuf-lite` in C++, `Google.Protobuf` in C#) for the pipe protocol; JSON for on-disk profiles | Protobuf gives a versioned schema across two languages; JSON profiles stay hand-editable. |
| **Previews** | DWM thumbnails (`DwmRegisterThumbnail`) first; `Windows.Graphics.Capture` for anything fancier | Thumbnails are free (no copy), composited by DWM, and work on occluded windows. |
| **Secrets** | Windows DPAPI (`CryptProtectData` / `ProtectedData`) | Account credentials never leave the user profile unencrypted. |
| **Build / packaging** | CMake + vcpkg for the core, `dotnet` for the UI, WiX or MSIX installer | Single `build.ps1` that produces `mb-core.exe` and `mb-ui.exe`. |
| **Tests** | Catch2 (core), xUnit (UI), plus a **Unity test target** (a 50-line Unity project that logs `Input` state) for input-channel validation | You cannot unit-test "does Unity see this key"; you need a throwaway Unity build that tells you. |

### 1.2 Why two processes rather than one C# process with P/Invoke

- **Hook safety.** A `WH_KEYBOARD_LL` callback must return within `LowLevelHooksTimeout` (default 300 ms) or Windows silently removes the hook. A WPF process that hits a gen-2 GC pause, a layout storm, or a blocking dialog will lose the hook. Isolating hooks in a native process with no GC and no UI thread makes that class of bug impossible.
- **Integrity / UIPI.** If the game ever runs elevated, `SendInput` and `SetForegroundWindow` from a medium-integrity process are silently dropped. It is easier to elevate a small, auditable native core than the whole UI.
- **Crash isolation.** The UI can crash and restart while the core keeps the clients laid out and the hotkeys live.
- **Upgrade path.** A future macOS port (CGEvent taps, `CGEventPostToPid`) replaces only the core.

If the team is one person and time-boxed, a **single C# process with the hook thread in a small native DLL** is a legitimate fallback. Keep the module boundary from Section 1.3 identical so you can split later.

### 1.3 Module map (core)

```
mb-core.exe
├── input/
│   ├── capture.cpp        WH_KEYBOARD_LL / WH_MOUSE_LL on a dedicated message-pump thread
│   ├── ring.hpp           lock-free SPSC ring buffer of InputEvent (32-byte POD)
│   ├── broadcaster.cpp    consumer thread: routing rules -> per-target delivery strategies
│   └── strategies/        focus_cycle.cpp, post_message.cpp, (future) in_process.cpp
├── proc/
│   ├── launcher.cpp       CreateProcess + Job Object per instance, mutex contingency
│   ├── adopter.cpp        attach to already-running clients by PID/HWND
│   └── watchdog.cpp       process exit detection, auto-relaunch, memory pressure guard
├── wm/
│   ├── discovery.cpp      EnumWindows -> "UnityWndClass" owned by tracked PIDs
│   ├── styler.cpp         borderless conversion, DPI handling
│   └── layout.cpp         grid / PiP / swap, DeferWindowPos batches
├── governor/
│   ├── policy.cpp         per-instance CPU/GPU/priority policies, foreground-aware
│   └── metrics.cpp        PDH / GetProcessTimes / DXGI memory sampling
├── ipc/
│   ├── pipe_server.cpp    \\.\pipe\multimadness-control, protobuf frames
│   └── shm_telemetry.cpp  Local\multimadness-telemetry ring (core -> UI)
└── main.cpp               thread orchestration, single-instance guard for *ourselves*
```

---

## 2. Process and window management

### 2.1 How Exalt starts

The official launcher authenticates, then starts `RotMG Exalt.exe` with a single argument of the form `data:{...json...}` carrying a per-account platform token, GUID, token timestamps and environment. Confirm the exact shape on the current build with Process Explorer (right-click → Properties → Command line). Two consequences:

- **Anything that can read process command lines (WMI, `NtQueryInformationProcess`) can read that token.** Our logs must redact `argv`, and the UI must never display it.
- The token expires. Our launcher must either (a) re-use the official launcher per account, or (b) reproduce its auth call. **Plan for (a) in Milestones 1–3 (adopt running clients) and (b) only in Milestone 6**, behind DPAPI-encrypted storage and an explicit user opt-in.

Useful Unity player switches (all standard Unity Standalone args, verify each on the current engine version):

```
-screen-fullscreen 0 -screen-width 1280 -screen-height 720   # windowed at a fixed size
-popupwindow                                                 # borderless (no title bar)
-window-mode borderless                                      # newer engines; prefer this if honored
-monitor 2                                                   # place on a specific display
-force-d3d11 | -force-d3d12                                  # pin graphics API
-force-device-index 1                                        # pin GPU on multi-GPU rigs
-nolog                                                       # skip Player.log writes
```

### 2.2 Launch pipeline

```
Launch(profile.instance[i])
  1. CreateJobObject("Local\mm-job-<guid>")
       JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE only when the user opts in (never kill a permadeath client by accident)
       (optional) JOBOBJECT_CPU_RATE_CONTROL, JOB_OBJECT_LIMIT_PROCESS_MEMORY
  2. CreateProcessW(exe, args, CREATE_SUSPENDED | CREATE_BREAKAWAY_FROM_JOB?)
  3. AssignProcessToJobObject(job, hProcess)
  4. SetPriorityClass / SetProcessAffinityMask / EcoQoS (Section 4)
  5. ResumeThread(hMainThread)
  6. WaitForInputIdle(hProcess, 10s)  ->  poll EnumWindows for HWND
  7. Apply window style + layout slot (2.4, 2.5)
  8. Register with watchdog (hProcess is waitable; WaitForMultipleObjects)
```

Creating the process **suspended** lets you attach the job and set scheduling policy before Unity spins up its worker threads, so the policy applies to all of them.

### 2.3 Single-instance locks: contingency plan

Unity does not add a single-instance mutex by default, and running several Exalt clients (mules) is common practice, so **the expected finding in M0 is "no mutex".** If a lock does appear (detect with Process Explorer's handle view or `handle.exe -a -p <pid>` and look for `\Sessions\1\BaseNamedObjects\...`), ranked options:

1. **Close the handle after launch.** Enumerate handles via `NtQuerySystemInformation(SystemExtendedHandleInformation)`, find the mutant in the target PID, `DuplicateHandle(..., DUPLICATE_CLOSE_SOURCE)`. This is exactly what Process Explorer's "Close Handle" does. Safe as long as the game does not re-check.
2. **Separate object namespace per instance.** Run each client in its own **AppContainer** (`CreateAppContainerProfile` + `PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES`). Named objects then live under `\Sessions\1\AppContainerNamedObjects\<SID>`, so two clients never see each other's mutex. Cost: the game must tolerate AppContainer restrictions (file/registry access), which is a coin flip.
3. **Sandboxie-Plus** as an external dependency. Works, but you have shipped someone else's kernel driver.

If the lock is implemented as a *file* lock (e.g. on `Player.log`), `-nolog` or `-logFile <unique path>` resolves it trivially.

### 2.4 Borderless conversion

Prefer the launch switch (`-popupwindow`). Fallback for adopted windows:

```cpp
LONG_PTR style = GetWindowLongPtr(h, GWL_STYLE);
style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU);
SetWindowLongPtr(h, GWL_STYLE, style | WS_POPUP);
LONG_PTR ex = GetWindowLongPtr(h, GWL_EXSTYLE);
ex &= ~(WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
SetWindowLongPtr(h, GWL_EXSTYLE, ex);
SetWindowPos(h, nullptr, x, y, w, h, SWP_FRAMECHANGED | SWP_NOZORDER | SWP_NOACTIVATE);
```

Unity re-asserts its own style on some resolution changes, so the styler subscribes to `EVENT_OBJECT_LOCATIONCHANGE` via `SetWinEventHook` and re-applies when the frame comes back.

**DPI:** declare `PerMonitorV2` in the core's manifest, use `GetDpiForWindow` / `AdjustWindowRectExForDpi`, and store layouts in DIPs plus the monitor's device name, not raw pixels.

### 2.5 Layout engine

- **Inputs:** monitor work areas (`EnumDisplayMonitors` + `GetMonitorInfo`), a layout descriptor from the profile, the instance→slot assignment.
- **Layouts:** `grid(rows, cols)`, `pip(masterRect, thumbSize, thumbEdge)`, `stacked` (all windows at the master rect, only the top one visible; combine with DWM thumbnails for previews). Every layout resolves to `vector<{hwnd, RECT}>`.
- **Apply atomically:** `BeginDeferWindowPos(n)` → `DeferWindowPos(...)` per window → `EndDeferWindowPos`. One repaint, no cascade of intermediate frames.
- **Swap hotkey:** swapping master and a slave is a slot reassignment followed by one deferred batch and a focus change; sub-frame.
- **Aspect ratio discipline:** keep every instance at the same client aspect ratio (ideally identical size). Mouse broadcasting (3.4) assumes it, and RotMG's aim-from-center model makes mismatched aspect ratios miss.

### 2.6 Live previews (picture-in-picture)

`DwmRegisterThumbnail(ourOverlayHwnd, slaveHwnd)` + `DwmUpdateThumbnailProperties` renders any non-minimized top-level window into a rectangle of our own window at zero copy cost, even if the source is fully occluded. Recommended PiP: all clients full-size and stacked at the master rect; our overlay shows thumbnails of the hidden ones along one edge; clicking a thumbnail triggers the swap. Never minimize a client, or its thumbnail goes black and Unity may stop presenting.

### 2.7 Focus control (`SetForegroundWindow` restrictions)

Windows refuses foreground changes from processes that did not receive the last input. Mitigations, in order of preference:

1. Our process installed the low-level hooks and is therefore "receiving input" in practice, but that does **not** satisfy the rule. Use `AttachThreadInput(ourTid, GetWindowThreadProcessId(fg), TRUE)` → `SetForegroundWindow(target)` → detach.
2. `SystemParametersInfo(SPI_SETFOREGROUNDLOCKTIMEOUT, 0, 0, SPIF_SENDCHANGE)` once at startup (user setting, restore on exit).
3. `SwitchToThisWindow(h, TRUE)` as a fallback; it is exported from user32 and ignores the lock.

Verify success with `GetForegroundWindow()` after each attempt; the broadcaster must never assume the switch happened.

---

## 3. Input broadcasting

### 3.1 Capture on the master

Two Windows mechanisms can observe global input; use both.

| Mechanism | Use it for | Notes |
|---|---|---|
| `SetWindowsHookEx(WH_KEYBOARD_LL / WH_MOUSE_LL)` | Hotkeys that must **not** reach the game (return 1 to swallow), and the primary event stream | Installed from a dedicated thread running `GetMessage` loop. Callback: copy 32 bytes into the ring, return. **Never** call `SendInput`, log, or allocate inside the callback. Watch `LLKHF_INJECTED` / `LLMHF_INJECTED` to filter our own output. |
| `RegisterRawInputDevices` with `RIDEV_INPUTSINK` on a hidden window | Device identity (which physical keyboard), and a hook-timeout-proof secondary stream | Cannot block events. Useful for "second keyboard drives slave #2" setups. |

**Feedback loop guard.** Every `INPUT` we inject carries `dwExtraInfo = 0x4D4D0001` ("MM"). The hook drops any event whose `dwExtraInfo` matches, in addition to the injected flag (some drivers clear the flag; the magic survives).

**Auto-repeat.** Low-level hooks do not expose the "previous state" bit. Keep a 256-entry `bool down[]` table in the capture thread and drop repeats before they enter the ring.

### 3.2 The pipeline

```
[LL hook thread, TIME_CRITICAL]         [broadcaster thread, HIGHEST]
  hook callback                           loop:
    ├─ filter injected/repeat                WaitForSingleObject(ringEvent)
    ├─ stamp QPC                             while (ring.pop(ev))
    └─ ring.push(ev) ─── SetEvent ─────►        route(ev) -> targets[]
                                                for t in targets: strategy[t].deliver(ev)
                                                telemetry.push(ev, t, QPC-now)
```

`InputEvent` (32 bytes, cache-line friendly, trivially copyable, identical layout in C++ and the shared-memory telemetry ring):

```cpp
struct InputEvent {
  uint64_t qpc;        // QueryPerformanceCounter at hook entry
  uint16_t kind;       // KeyDown, KeyUp, MouseMove, MouseDown, MouseUp, Wheel
  uint16_t vk;         // virtual key or mouse button
  uint16_t scan;       // scan code (send scan codes, not VKs, to Unity)
  uint16_t flags;      // extended key, injected, etc.
  int32_t  x, y;       // absolute screen coords for mouse events
  uint32_t source;     // device id from raw input, 0 if unknown
  uint32_t reserved;
};
```

The ring is a single-producer/single-consumer power-of-two buffer with `head` and `tail` on separate cache lines (`alignas(64)`) and acquire/release atomics. No locks, no allocation, no syscalls except the wakeup event. Capacity 4096 events (128 KB) absorbs any burst.

### 3.3 Delivery strategies

This is the heart of the design, and it must be honest about what Unity accepts.

**Strategy A — Focus-cycle + `SendInput` (baseline, always works for discrete actions).**
For each target: bring it to the foreground (2.7), `SendInput` the event with `KEYEVENTF_SCANCODE`, return focus to the master. Cost per target ≈ 1–3 ms for the foreground switch plus ~50 µs for `SendInput`. Four slaves ≈ 10 ms round trip, under one 60 FPS frame.

*The catch:* Unity clears its key state on `WM_KILLFOCUS`/deactivate. A slave that received "W down" forgets it the moment focus returns to the master, and the **master** forgets its own held keys during the few milliseconds it was not foreground. Therefore:

- After every focus return, **re-assert** the master's held-key set (from the `down[]` table) with injected key-downs. Unity treats them as fresh presses; the user sees no gap because the re-assert lands in the same frame.
- Discrete actions (ability keys, nexus, item slots, toggles, clicks) broadcast perfectly with Strategy A.
- **Continuous held input (movement) cannot be mirrored to more than one window with Strategy A.** Do not promise it in the UI. Offer "movement follows the focused window" and "tap-to-step" (a held W becomes 100 ms W taps to each slave in rotation) as explicit modes, and measure whether tap-to-step is playable.

**Strategy B — `PostMessage` (`WM_KEYDOWN`/`WM_KEYUP`/`WM_LBUTTONDOWN`… to the target HWND).**
Zero focus change, zero latency, works on background windows, and for Unity games it usually **does not** drive `Input.GetKey` because Unity reads Raw Input rather than window messages. It often *does* drive text fields (`WM_CHAR`) and sometimes mouse clicks on UI. Keep it as a per-binding strategy the user can select after M0 proves which keys it moves. It costs nothing to keep.

**Strategy C — In-process raw-input shim (injected DLL that feeds `WM_INPUT`/`GetRawInputData` to a background client).**
Highest fidelity, solves held keys, and is how commercial multiboxers achieve "true" background input. It is also code injection into the game client, which is the thing DECA's integrity checks and ToS are written against. **Out of scope for v1.** The `strategies/` interface leaves room for it so the decision is a product decision, not a rewrite.

**Strategy D — Session-per-slave (research spike).** Each Windows session has its own foreground window. Slaves launched inside secondary logged-in user sessions (fast user switching) each keep foreground focus permanently; a tiny agent in each session receives events over the named pipe and calls `SendInput` locally. Held keys work without injection. Costs: no live view of slaves (capture does not cross sessions), GPU scheduling of disconnected sessions is unpredictable, and each session needs a Windows user account. Worth one week of investigation after M3 if movement broadcasting turns out to matter.

### 3.4 Mouse

- Normalize master coordinates to client space (`ScreenToClient`, divide by client size), map to each slave's client rect, convert to virtual-desktop absolute units (`MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK`, 0–65535).
- A click is a three-step sequence per target: move, button down, button up, then cursor restore to the master position. Unity samples the cursor once per frame, so the move and the click must land in the same frame or the click registers at the old position. M0 measures this; if it fails, insert a `WaitForSingleObject(frameEvent, 1)` based on the target's `DwmFlush` cadence or simply delay 1 frame (17 ms).
- Continuous mouse-move broadcasting is pointless under Strategy A (only the foreground window sees it) and is a CPU sink; broadcast only clicks and wheel, and only when a user-chosen modifier is held.

### 3.5 IPC between core and UI

**Control plane: named pipe** `\\.\pipe\multimadness-control`, `PIPE_TYPE_MESSAGE`, overlapped I/O, one client (the UI). Each frame is `uint32 length` + protobuf `Envelope { oneof { LaunchInstance, AdoptInstance, SetLayout, SetMaster, SetBindings, SetPolicy, Shutdown, ... } }`. Core → UI events on the same pipe: `InstanceStateChanged`, `WindowFound`, `PolicyApplied`, `Error`. Protect the pipe with a DACL restricted to the current user so another process cannot drive the core.

**Event plane: shared memory** `Local\multimadness-telemetry`, a 1 MB `CreateFileMapping` region containing the same SPSC ring type as 3.2 (core is producer, UI is consumer) plus a header with schema version and QPC frequency. Payload per record: the `InputEvent`, the target instance id, the strategy used, and `deliverLatencyUs`. The UI samples it at 10–30 Hz for the latency histogram and the "which keys are live" panel. No pipe traffic per keystroke.

**Why not one mechanism?** Control messages need reliability and ordering (pipe). Telemetry needs zero back-pressure on the hot path (shm). Mixing them means a slow UI stalls the broadcaster.

### 3.6 Latency budget

| Stage | Typical |
|---|---|
| Physical key → LL hook callback | 0.1–1 ms (driver + system input queue) |
| Hook → ring → broadcaster wake | 20–100 µs |
| Foreground switch (Strategy A) | 1–3 ms per target |
| `SendInput` | ~50 µs |
| Unity's next input poll | 0–16.7 ms at 60 FPS (**dominant**) |

Target: added latency below one game frame for up to 4 slaves; p99 reported live from telemetry. "Near-zero" is honest only relative to the frame time; nothing on Windows delivers to two windows in the same input-queue tick.

### 3.7 Memory management on the hot path

- All event storage is fixed-size and pre-allocated at start (rings, `down[]` tables, target arrays). The broadcaster loop performs no heap allocation; verify with a debug allocator hook that asserts in that thread.
- Routing tables (binding → targets) are immutable snapshots swapped atomically (`std::atomic<shared_ptr<const Routes>>` or an RCU-style double buffer). Profile edits from the UI build a new table and swap it; the hot path never takes a lock.
- Handles are RAII (`wil::unique_handle`, `wil::unique_hwineventhook`); the watchdog owns process/job handles, nothing else duplicates them.
- Thread priorities: hook thread `THREAD_PRIORITY_TIME_CRITICAL`; broadcaster `THREAD_PRIORITY_HIGHEST`; everything else normal. Consider `AvSetMmThreadCharacteristics(L"Games")` for the broadcaster if the OS scheduler proves noisy under load.

---

## 4. Resource optimization ("the governor")

Goal: the master runs at full quality; every other instance consumes the minimum that keeps it connected and responsive. Levers, weakest to strongest:

### 4.1 In-game settings (largest effect, hardest to apply per instance)

Exalt's own FPS cap, particle/quality toggles, and resolution scale are the single biggest lever. Unity stores these as PlayerPrefs under `HKCU\Software\<Company>\<Product>`, shared by **all** instances of the same user, so "low on slaves, high on master" is not directly expressible. Options: (a) accept a global cap (e.g. 60) and rely on OS levers below; (b) run slaves under a second local Windows user via `CreateProcessWithLogonW` (separate HKCU, separate PlayerPrefs, same desktop; `SendInput` and focus still work because it is the same session and integrity level). (b) is cheap to implement and worth an M5 experiment.

### 4.2 OS scheduling levers (applied per instance, foreground-aware)

| Lever | API | Effect |
|---|---|---|
| Priority class | `SetPriorityClass(BELOW_NORMAL_PRIORITY_CLASS)` | Master wins CPU contention |
| Efficiency mode (EcoQoS) | `SetProcessInformation(ProcessPowerThrottling, PROCESS_POWER_THROTTLING_EXECUTION_SPEED)` | Windows 11 "Efficiency mode": slaves scheduled on E-cores at low clocks |
| Affinity | `SetProcessAffinityMask` / CPU sets (`SetProcessDefaultCpuSetMasks`) | Keep slaves off the master's cores |
| Hard CPU cap | Job `JOBOBJECT_CPU_RATE_CONTROL_INFORMATION` (`CPU_RATE_CONTROL_HARD_CAP`) | Guarantees a ceiling; too aggressive and the client desyncs or disconnects. Start at 35% of one core and tune |
| Memory ceiling | Job `JOB_OBJECT_LIMIT_PROCESS_MEMORY` | Converts a runaway into a clean crash of one client instead of the whole machine |

The governor re-evaluates on every master change: the new master is promoted (normal priority, no throttling) and the old master demoted, in one batch, before the focus change so the incoming master already has its cores.

### 4.3 GPU

- **Pixels are the lever.** A slave at 640×360 renders a quarter of the fragments of 1280×720. Unity honors `-screen-width/-screen-height`; combine with `stacked` layout so the size is invisible to the user. Mouse mapping in 3.4 already scales.
- Unity keeps presenting when occluded; DXGI does not throttle it. Do not rely on occlusion for savings.
- Per-executable frame caps via NVAPI/ADL driver profiles apply to the *executable name*, which is shared by all instances. Not usable without per-instance exe copies; rejected.
- Multi-GPU: pin slaves to the weaker GPU with `-force-device-index`.

### 4.4 Memory pressure and crash prevention

- Sample `GetProcessMemoryInfo`, `GlobalMemoryStatusEx` (commit charge), and `IDXGIAdapter3::QueryVideoMemoryInfo` at 1 Hz.
- Refuse to launch another instance if projected commit exceeds 85% of the limit; surface the reason in the UI.
- Do not `EmptyWorkingSet` idle slaves by default; the resulting page-in storm when they become master is worse than the RAM saved. Offer it as an "aggressive" policy.
- Watchdog: `WaitForMultipleObjects` over all process handles; on exit, record exit code, optionally relaunch after a back-off, and free the layout slot.

---

## 5. Development roadmap

Estimates assume one experienced Windows developer. Each milestone has an exit criterion; do not start the next without it.

### M0 — Feasibility spikes (1 week)

Throwaway C++ console programs. Answers we need before committing to anything:

1. Does Exalt run N instances from the same install? Any mutex/file lock? (2.3)
2. Exact command line and window class of the current build; do `-popupwindow` / `-screen-*` work?
3. **Input channel matrix** against the real client and the Unity test target: for `SendInput`+focus, `PostMessage`, and `WM_INPUT`-less background windows, which of {key tap, key hold, mouse click, mouse move} registers? Does a held key survive focus loss? Does a click register at the cursor's new position in the same frame?
4. Baseline resource cost of one idle client at 720p/60 FPS.

**Exit:** a one-page findings table. If held keys survive focus loss (unlikely), Strategy A alone covers everything and the plan simplifies.

### M1 — Input-mirroring proof of concept (2 weeks)

Single native executable, no UI, HWNDs from the command line. Delivers: LL hook capture thread, SPSC ring, broadcaster with Strategy A and the held-key re-assert, feedback-loop guard, QPC telemetry printed to the console (p50/p99 per target).
**Exit:** pressing an ability key on the master fires it on 3 slaves with p99 added latency ≤ 1 frame, for 10 minutes without a dropped hook.

### M2 — Process and window manager (2 weeks)

Launcher with Job Objects, adopter, discovery, borderless styler, grid and stacked layouts with `DeferWindowPos`, swap hotkey, foreground-lock mitigations, watchdog. Still console-driven (`mb-core.exe --profile dev.json`).
**Exit:** launch 4 adopted-or-spawned clients into a 2×2 grid from one command; swap master in < 1 frame; kill one client and see the slot freed.

### M3 — Core service and IPC (2 weeks)

Split into `mb-core.exe` as a long-running service-style process: pipe server, protobuf schema, shm telemetry ring, pipe DACL, structured logging with `argv` redaction, graceful shutdown that restores window styles and the foreground-lock setting. Ship a tiny C# `mb-cli` that speaks the protocol to prove the boundary.
**Exit:** UI-less integration test drives launch → layout → bindings → shutdown over the pipe; telemetry ring shows per-event latency.

### M4 — Desktop UI (3 weeks)

WPF, MVVM. Screens: instance list with state and live thumbnails, layout picker with drag-to-assign slots, bindings editor (key → targets → strategy), master indicator overlay, latency panel fed from shm, log viewer. First-run wizard that explains the ToS risk and the held-key limitation in plain words.
**Exit:** a user who has never seen the CLI can arrange and drive 4 clients from the UI.

### M5 — Resource governor (2 weeks)

Policies from Section 4 as per-instance presets (Balanced / Background / Aggressive), foreground-aware promotion/demotion, memory-pressure launch guard, optional second-user launch for separate PlayerPrefs, GPU pixel-budget mode with stacked layout.
**Exit:** 6 clients on the reference machine with the master holding its FPS cap and no slave disconnecting over a 30-minute session.

### M6 — Profiles, accounts, polish (3 weeks)

JSON profiles (layouts, bindings, policies, per-instance launch args) with schema versioning; DPAPI-encrypted account store and integrated launcher (opt-in); installer; auto-update; crash reporting; user docs.
**Exit:** clean install on a fresh Windows machine to first multiboxed session in under 5 minutes.

### Later / optional

- Strategy D session-per-slave research (1 week spike).
- macOS core (`CGEventTap` capture, `CGEventPostToPid` delivery, which notably works on background processes).
- `Windows.Graphics.Capture` based preview wall.

**Total to a polished v1: roughly 15 weeks.** The two schedule risks are M0 findings (if Exalt fights multiple instances, add 1–2 weeks for the AppContainer path) and the held-key limitation being unacceptable to users (which pushes Strategy D or a product-level decision).

---

## 6. Appendix — API index

| Area | APIs |
|---|---|
| Capture | `SetWindowsHookEx`, `CallNextHookEx`, `KBDLLHOOKSTRUCT`, `MSLLHOOKSTRUCT`, `RegisterRawInputDevices`, `GetRawInputData` |
| Delivery | `SendInput`, `MapVirtualKeyEx`, `PostMessage`, `SetForegroundWindow`, `AttachThreadInput`, `SwitchToThisWindow`, `AllowSetForegroundWindow` |
| Process | `CreateProcessW`, `CreateProcessWithLogonW`, `CreateJobObject`, `SetInformationJobObject`, `AssignProcessToJobObject`, `SetPriorityClass`, `SetProcessAffinityMask`, `SetProcessInformation(ProcessPowerThrottling)`, `WaitForInputIdle`, `NtQuerySystemInformation`, `DuplicateHandle` |
| Windows | `EnumWindows`, `GetWindowThreadProcessId`, `GetClassName`, `SetWindowLongPtr`, `SetWindowPos`, `BeginDeferWindowPos`, `EnumDisplayMonitors`, `GetDpiForWindow`, `SetWinEventHook`, `DwmRegisterThumbnail`, `DwmUpdateThumbnailProperties` |
| IPC | `CreateNamedPipe`, `ConnectNamedPipe`, overlapped `ReadFile`/`WriteFile`, `CreateFileMapping`, `MapViewOfFile`, `CreateEvent`, `InitializeSecurityDescriptor` + `SetSecurityDescriptorDacl` |
| Metrics | `GetProcessTimes`, `GetProcessMemoryInfo`, `GlobalMemoryStatusEx`, `IDXGIAdapter3::QueryVideoMemoryInfo`, PDH counters |
| Secrets | `CryptProtectData` / `CryptUnprotectData`, Credential Manager (`CredWrite`) |
