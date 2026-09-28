# party8_20260928 - flash 8: exact final ramp + shadow landings off the flapper pegs

Base: work/party7_20260928 (flash 7). Source = flash-7 source + `apply_flash8.py` (exact-once anchors);
`stage8.py` stages it on MILL-PC. Normalized .ino sha256 66166950258b98616f90ffa409c7de6f0d06373a259492f3effd38df6dbc829e
(local dry run = MILL-PC).

## Why (Timur, 2026-09-28)
17:41 "I added counterweight on 0. And installed flapper ... Sometimes its final steer is not natural."
18:11 "Crept to one [a peg], flapper is straight above the center of the wheel so it touches about 0.2 degrees
and releases 1 degree after."  18:23: the flapper went in just before the 6 free spins (~17:50).

## Findings
- Free coasts (takeover off, spins 3-10, 1 kHz DIAG tails): rest angles scatter all round the wheel -> the
  counterweight leaves no dominant heavy spot. Tail decel 0.04-0.08 rev/s^2; 4-10 deg roll-back after the last
  peg on spins 6/7. Spin 10 (takeover re-enabled mid-coast) landed in dare 3: P7 attempt 1 toward the 3|4 peg
  settled back in the dare (58.3 -> 51.6), attempt 2 the other way succeeded (31.0).
- 103 steered spins 9/22-9/28 (almost all before the flapper): err = +3.6 mean (up to +12) and
  err - (RAMP_STOP lead - remain) = -0.5 +/- 2, i.e. the overshoot is the stale capture-time decel at RAMP_STOP.
  24 of 103 landings ended within 2 deg of a wedge line; shadow landings (q3, 53 of 103) ignore lines entirely.

## What changed
1. RAMP_STOP re-sizes the final ramp to (remaining - 1.5 deg): only ever steeper, capped at the spin's decel cap,
   and for pass-1 (q0) at 1.25x the model's natural decel. Log: `exact=0/1 need=<sps2>`.
2. Shadow pass: runway shortened up to 6.5 deg (not below MIN_RESERVE_RUNWAY+1) until the landing is >= 3 deg
   from every line, in a safe wedge, >= 5 deg from dares; otherwise the legacy natural-6 runway.
3. Banner party8-20260928.
Review (independent agent): no blocking issues; applied its fix (loop floor) + q0 cap + logging.
Not changed: margins elsewhere, carry, P7 recovery (attempt alternation already recovered spin 10), currents.

## Result
Arm approved ~18:23 (Telegram, reason stated the upload). MILL-PC compile exit 0, no sketch warnings,
1,702,003 bytes (54%); upload exit 0, hash verified. `?` party8 banner; `s` IDLE_STOPPED fault=NONE tmc=1
takeover=1 rawZero=1925; `f` friction kept (cw 0.1768/0.0907 fits 27, ccw 0.3225/0.2020 fits 40).
Rollback: work/party7_20260928/upload_candidate.py (flash 7).
