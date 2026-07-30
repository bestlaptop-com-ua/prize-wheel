# TASK_FRAME_V5 - Rebuild wheel frame on static absolute anchor (fixes angle-error class)

## Read first, in order
1. FINDINGS_2026-07-28_frame_forensics.md  (root cause + verified physical frame)
2. AGENT_NOTES.md, section "2026-07-28 owner+assistant bench session"
3. MISSION.md + CLAUDE.md (owner context; party prop, full reveal at end of night)
BOARD STATE: the ESP32 currently runs pw_diag/pw_diag.ino (motor-off diag), NOT the
main firmware. That is expected. You will flash your rebuilt firmware over it.

## Objective
Replace the wheel-frame layer of prize_wheel/prize_wheel.ino so wedge identity is
computed from the AS5600 absolute register via a static map and can NEVER be corrupted
by reboots, re-primes, or blind I2C gaps. Keep all other machinery (motor control,
current ramps, fight-aborts, spin detection, v4 engagement, FRZ-EVT, auto-calibrator).
This is a frame refactor, not a rewrite.

## Contract
1. NVS (namespace "prizewheel"): new key "rawZero" (uint16). Load at boot; default 3807
   (measured 2026-07-28, leading edge of label 0 under pointer). Do NOT erase or disturb
   any other keys (auto-calibrator profile keys must survive).
2. Wedge-truth frame (the ONLY source of wedge identity):
     wheelAngleDeg = norm360((rawZero - raw) * 360.0 / 4096.0)
     currentWedge  = (int)(wheelAngleDeg / 30) % 12
   Label N spans [30N, 30N+30). Labels are physical: 0 green, 1 orange (DARE), 2 blue,
   3 red, 4 yellow, 5 purple (DARE), ... sequential to 11. dare_mask (1<<1)|(1<<5)
   UNCHANGED. Rim screw sits at the 11|0 boundary = 0 deg.
3. Continuous layer (ballistics ONLY - velocity, spin detection, ramp targeting):
   continuousAngleDeg unwrapped from the static angle: each sample,
   d = shortestPath(static_now - static_prev), continuousAngleDeg += d.
   All code now using angleDegMT/encoderCountsMT for motion switches to this. Wedge
   identity must never be derived from the continuous layer.
4. Re-prime / blind-gap handling: after a gap, resume unwrapping from current static
   angle. Turn-count may err across a long gap (acceptable); wedge identity cannot.
   Keep FRZ-EVT capturing gap/resync events; point its frame-jump detector at the
   continuous layer.
5. z command: new semantics - owner parks label-0 leading edge under the pointer, z
   stores current raw as rawZero (uint16, NVS) and prints old/new. Remove
   wedge0OffsetDeg (double) and its load/save; delete the legacy "wedge0" key handling
   (leave the stale key in NVS untouched or delete it, either is fine).
6. Remove the now-dead +raw seed / ENCODER_DIR_SIGN accumulation for wedge identity
   (the -1 direction is embodied in (rawZero - raw)). encoderCountsMT may remain only
   if something still genuinely needs counts; prefer deleting it.
7. Status output: boot banner and periodic status must print rawZero, wheelAngleDeg
   (2 decimals), currentWedge, i2c error count.

## Verification ladder (all logged to AGENT_NOTES.md)
V1. Compiles clean; flash succeeds (stop serial bridge first; do not reopen COM3 for
    5 s after esptool).
V2. Static-frame sanity at rest: status shows a stable angle; RTS-reset 5x with wheel
    untouched -> identical angle/wedge every boot (this is the exact scenario the old
    firmware failed at 143.17 deg).
V3. Bounded-move ladder (bench clear, low current): use existing k/K bounded moves to
    step exactly +30.0 deg x 12 (one full turn), then -30.0 deg x 12. After each move,
    wedge must increment/decrement by exactly 1 and angle delta = 30 +/- 1.5 deg.
V4. Reset-under-displacement: park at 3 different wedges via bounded moves; RTS-reset
    at each; wedge identity unchanged across every reset.
V5. Continuous-layer check: during a few bounded moves, velocity sign/magnitude sane;
    no FRZ-EVT frame jumps in the wedge frame during the entire ladder.
Acceptance = V1-V5 all pass. Hand-spin campaign (T3-style) remains owner-attended and
is OUT of scope for this task; stop after V5 and summarize.

## Do NOT
- Do not touch chooseRandomSafeTargetAngle / landing distribution (separate known task).
- Do not change dare_mask, motor current profiles, fight-abort logic, or thresholds.
- Do not run free spins (g/G) or takeover rehearsals; bounded k/K moves only.
- Do not erase NVS wholesale.

## Workflow
Branch claude/adaptive-v2. Commit early and often with clear messages; push at the end.
Log a dated section in AGENT_NOTES.md with results of V1-V5 and final board state.
Build: "C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe"
compile --upload -p COM3 --fqbn esp32:esp32:esp32 --libraries
"C:\Users\4urka\OneDrive\Documents\Arduino\libraries" <sketch dir>

## ADDENDUM 2026-07-29 22:4x (assistant)
Implementation already EXISTS: commit 5062075 on this branch, reviewed line-by-line by
the assistant and found contract-correct (static frame, same-frame MT seed, label-true
re-prime snap, z->rawZero, legacy wedge0 retired, cal keys untouched). It has NEVER been
compiled, flashed, or tested. Your job is therefore: (1) grep for any remaining
wedge0OffsetDeg/angleDegMT references that break compile or violate the wedge-identity
rule, (2) execute verification ladder V1-V5 against 5062075, fix only what verification
demands, (3) report. Integration into the party build (main mid-spin line vs this line)
is an OWNER DECISION - do not merge branches yourself.
