# MultiMadness

A multiboxing controller for the Realm of the Mad God *Exalt* client on Windows:
launch or adopt several clients, arrange them on screen, throttle the ones you are
not looking at, and mirror chosen keys and clicks from the window you are playing
to all the others.

Design and roadmap: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

> **Read before using.** Owning several accounts is allowed; third-party input
> automation is a grey-to-black area of the RotMG Terms of Service, and Exalt is a
> permadeath game. This project never reads game memory, never decodes network
> traffic, and never injects code into the client. Use at your own risk.

## Status

Milestones 0–2 of the roadmap are implemented as a single Windows CLI, `mm-core`:

| Area | What exists |
|---|---|
| Input capture | Low-level keyboard + mouse hooks on a dedicated thread, auto-repeat filter, feedback-loop guard (`dwExtraInfo` magic + injected flag), lock-free ring to the broadcaster |
| Broadcast | Focus-cycle + `SendInput` (scan codes) and `PostMessage` strategies, held-key re-assert after every focus return, mouse click/wheel mapping between client rects, swap-master hotkey |
| Processes | `CreateProcess` suspended → Job Object → priority / EcoQoS / affinity → resume; adopt running clients by PID or HWND; exit watchdog |
| Windows | Window discovery by PID and class, borderless conversion + restore, grid / stacked / picture-in-picture layouts applied in one `DeferWindowPos` batch |
| Telemetry | Capture→delivery latency histogram (p50 / p99 / max), focus and inject failure counters |
| Tests | Portable modules (ring, key state, routing/dispatch, geometry, layout, key names, args) run on any platform |

Not yet: the UI, the named-pipe/shared-memory IPC (M3), profiles, account storage.

## Layout

```
core/include/mm/        portable headers (no Win32): input_event, spsc_ring, key_state,
                        routing, dispatch, geometry, layout, latency_stats, key_names, args
core/include/mm/win/    Win32 adapters: capture, inject, focus, launcher, discovery, window, broadcaster
core/src/win/           their implementations
core/tools/mm_core.cpp  the CLI
tests/                  portable unit tests (no framework, exit code = failures)
scripts/                test-portable.sh (any OS), build-windows.ps1
docs/                   architecture document
```

## Build

Windows (Visual Studio 2022 + CMake 3.21+):

```powershell
.\scripts\build-windows.ps1
```

Portable tests only, on macOS/Linux:

```bash
./scripts/test-portable.sh
```

CI builds both on every push and publishes `mm-core.exe` as a workflow artifact.

## Milestone 0: verify the assumptions against the current client

The architecture rests on facts about the Exalt build that change with updates.
Run these once and record the results before relying on anything else.

1. **How does the official launcher start the client?** Start Exalt normally, open
   Process Explorer, and read the command line of `RotMG Exalt.exe`. Note the exact
   argument shape; it carries a per-account token, so never paste it into a bug report.

2. **Can two clients run from one install?**
   ```powershell
   mm-core spike-launch --exe "C:\...\RotMG Exalt.exe" --args "<args from step 1>" --count 2
   ```
   An instance that exits during the observation window points to a single-instance lock
   (see the contingency plan in the architecture document).

3. **Window class and borderless switches.**
   ```powershell
   mm-core list --class UnityWndClass
   mm-core spike-launch --exe "..." --args "-popupwindow -screen-fullscreen 0 -screen-width 1280 -screen-height 720 ..." --count 1
   ```

4. **Input channel matrix.** With a character standing in a safe area:
   ```powershell
   mm-core spike-input --hwnd 0x<HWND> --channel focus --key W --hold-ms 1500
   mm-core spike-input --hwnd 0x<HWND> --channel post  --key W --hold-ms 1500
   mm-core spike-input --hwnd 0x<HWND> --channel focus --key W --hold-ms 3000 --blur-hwnd 0x<OTHER>
   mm-core spike-input --hwnd 0x<HWND> --channel focus --click 640 360
   ```
   Record which channel moves the character, whether movement continues after focus
   leaves the window (the `--blur-hwnd` run), and whether the click lands at the new
   cursor position without a `--settle-ms` delay.

## Running (Milestones 1–2)

Adopt two running clients, 2×2 grid on the primary monitor, mirror ability keys,
rotate the master with F12, keep the slaves in Windows efficiency mode:

```powershell
mm-core run --adopt 12345 --adopt 23456 --layout grid --borderless --keys 1,2,3,4,5,6,7,8,F,R,SPACE --swap-key F12 --slave-eco
```

Launch three clients yourself, picture-in-picture, broadcast clicks while Caps Lock is held:

```powershell
mm-core run --launch "C:\...\RotMG Exalt.exe|<args A>" --launch "C:\...\RotMG Exalt.exe|<args B>" --launch "C:\...\RotMG Exalt.exe|<args C>" --layout pip --borderless --keys 1,2,3 --mouse-modifier CAPSLOCK
```

Every five seconds `run` prints capture and delivery counters and the latency percentiles
from capture to delivery. Ctrl+C restores window styles and exits; launched clients keep
running unless `--kill-on-exit` was given.

`run` options: `--adopt PID`, `--adopt-hwnd 0x..`, `--launch "EXE|ARGS"`, `--class`,
`--layout grid|stacked|pip`, `--grid RxC`, `--thumb PX`, `--monitor N`, `--master N`,
`--borderless`, `--keys LIST`, `--strategy focus|post`, `--mouse-modifier KEY`,
`--swap-key KEY`, `--slave-priority normal|below|idle`, `--slave-eco`,
`--slave-cpu-cap PCT` (share of total system CPU, launched clients only),
`--click-settle-ms N`, `--stats-every S`, `--kill-on-exit`.

## The limitation you will hit first

Unity reads input through Raw Input, which Windows delivers only to the foreground
window, and it clears its key state on focus loss. Tapped keys and clicks mirror to every
client; a *held* movement key cannot be mirrored to more than one client without
injecting into the game, which this project does not do. The architecture document
explains the options (Section 3.3).
