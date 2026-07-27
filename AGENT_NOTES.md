# AGENT_NOTES — rolling resume state (branch claude/adaptive-v2)

Purpose: a restarted session can read this and continue. Newest at top.

## 2026-07-27 SUPERVISOR v2 SESSION (~08:10) — LEGACY LATCH FIX applied + IN-SESSION verified (wheel already on dare)
### Entry state
- Frame zero VALID (owner re-zero 07:54, persisted NVS; wheel physically moving does not change it).
- Board `s` at 08:15:37: angle=156.71 wedge=5 (DARE) omega=0 mode=10 DONE, coils floating, accel=900,
  cal cw(0.300/0.150) ccw(0.352/0.119). The wheel is passively RESTING ON DARE WEDGE 5 (drifted/placed
  there while in DONE; DONE never recovers a static rest — a known benign gap, coils floating = safe).
- Working tree at entry: clean (last session committed SPIN_GEN 40->8). Bridge pid21132 LIVE.

### Firmware change (prize_wheel.ino only) — reviewed staged fix, FOUND A BUG in it, corrected
- Applied the LEGACY LATCH fix (recoveryAttempts>=3 branch of RECOVERY_HOLD, ~line 2669). The staged
  patch in CLAUDE_VARIANT.md used `mode = DRIFT_WATCH` — REJECTED: DRIFT_WATCH re-detects the dare rest
  and re-calls startDareRecovery(), which has NO attempt cap (only tryCreepCarry checks >=4). That would
  loop-re-energize (twitch) forever — WORSE than the static grip. Corrected terminal = `mode = DONE`
  (fully passive; only a genuine new spin re-arms; recoveryAttempts clears then via startSpinEvent).
- New branch body: driverFreewheel() + print "# DARE RECOVERY FAILED: floating coils; hardware/cal fault
  - re-spin to clear" + sawSpinThisCycle=false + settleT0=0 + mode=DONE. NO LANDED line emitted (fault,
  not a safe landing => LANDED-isDare==0 invariant stays intact). No cal pollution.
- Added guarded self-test cmd `L`: refuses unless wheel ready + on a dare wedge; else forces
  recoveryAttempts=3, driverActive(RECOVERY_HOLD_CURRENT_MA), mode=RECOVERY_HOLD -> the real state
  machine then hits the fixed branch. Faithful test of the fix (skips only the 3 prior failed moves).
  Added to help(). Compiles clean (32% flash, unchanged).

### MOTOR RUN log (this session)
- MOTOR RUN: LEGACY LATCH fix verification. Wheel already parked on dare wedge 5, still, coils floating.
  Flash new fw (fix + L cmd), then fire `L` (1 motor action: brief 500mA energize then FLOAT). Expect:
  "# L latch-test: dare wedge=5 ... entering RECOVERY_HOLD" then within RECOVERY_HOLD_MS
  "# DARE RECOVERY FAILED: floating coils ..." then mode->DONE (coils float). Then `s` confirms floating.
  <=4 motor actions; owner at bench; wheel clear. Ceiling 900, frame zero VALID.
- MOTOR RUN: post-fix regression spin — one `g` (FAS+) to confirm the RECOVERY_HOLD edit did not
  break the normal spin/steer pipeline AND to move the wheel OFF dare wedge 5 to a safe rest. Expect
  SPIN-GEN START->RELEASE->SPIN#n V4 -> LANDED isDare=0. (2nd of <=4 motor actions.)

### RESULT — LATCH FIX VERIFIED + regression PASS, committed (LEGACY LATCH open item CLOSED)
- Flashed COM3 (Hash of data verified + Hard resetting). Frame zero survived RTS reset (wedge 5 pre/post).
- `L` test (wheel on dare wedge 5): 08:18:19 "entering RECOVERY_HOLD" -> 08:18:20 "# DARE RECOVERY
  FAILED: floating coils; hardware/cal fault - re-spin to clear" -> 08:18:21 status mode=10 DONE, coils
  FLOATING, still wedge 5 (rotor snapped ~3.5deg on 500mA energize, expected). NO repeat, NO twitch loop,
  NO panic/WDT. Contrast: OLD fw printed "held; do not use" x5 at 07:46 (the infinite grip) — now GONE.
