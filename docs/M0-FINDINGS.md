# Milestone 0 findings

Run on 2026-09-08 against the live Production client, using `mm-core` built from
commit `a743238` with Visual Studio Build Tools 2026 (MSVC 14.50).

| Item | Value |
|---|---|
| Client version | 7.0.0.2.0 (`Version number from backend` in Player.log; `RotMG #7.0.0.2.0` in the in-game log panel) |
| Engine | Unity 6000.0.58f2, Direct3D 11, Unity **Input System** package (`Input System module state changed` / `Input System polling thread` in Player.log) |
| Client exe | `%LOCALAPPDATA%\RealmOfTheMadGod\Production\RotMG Exalt.exe` |
| Launcher exe | `C:\Program Files\RotMG Exalt Launcher\RotMG Exalt Launcher.exe` (a Steam copy also exists) |
| Logs / prefs | `%USERPROFILE%\AppData\LocalLow\DECA Live Operations GmbH\RotMGExalt\Player.log` |
| Test machine | Windows 11 Pro 26200, 16 logical cores, AMD Radeon PRO integrated GPU (14 GB shared VRAM), 1536x960 desktop |

Everything below was observed, not inferred. Trial counts are given where they matter.

## 1. How the launcher starts the client

The launcher spawns the client with a single argument:

```
"<client exe>" data:{platform:Deca,guid:<b64>,token:<b64>,tokenTimestamp:<b64>,tokenExpiration:<b64>,env:4,serverName:}
```

- Total command line 688 chars; the `data:` argument alone is 612 chars. No spaces inside it.
- `tokenTimestamp` is a base64 Unix time; `tokenExpiration` is base64 `86400`, so the token is
  good for 24 h from issue.
- The launcher process stays alive after the client starts and is the client's parent.
- The launcher does **not** pass any resolution or window switches of its own.

## 2. Multiple instances from one install

**No single-instance lock.** `mm-core spike-launch` started two extra clients with the
same `data:` argument while the launcher's own client was running. All three stayed alive
through the 15 s observation window and all three logged in, landing the same character
in the Nexus at once.

| Detail | Observation |
|---|---|
| Token reuse | The same token logged in three clients within a couple of minutes. Whether the server tolerates this long-term, or for distinct accounts, is untested (one account available). |
| Shared state | All instances write the same `Player.log` (each new instance rotates it to `Player-prev.log`) and share PlayerPrefs under HKCU. Section 4.1 of the architecture doc stands. |
| Shutdown | Closing clients produced clean `Input System ... Shutdown` sequences in the log; no server kick was observed in roughly 10 min with three instances up. |

## 3. Window class and Unity switches

| Property | Default launch | With `-popupwindow -screen-fullscreen 0 -screen-width 1280 -screen-height 720` |
|---|---|---|
| Class | `UnityWndClass` | `UnityWndClass` |
| Title | `RotMGExalt` (the launcher window is also `UnityWndClass`, titled `RotMG Exalt Launcher`; filter by PID, not class alone) | same |
| Style | `0x14CA0000` = `WS_VISIBLE`, `WS_CLIPSIBLINGS`, `WS_CAPTION`, `WS_SYSMENU`, `WS_MINIMIZEBOX`; **no `WS_THICKFRAME`** | `0x94000000` = `WS_POPUP`, `WS_VISIBLE`, `WS_CLIPSIBLINGS`; no caption |
| Ex-style | `0x00000100` (`WS_EX_WINDOWEDGE`) | `0x0` |
| Size | 1358x878 outer | Opened at 1024x576, then the client **resized itself to 1344x840** within a few seconds |

Conclusions:

- `-popupwindow` works and is the cheapest borderless path. Keep the style-stripping code
  as the fallback for adopted clients.
- `-screen-width` / `-screen-height` are **overridden** by the client's saved resolution
  shortly after startup. `mm-core` must size windows itself with `SetWindowPos` once the
  window settles (the layout engine already does this); do not rely on the flags.
- The default window is not user-resizable, so `SetWindowPos` is the only way to resize it.

## 4. Input channel matrix

Target: the launcher-started client, character standing in the Nexus. Movement was judged
from `PrintWindow` captures before and after each step. Distances were calibrated by
comparing a 1.5 s hold with a 3 s hold on the same axis.

