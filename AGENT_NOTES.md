# AGENT_NOTES — rolling resume state (branch claude/adaptive-v2)

Purpose: a restarted session can read this and continue. Newest at top.

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