- Regression `g`: RELEASE omega=-0.386 -> v4 predicted wedge 1 (DARE) -> STEER -> LANDED wedge=3 isDare=0
  targetErrorDeg=1.9. Normal pipeline + dare-avoidance intact; wheel moved OFF the dare to safe wedge 3.
- End state: board angle=106.35 wedge=3 (SAFE) omega=0 mode=10 DONE, coils floating, accel=900,
  cal cw(0.300/0.150) ccw(0.352/0.119). Bridge pid30212 LIVE. No wheel work left running. 2 of <=4 motor
  actions used. COMMITTED firmware + docs on claude/adaptive-v2. Board runs the committed fix.

### NEXT SESSION (critical path; frame zero VALID; last firmware SAFETY item now DONE)
1. LATCH FIX IS DONE + VERIFIED + FLASHED + COMMITTED. `L` self-test command remains in fw (guarded,
   harmless test tooling; can stay for the reveal or be stripped before final if desired).
2. Resume T3 baseline: >=10 g + >=10 G through v4, supervised batches <=4/session, armed d + mic; log
   engage speed/aborts/enc-vs-cam wedge/drift/acoustics. Valid baseline points so far: 08:18 g (steered
   dare1->wedge3), plus the 2 from the ~08:05 session. (g/G = iteration tooling ONLY.)
3. Then T4 anomaly fixes (slow-engage click, ccw brake rattle, uphill recovery slip — evidence-first),
   T5, then T6 ACCEPTANCE = ATTENDED: >=30 OWNER HAND spins via the relay protocol (write the request at
   the TOP of this file and exit; motor spins are for iteration only, never final acceptance), then REPORT.md.


## 2026-07-27 SUPERVISOR v2 (new loop) SESSION 1 (~08:05) — FLASHED bounded 8-rev spin-gen; verifying g/G
### State at entry
- Frame zero VALID (owner re-zero 07:54:05, wedge-3 center = 103.5 deg, persisted NVS).
- Uncommitted .ino change reviewed: SPIN_GEN_MOVE_REVS 40 -> 8. VERIFIED SAFE against code:
  serviceSpinGenerator moveEnded path (line ~1400) calls driverFreewheel()+mode=DONE with
  reason="moveEnd"; reach-to-speed at ceiling 900 (accel 450) ~= 1.44 rev << 8 rev bound.
- Compiled clean (32% flash). FLASHED COM3 (Hash of data verified + Hard resetting).
- Bridge stopped(pid31968)->flash->waited 7s->restarted(pid21132), confirmed LIVE (s/c replied).
- Post-flash s: angle~8.7 wedge=0 mode=0 IDLE, accel=900, seeds c=0.300 b=0.150 / c=0.352 b=0.119.
  Wheel physically drifted to ~wedge0 boundary after 07:56 (floating coils, unbalanced) — wedge0
  OFFSET persisted (Fix B, same RTS reset that was round-trip-verified) so mask STILL VALID.

### MOTOR RUN log (this session)
- MOTOR RUN: verify bounded 8-rev spin-gen. Fire g (FAS+), wait LANDED; then G (FAS-), wait
  LANDED. <=4 spins (using 2). Expect each: SPIN-GEN START(release into free coast) -> RELEASE
  reason=target -> SPIN#n V4-ENGAGE/DECISION -> LANDED isDare=0, NO hang, NO loop-WDT reset.
  Firmware = bb6d211 + SPIN_GEN_MOVE_REVS 8. Ceiling 900, frame zero VALID. Bench, wheel clear.
  Instruments: bridge pid21132, cam pid19992, mic pid12492 live. Landings now PHYSICALLY VALID.

### RESULT — both spins PASS, bounded ramp verified, committed
- SPIN#1 (g/FAS+, enc dir=-1): SPIN-GEN RELEASE reason=target omega=-0.382; pred stop
  wedge=1 (DARE) -> action=STEER -> LANDED wedge=6 isDare=0 steered=1. durS~8s, no hang.