| Channel | Key tap / hold | Held key survives focus loss? | Mouse click |
|---|---|---|---|
| **Strategy A**: focus + `SendInput` (scan codes) | **Works.** W and S moved the character for the full hold. | **No.** With focus moved to another window 1.5 s into a 3 s hold, the character travelled the 1.5 s distance (2 of 2 trials). Unity clears key state on focus loss. | **Works with `--settle-ms 0`.** Move, down, up landed on the Log toggle at the new cursor position in one call; the hover tooltip appeared at the target spot (1 of 1). |
| **Strategy C**: `PostMessage` `WM_KEYDOWN/UP`, `WM_LBUTTONDOWN/UP` | **No effect** on movement (1 of 1). | n/a | **No effect** in 2 of 3 trials; the one apparent toggle coincided with the panel closing on its own. Treat as non-functional for this client. |

Additional input observations:

- **The first key after a focus change was dropped once** (1 of 3 first-key trials showed no
  movement; the next key in the same session worked). Add a one-frame settle (about 17 ms)
  between a successful `SetForegroundWindow` and the first `SendInput`, and treat the
  held-key re-assert after every focus return as mandatory.
- `SetForegroundWindow` from `mm-core` succeeded on every attempt while
  `ForegroundLockGuard` was active. A plain PowerShell `SetForegroundWindow` failed for
  background clients, so the guard is doing real work.
- **Elevated clients are unreachable.** Starting the game from the installer's finish page
  launches the launcher, and therefore the client, elevated. From a non-elevated tool,
  `OpenProcess(PROCESS_QUERY_INFORMATION)` fails, the command line is unreadable, and UIPI
  silently drops `SendInput` and `PostMessage`. The adopter must detect an integrity-level
  mismatch and refuse with an explanation instead of failing silently.
- `PrintWindow` with `PW_RENDERFULLCONTENT` captures a background game client correctly.
  It returns black for the launcher window.

## 4a. Later the same day: concurrency and focus limits

- **The server refused a fourth concurrent login on the same account.** With three
  clients already in the Nexus, a fourth launched with the same token reached the loading
  screen, then showed "Oops... Please wait a bit and reconnect" and exited a minute later.
  Three worked repeatedly. Multi-account setups will need one token per account; the
  per-account concurrency limit is somewhere at three or four.
- Freshly spawned clients land on the daily **Login Calendar** popup, which blocks the
  Nexus until dismissed. The launcher-started client did not show it (already claimed).
  Automation that expects a clean Nexus must send Escape or click the close button first.
- **Focus stealing fails while the user is actively typing or clicking elsewhere.**
  With the foreground lock timeout zeroed, `SetForegroundWindow` still failed on three
  consecutive attempts while the desktop app had the keyboard. Windows suppresses focus
  changes during active user input. The broadcaster's focus-cycle strategy therefore
  cannot coexist with the user working in another application. The UI should say so.

## 5. Baseline resource cost per idle client

Measured in the Nexus, windowed 1358x878, default in-game settings, three clients up,
5 s CPU sample:

| Client | CPU (one core = 100 %) | Working set | Private bytes |
|---|---|---|---|
| Launcher-started | 154 % | 3.7 GB | 4.4 GB |
| Spawned #1 | 233 % | 3.2 GB | 4.2 GB |
| Spawned #2 | 185 % | 3.3 GB | 4.2 GB |

About 10 GB of working set and roughly five to six cores for three idle clients. The
memory-pressure launch guard in Section 4.4 is not optional; on a 16 GB machine the fourth
client is the first one at risk.

## 6. Milestone 1 exit test, first attempt (2026-09-08, 19:26-19:36)

Setup: three clients on one account (one master, two slaves; the server refused a
fourth), 2x2 grid, borderless, `--slave-eco`, one mirrored key (Numpad 5) tapped every
500 ms by a `SendInput` driver for 580 s, hook set to accept foreign injected input.

| Metric | Value |
|---|---|
| Events captured | 2182 (of about 2268 sent); **0 dropped** by the ring |
| Deliveries | 2808 (4364 needed for two slaves) |
| Delivery throughput | about 4.8 per s, so **about 200 ms per focus-cycle delivery** |
| p50 / p99 | both in the histogram's overflow bucket, i.e. **> 51.2 ms** |
| Max | grew linearly with wall-clock time to 160 s: the ring backlog, not a single slow event |
| Focus failures | 431 (roughly 20 % of events), arriving in bursts |
| Inject failures | 0 |
| Hook | stayed installed for the full 10 minutes |

**Verdict: fails the exit criterion by more than an order of magnitude.** The hook and
ring are fine; the focus cycle is the bottleneck. Each `SetForegroundWindow` handoff to
a slave costs on the order of a background Unity frame, and slaves in efficiency mode
pump messages slowly, so the broadcaster falls behind a 2 Hz key rate. See section 7
for the variant runs that isolate the cause.

