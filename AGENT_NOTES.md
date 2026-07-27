# AGENT_NOTES — rolling resume state (branch claude/adaptive-v2)

Purpose: a restarted session can read this and continue. Newest at top.

## 2026-07-27 relaunch (~07:00) — T1 HANG FIXED; RESUME AT T3

### FIRST-PRIORITY RESOLVED
- The 00:49:50 `G` spin hang is FIXED. Root cause class: `loop()` blocked inside a
  library/peripheral call during the spin-gen motor ramp (12 s software timeout
  provably never fired; no software infinite loop; I2C already bounded by
  Wire.setTimeOut(3)), and there was NO hardware watchdog to recover. Fix: loop task
  now subscribed to ESP32 Task WDT, 4000 ms, panic+reset; fed once per loop().
  Full postmortem in CLAUDE_VARIANT.md ("T1 hang postmortem + loop-watchdog fix").
- Firmware change: prize_wheel.ino only — added `#include "esp_task_wdt.h"`,
  `LOOP_WDT_TIMEOUT_MS 4000`, WDT init/reconfigure+add at end of setup(),
  `esp_task_wdt_reset()` as first line of loop(). TMC init block untouched.
- COMPILED clean (32% flash) + FLASHED to COM3 (Hash of data verified + Hard
  resetting). Not yet committed at time of this note -> commit right after (this
  session commits it on claude/adaptive-v2).
- VERIFIED: 4 supervised spins (g,G,g,G) all clean START->RELEASE->SPIN#n->LANDED,
  no hang / no WDT reset / no refusals. Landed wedges 6/11/10/3, all isDare=0.
  Spin #4 target 0.442 rev/s (> the 0.436 that hung old fw) completed fine.

### BOARD/INSTRUMENT STATE AT END OF SESSION
- Board healthy, mode=IDLE, coils floating (safe). Boot banner shows
  `# loop watchdog armed: 4000 ms`.
- Serial bridge RESTARTED and live, new pid in %TEMP%\pw_bridge.pid (was dead-handle
  once after flash; fixed by full stop+restart — remember the >=5 s wait rule, and
  if pid alive but log stays empty, stop+restart the bridge). Cam tracker v2 assumed
  still running (not re-checked this session). No wheel work left running.
- Firmware now = commit 16e1e14 (spin-gen) + this WDT commit. Ceiling 900, cal seed.

### NEXT: resume T3 baseline campaign (was interrupted by the hang)
- Run >=10 g + >=10 G spins through v4 in supervised batches of <=4 per session.
  Per spin capture: engage speed, ramp accel, aborts, landing wedge (enc) vs
  cam-implied wedge, post-release drift. Host tool: %TEMP%\pw_baseline.py.
- Acoustic addendum (T2b) is still PENDING and is owner-flagged do-FIRST for the
  broader mission: build %TEMP%\pw_mic.log logger before/alongside T3 so T3 spins
  also get acoustic data. (Not started.)
- Watch for any watchdog reset in the serial log during campaigns; if one fires,
  grab the panic backtrace to pin the true stall cause.

## 2026-07-27 session

### State
- Instruments confirmed alive at session start: serial bridge (COM3) + cam
  tracker v2. Cam calibration file `%TEMP%\pw_cam_cal.txt` did NOT exist ->
  T2 still pending.
- Firmware baseline at session start: v4.4 (commit 47eebd1).
- Persisted accel ceiling reported by `c`: 900 sps2. Friction seeds still at
  c=0.300 b=0.150 both dirs (calibration is fresh/seed).

### T1 — spin generator `g`/`G`  (IN PROGRESS)
Added to prize_wheel.ino:
- Tunables SPIN_GEN_* (target 0.35-0.45 rev/s, 600 mA, accel = ceiling/2,
  40-rev move, 12 s timeout).
- Mode `SPIN_GEN` appended to enum (after DRIFT_WATCH).
- State vars spinGen*.
- startSpinGenerator(fasDir) / serviceSpinGenerator(): ramp up at half the
  persisted ceiling, RELEASE (driverFreewheel) when encoder omega reaches the
  random target, hand off to FREE_SPIN via mode=DONE. `g`=FAS+, `G`=FAS-.
- Wired into loop switch + handleSerial + help().
Rationale for release-by-encoder-omega: guarantees the wheel actually reached
speed (not just the commanded profile), and target < GUEST_OVERRIDE (0.80) so
no override re-latch. Direction invariant safe: motion never reverses.

NEXT STEP: compile-only, then flash (stop bridge first!), then verify `g`/`G`
produce SPIN-GEN START/RELEASE then a normal SPIN#n START/V4-ENGAGE.

### Flash reminder (from CLAUDE.md)
Stop bridge -> esptool -> wait >=5 s -> restart bridge. cli + --libraries
mandatory. Judge upload by 'Hash of data verified' + 'Hard resetting'.

## MOTOR RUN log
- 2026-07-27 ~00:17 flashed T1 (commit 16e1e14). Verified help shows g/G.
- MOTOR RUN: first `g` (FAS+) spin-generator test — expect SPIN-GEN START -> RELEASE -> SPIN#n V4-ENGAGE coast. Bench, wheel clear.

- MOTOR RUN: `G` (FAS-) spin-generator test, opposite direction.

- MOTOR RUN: T2 cal capture — arm d (dense encoder), fire g, dump, fit cam vs enc.

- MOTOR RUN: T2 cal — arm d, fire k (bounded 108 deg cal move), dump, fit.

- MOTOR RUN: T2 cal series — 6 alternating k/K bounded moves for net-rotation scale fit.

- MOTOR RUN: T2 verify — 3 fresh k/K moves, check vs stored scale.

## T2 DONE (00:38): scale=0.837491 sign=+1, resid 0.29deg RMS (7 moves), check 0.76/0.86deg PASS.
Host tools in %TEMP%: pw_cam_cal_net.py (fit/check net-rotation), pw_cam_cal.py (streaming, deprecated), pw_baseline.py.
Cal file %TEMP%\pw_cam_cal.txt written.
NEXT: T3 baseline campaign - >=10 g-spins + >=10 G-spins through v4, catalog wheel-speed-up aborts, landings, cam-enc net agreement.

- MOTOR RUN: T3 baseline campaign — 20 spins alternating g/G (10 each) through v4 pipeline.
- MOTOR RUN: T3 baseline campaign (resumed) — driving 20 fresh spins alternating G/g via bridge, waiting for each LANDED. Build=T2 (c442323), ceiling=900, cal seed. Bench, wheel clear.

## 2026-07-27 session (relaunch, ~07:00) — T1 hang fix
- MOTOR RUN: watchdog-fix verification — one `g` (FAS+) then one `G` (FAS-) spin generator run, expect SPIN-GEN START -> RELEASE -> SPIN#n V4-ENGAGE -> LANDED, no hang. Bench, wheel clear. Firmware = spin-gen + loop TWDT (4000ms panic).