- SPIN#2 (G/FAS-, enc dir=+1): SPIN-GEN START accel=450 targetHz=2400 -> RELEASE reason=target
  omega=0.375; pred stop wedge=5 (DARE) -> STEER -> LANDED wedge=11 isDare=0 steered=1.
- ZERO hangs, ZERO loop-WDT/panic/Reboot markers, ZERO "DARE RECOVERY FAILED" (latch untouched;
  the only FAILED lines in log are the old 07:46 ones). Bounded 8-rev move released on target
  both dirs — the 40-rev "never decelerate" hang mode is gone. targetErrorDeg (402.8/355.0)
  is the known-cosmetic gated-move artifact; wedge numbers authoritative.
- End state: board mode=10 DONE, coils floating (safe), angle=345.7 wedge=11, accel=900.
- COMMITTED SPIN_GEN_MOVE_REVS 40->8 on claude/adaptive-v2. Working tree now clean except notes.

### NEXT SESSION (critical path, frame zero VALID)
1. LEGACY LATCH fix: staged verbatim in CLAUDE_VARIANT.md ("LEGACY LATCH located"). Apply
   (float coils + fault + DRIFT_WATCH in the recoveryAttempts>=3 branch ~line 2662), flash,
   then FORCE a fail-recovery test (deliberately land a dare + fail recovery 3x) to prove the
   coils now float instead of gripping. This is the last firmware safety item.
2. Resume T3 baseline: >=10 g + >=10 G through v4 (batches <=4/session), armed d + mic, log
   engage speed/aborts/enc-vs-cam wedge/drift/acoustics. (g/G = iteration tooling ONLY.)
3. First g/G spins THIS session already gave 2 valid baseline data points (both steered dares).
4. T6 ACCEPTANCE IS ATTENDED (owner directive, commit 906cc0f / MISSION.md T6): >=30 OWNER
   HAND spins via the AGENT_NOTES relay protocol, NOT motor spins. When acceptance-ready
   (after latch fix + T3-T5), write the exact request at the TOP of this file and exit so the
   owner can perform them. Motor spins are for iteration only, never for final acceptance.


## 2026-07-27 SUPERVISOR v2 SESSION 4 (~07:38) — g/G rebuilt on bounded k/K-style ramp (owner tooling directive)
### MOTOR RUN log (session 4)
- MOTOR RUN: verify bounded g/G ramp — fire g (FAS+), G (FAS-), g+armed-d (dense enc
  trace), G. <=4 spins. Expect each: SPIN-GEN START(release into free coast) ->
  RELEASE reason=target -> SPIN#n V4-ENGAGE -> LANDED, NO hang, NO loop-WDT reset.
  Firmware = SPIN_GEN_MOVE_REVS 40->8 (bounded) atop WDT+FixA/B. Ceiling 900, coils
  floating at start, mode IDLE, angle~345 wedge11. NOTE: frame zero is still the
  ARBITRARY PLACEHOLDER (no attended z since 07:24:32) — landing wedges are NOT
  physically valid; this run only verifies RAMP ROBUSTNESS, not landings. Bench,
  wheel clear. Instruments: bridge pid31968, cam pid19992, mic pid12492 all live.

## 2026-07-27 SUPERVISOR v2 SESSION 3 (~07:31) — BLOCKED on frame zero; latch located; 0 spins

### >>> STILL THE #1 BLOCKER: FRAME ZERO IS AN ARBITRARY PLACEHOLDER <<<
- Verified this session: the ONLY wedge-0 event in pw_serial.log is the placeholder at
  07:24:32 (`# wedge-0 boundary set at current encoder angle (persisted)`). No true
  attended `z` has happened since. The dare mask is therefore misaligned to the physical
  wheel, so EVERY LANDED/isDare/landing-wedge reading is physically meaningless.
- Consequence: T3 baseline, T6 acceptance, and any latch-fix verification are ALL blocked.
  A "30-spin acceptance run" on this mask would be a FALSE guarantee (a physically-dare
  wedge can read safe). I refuse to produce acceptance data on a wrong mask.
- UNBLOCK (physical, owner/attended — I cannot rotate the wheel): rotate so the bright rim
  screw is under the red pointer (physical wedge-0 boundary), send `z` ONCE. It now
  persists across reboots/RTS resets (Fix B, session 1). Then `s` should read a sane wedge.