Bugs found by this run and fixed in the same session:

- `run` buffered all its output when stdout was a file; stats are now flushed each interval.
- Shutdown waited forever on a broadcaster thread stuck in a focus call; `Broadcaster::stop`
  now takes a timeout and `run` exits after restoring window styles.
- `AttachThreadInput` is skipped for hung windows, and `--no-attach` disables it entirely.
- A 32-byte `INPUT` struct is silently rejected by `SendInput` on x64 (needs 40). This bit the
  test driver, not `mm-core`, but the inject path's own struct size is worth an assert.

## 7. Focus-cycle variants (90 s each, same setup, key tapped every 500 ms)

| Variant | Deliveries / needed | p50 | p99 | Max | Focus failures | Backlog? |
|---|---|---|---|---|---|---|
| eco + AttachThreadInput (as in section 6) | 341 / 608 | > 51 ms | > 51 ms | 19.9 s | 111 | yes, and the broadcaster thread hung at shutdown (the new 3 s stop timeout caught it) |
| no eco + AttachThreadInput | 451 / 832 | > 51 ms | > 51 ms | 14.0 s | 50 | yes |
| no eco, **no AttachThreadInput**, 5 ms focus wait | 383 / 568 | **25 ms** | **34 ms** | **36 ms** | 185 (33 %) | **no** |

`AttachThreadInput` is the 200 ms. It synchronises with the foreground thread's message
queue, and a Unity client in the background pumps once per (throttled) frame. Without it,
`SetForegroundWindow` under a zeroed foreground-lock timeout lands in about one frame per
slave, which is where the 25 ms p50 for two slaves comes from. The remaining problem is
the 33 % focus-failure rate, which looks like the 5 ms activation wait expiring before
Unity processes the activation. Section 8 varies that wait.

## 8. Activation wait (no AttachThreadInput, 90 s each)

| Variant | Deliveries / needed | p50 | p99 | Max | Focus failures |
|---|---|---|---|---|---|
| 20 ms wait | 510 / 614 | 36 ms | > 51 ms | 131 ms | 55 (9 %) |
| 40 ms wait | 525 / 584 | > 51 ms | > 51 ms | 245 ms | 54 (9 %) |
| **20 ms wait + `--slave-eco`** | **568 / 568** | **33 ms** | **48 ms** | **57 ms** | **0** |

The failure bursts in the first two rows line up with periods of desktop activity rather
than with the setting, so treat the 9 % as environmental. The last row is the
configuration to ship and is now the default (`AttachThreadInput` off, 20 ms wait;
`--attach` restores the old path for experiments).

**Milestone 1 verdict.** The pipeline is sound: no hook drops, no ring drops, no inject
failures across ~30 minutes of runs. But the exit criterion as written (p99 added latency
of at most one frame with three slaves) is not reachable with sequential focus cycling.
Each slave costs about one Unity frame to accept activation, so latency scales as roughly
one to one and a half frames per slave: 33 ms median and 48 ms p99 for two slaves, so
expect about 50 ms median and 70 ms p99 for three. Options:

1. Restate the criterion per target (each slave receives the key within one frame of the
   previous one) and accept two to four frames of total spread on ability keys. Cheapest.
2. Deliver in parallel by giving each slave its own session (Strategy D). The only way
   to beat one frame per slave, at the cost of no live preview.
3. Raise the background frame rate of slaves so activation is processed sooner. Worth a
   quick check of Exalt's settings, since efficiency mode did not hurt.

## Impact on the roadmap

1. **Strategy A is the only working channel.** Drop `PostMessage` from the default binding
   strategy; keep the code path behind a flag for future client builds.
1. **Never use `AttachThreadInput` in the focus cycle** and wait a full frame for activation
   (now the defaults). Restate the Milestone 1 latency target per slave (section 8).
2. **Held movement keys cannot be mirrored** across clients, as predicted. The product
   decision in Section 3.3 (accept tap-only mirroring, or investigate Strategy D) is now live.
3. Add a post-focus settle and keep the held-key re-assert.
4. Size windows with `SetWindowPos` after the client's self-resize; do not trust `-screen-*`.
5. Add an integrity-level check to adopt and launch.
6. The token format lets a Milestone 6 integrated launcher reuse a captured `data:` blob
   for up to 24 h without re-authenticating. Store it DPAPI-encrypted only.