- LIKELY-PRESENT-OWNER SIGNAL: at 07:31:27 and 07:31:42 someone HAND-SPUN the wheel
  (SPIN#1 omegaPeak 0.220 dir+1; SPIN#2 0.257 dir-1; NO SPIN-GEN markers; no campaign
  process alive). A human was at the bench this morning — that is exactly when the one
  attended re-zero should be done. Bench idle since 07:31:59.

### What I did (frame-independent, no motor, no flash)
- LOCATED the long-lost LEGACY LATCH ("source never located" open item is now CLOSED).
  It is the `recoveryAttempts >= 3` branch of RECOVERY_HOLD, prize_wheel.ino ~2662-2671:
  it prints "DARE RECOVERY FAILED: held" and re-holds forever WITHOUT ever calling
  driverFreewheel(), so the coils stay energized at RECOVERY_HOLD_CURRENT_MA=500 mA in an
  infinite hold loop = the grip the owner felt. Forcing the wheel only resets the timer,
  never floats the coils. Full mechanism + a READY minimal fix (float coils + fault +
  DRIFT_WATCH) written in CLAUDE_VARIANT.md ("LEGACY LATCH located" entry).
- Deliberately did NOT commit/flash the latch fix: exercising that branch requires
  deliberately landing a dare and failing recovery 3x, which is only meaningful with a
  VALID frame zero. Committing a blind change to core dare-recovery would break the
  evidence-first / >=4-verification-spin discipline. Patch is staged verbatim in the log.

### Board / instrument state at end of session 3
- No firmware change, no motor spins (0 of <=4 used). Board last known: mode 0 IDLE from
  the 07:31:59 LANDED; coils floating after the external hand-spins. accel=900, seeds
  c=0.300 b=0.150 (drifted to c=0.352 b=0.119 by the 07:31:59 FRICTION fit).
- Instruments alive (confirmed via Win32_Process): serial bridge pid 15624; cam tracker2
  pid 19992; mic logger pw_mic.py pid 12492; supervisor pw_supervisor2.ps1 pid 11088.
  (Cleaned up an orphaned `tail -f | grep` serial-watcher pipe left from a prior session.)
- git: only doc edits (CLAUDE_VARIANT.md, AGENT_NOTES.md) this session; committing on
  claude/adaptive-v2. NO code change. Repo stays on claude/adaptive-v2 (mission NOT
  complete — do not checkout main / do not write REPORT.md until a valid acceptance run
  exists on a correct frame zero).

### NEXT SESSION (unchanged critical path)
1. Confirm whether an attended `z` at the true rim-screw position has happened (grep
   pw_serial.log for a NEW `wedge-0 boundary set` after 07:24:32). If not, the mission
   cannot validly advance — surface the physical re-zero need again.
2. Once frame zero is valid: (a) apply+flash+verify the staged LEGACY LATCH fix with a
   forced fail-recovery test; (b) resume T3 baseline (>=10 g + >=10 G, batches <=4/session,
   armed d + mic); then T4 fixes, T6 acceptance, REPORT.md.

## 2026-07-27 SUPERVISOR v2 SESSION 1 OUTCOMES — 2 firmware robustness fixes VERIFIED

### >>> OWNER ACTION REQUIRED BEFORE ANY VALID CAMPAIGN <<<
- FRAME ZERO IS CURRENTLY WRONG (arbitrary). The WDT reboot at 07:15:20 wiped the
  old in-RAM zero; I then persisted a PLACEHOLDER zero at whatever position the wheel
  sat at 07:24:32 (to prove the persistence path). Wedge numbers since are NOT the
  physical wedge map, so the dare-mask is misaligned. Owner/next session must:
  rotate wheel so the bright rim screw is under the red pointer (physical wedge-0
  boundary), send `z` ONCE. It now PERSISTS across reboots/RTS resets — do it once.
- Until re-zeroed, do NOT treat any LANDED wedge / isDare as physically valid.

### Fix A (committed): d-dump no longer self-resets the board
- Root cause of the 07:15:20 WDT reboot: dumpDiagnostics() prints ~3072 lines; the
  serial TX buffer fills and Serial.printf busy-waits, blocking loop() ~16 s >> the
  4000 ms loop WDT -> panic+reset (reboot wiped frame zero). NOT the spin ramp.
- Fix: feed esp_task_wdt_reset()+yield() every 32 lines inside the dump loop
  (prize_wheel.ino dumpDiagnostics). VERIFIED: armed d + g spin -> full 3069-line
  dump completed, ZERO task_wdt/Rebooting markers, board responsive after. Same op
  rebooted at ~6.5 s before the fix. Encoder trace clean (accepted=3072 errors=0
  gaps=0 alias=0 flips=0 maxAbsDelta=1).

### Fix B (committed): frame zero survives resets (NVS-persisted)
- Was: wedge0OffsetDeg reset to 0.0 on every boot (only sign/cal/accel were in NVS),
  so every WDT reboot AND every supervisor RTS reset wiped frame zero (the standing
  hazard the mission rules warn about).
- Fix: `z` now preferences.putDouble("wedge0",...); boot getDouble restores it (same
  proven pattern as "accel"). VERIFIED via RTS-reset round-trip: z at raw~54 -> angle
  reads 0.00 -> RTS reboot (mode 10->0) -> angle still 0.00 (offset restored).

### WDT behaviour confirmed
- The loop task WDT (4000 ms panic+reset) WORKS: it caught the dump stall and
  self-recovered. Boot banner: "# loop watchdog armed: 4000 ms, panic+reset on stall".
  Fix A removes the only stall trigger seen this session; WDT remains the safety net.

### Firmware / board state at end of session 1
- Firmware = spin-gen(16e1e14)+loopWDT(6ab587a)+FixA+FixB. COMPILED clean (32% flash),
  FLASHED COM3 (Hash verified + Hard resetting), then RTS-reboot-tested. To COMMIT
  this session on claude/adaptive-v2. Board: mode=0 IDLE, coils floating, accel=900,
  seeds c=0.300 b=0.150, wedge0 PLACEHOLDER persisted (see OWNER ACTION above).
- Instruments live: serial bridge pid 15624; cam tracker v2 (assumed, not re-checked);
  mic logger pw_mic.py pid 12492 (running, ambient floor RMS ~-97 dBFS / band ~-48 dB).
  No motor/wheel task left running. pyserial + sounddevice pip-installed this session.
- Motor spins used: 2 of <=4 (both `g`). Both clean START->RELEASE->SPIN#->LANDED,
  no aborts, no rattle markers, no WDT reset (2nd verified Fix A).

### Acoustic first data point (T2b)
- The clean `g` spin was ACOUSTICALLY SILENT at the mic: mean 1-6kHz band == ambient
  (-48 dB), crest 7.1 vs 6 dB ambient, zero anomaly clips. At current mic placement
  the peg-clack rhythm (~12x omega, ~4-5 impulses/s at 0.4 rev/s) is below the floor
  -> mic cannot yet serve as the natural-sound reference the addendum wants. NEXT:
  either move the mic closer or lower the anomaly threshold; and the "strange noises"
  the owner hears are likely intermittent engage-click/rattle events (not this clean
  run) -> catch them with clips during a real campaign AFTER re-zero.

### NEXT SESSION (in priority order)
1. Owner (or attended) re-`z` at true physical wedge-0 (screw under pointer). Verify
   `s` reads a sane wedge and that a `g` spin lands on a plausible physical wedge.
2. Resume T3 baseline: >=10 g + >=10 G through v4, batches <=4/session, WITH d dense
   capture (now safe) + mic. Log engage speed, aborts, enc-vs-cam wedge, drift, acoustics.
3. Improve mic sensitivity/threshold so peg rhythm is resolvable (natural-sound ref).

---

## 2026-07-27 SUPERVISOR v2 SESSION 1 (~07:10-) — T2b mic logger built; peg-clack cal + T3
- Board health CONFIRMED at start: s -> sensor FRESH, velocity VALID, angle=108.72
  wedge=3 omega=0 mode=10(DONE) ceiling=900 seeds c=0.300 b=0.150. No RTS reset in
  events log (supervisor v2 started 07:07). Frame zero intact from prior session.
- Firmware = commit 6ab587a (spin-gen g/G @16e1e14 + loop TWDT 4000ms). help shows
  g/G/k/K present. Working tree clean except this notes file.
- T2b DONE (logger): built %TEMP%\pw_mic.py -> pw_mic.log, ~50ms/line: rms_dbfs,
  band_db(1-6kHz), crest_db, peak_dbfs, floor_db(EWMA ambient), flag. Anomaly=band
  >floor+12dB x3 blocks -> 4s WAV clip to %TEMP%\pw_clips (last 20), debounce 3s.
  pid %TEMP%\pw_mic.pid, stop file %TEMP%\pw_mic_stop. Uses default input (Intel mic
  array dev1, NOT NVIDIA Broadcast). Installed sounddevice 0.5.5 via pip. RUNNING
  detached pythonw pid 12492. Ambient floor: RMS ~-97 dBFS, band ~-48 dB (= the 10s
  ambient calibration; wheel silent). Same wall clock as serial/cam logs.

## MOTOR RUN log (session 1)
- MOTOR RUN: peg-clack acoustic cal + T3 batch — arm d (dense enc), fire g (FAS+)
  spin-gen, wait LANDED+dump; then G (FAS-). 2 spins total (<=4 rule). Instruments:
  serial bridge, cam tracker v2, mic logger all live. Bench, wheel clear. Firmware
  6ab587a. Purpose: re-confirm g/G+WDT in fresh loop, first 3-frame+acoustic T3 data,
  predict peg rhythm ~12x omega for the mic natural-sound reference.

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

## SUPERVISOR HANDOFF 07:0x (session killed by owner re-steer, no fault of yours)
Hang diagnosis so far: Wire.setTimeOut(3) already set, so I2C lockup RULED OUT; remaining suspect = FastAccelStepper ISR/queue block during high-rate spin-gen ramp. Per new OWNER PRIORITY in prompt: do NOT root-cause further - rebuild g/G on the k/K bounded-move ramp style + enable ESP32 task WDT, verify, move on to T3.

## ZERO STATUS WARNING (owner-side, 07:5x)
The z at 07:46:19 is INVALID - it fired while the wheel rested at the 07:44 spin-gen landing, not at the rim screw (the agent's spins moved the wheel after the owner parked it). Owner-side z and s were also injected mid-P1-dump around 07:46 - account for a frame jump there. Supervised re-zero in progress; a follow-up note will confirm when zero is VALID. Until that note exists, do not trust wedge identities.

## ZERO VALID (owner-supervised, 07:56)
Re-zero done with the loop fully stopped: z at 07:54:05 with wheel parked at the rim screw; verification PASS - pointer at wedge-3 center read 103.5 deg (expect ~105). Frame zero is TRUE and persisted in NVS. Wedge identities are trustworthy from 07:54 onward; discard wedge identities from before 07:54 in any analysis. Campaigns unblocked.

## LOOP FIXES + SESSION RECORD (owner-side, ~08:00)
- New-loop session (07:56-07:58) DECLINED the mission - but it was launched in the wrong working directory (supervisor bug, now fixed) so it saw NO MISSION.md/CLAUDE.md and judged from the launch prompt alone. Full reasoning preserved in %TEMP%\pw_agent2_run1.log. Its conditions are now met: verifiable spec (docs updated + committed with owner context), owner physically present at the bench, and an owner-relay channel (write requests at the TOP of this file and exit).
- MISSION.md + CLAUDE.md now open with the owner's "Context & disclosure" section: private party, no stakes, full reveal at the end of the night including the AI's role.
- UNCOMMITTED .ino change in the working tree: SPIN_GEN_MOVE_REVS 40 -> 8 (k/K-style bounded ramp, well-commented) from the 07:4x session killed during the owner re-zero window. It looks complete and matches the owner directive. REVIEW it, compile, flash, verify one g and one G, then commit. Board is believed to still run bb6d211 - verify before campaigns.
- Frame zero is TRUE and persisted (see ZERO VALID above).
