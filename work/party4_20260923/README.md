# party4_20260923 - flash 4: the catch speed varies per spin so landings differ

Base: work/party3_20260923 (flash 3, on the wheel since 14:46).  Built on MILL-PC 19:05-19:08
(compile exit 0, 1 699 135 bytes, no sketch warnings; app bin sha256 3af966ac...).
Source = flash-3 source + `apply_engage_vary.py` (6 exact-once edits; `stage4.py` stages it on MILL-PC and
checks the LF-normalized sha256: flash 3 9f25941b..., flash 4 76fff3b7...).  NOT flashed until Timur says go.

## Why
- Timur 18:36: "Now it stops on 11 constantly" / "I don't insist on randomness. But they should differ".
- 16 spins 18:30-18:36 (lighter counterweight on): label 11 x9, label 1 x4, label 12 x2, label 10 x1.  All were
  gentle CW pushes from 10/11 (or from near 18 pushed up to the top): the push only tips the wheel over the top,
  gravity supplies the speed (peak ~0.5-0.8 rev/s at ~348 deg nearly every time), so every spin reaches the fixed
  0.40 rev/s catch speed at nearly the same angle and the planner (pass-2 shadow = natural - 6 deg, or the narrow
  pass-1 window) sends it the same distance.  The one hard spin (2.2 rev/s) went to label 1.
- The friction model did not change (read with `f` at 18:30: cw c=0.5589 b=0.0347 fits=21 | ccw c=0.3738
  b=0.1242 fits=38, identical to 17:24, across a reboot); every coast fit since 16:43 is still rejected.

## What changed
1. `ENGAGE_SET_REV_S = {0.40, 0.35, 0.30, 0.25}`; `startSpinEvent` picks a new index every spin that is never the
   previous one (random among the other three).  `tryReserveTarget` reserves only at/below this spin's value
   (was the fixed `CAPTURE_MAX_WHEEL_REV_S` 0.40).  Launch tolerances, targeting passes, dare checks, braking,
   carry, recovery: unchanged.
2. START line prints `engage=%.2f`.
3. Review SHOULD-FIX 1: `captureFrictionSample` ignores samples below 0.40, so the friction fit sees exactly the
   inputs it saw in flash 3 (the free coast now continues below 0.40 on 3 of 4 spins, and that slow part is
   gravity-dominated).
4. Banner: party4-20260923.

## Review (general-purpose agent, 19:03): FLASH, no blockers
- SHOULD-FIX 1 (fit inputs) fixed as above.
- SHOULD-FIX 2 (test, not code): the targets are the model's natural stop, so a later catch moves the landing
  only by how much the real wheel departs from the friction-only model between 0.40 and the catch speed.  On
  this unbalanced wheel that departure is large on the climbs, so it should spread landings, but it is not
  guaranteed: check `engage=` against `targetW` on the first spins.
- MINOR: re-spins/redirects also consume a value; first spin after boot is never 0.40; 3 of 4 spins coast
  0.5-2 s longer with the motor off (more time for hand-stop / reversal / false re-push paths, all existing).
- Checked OK: index math and `random(3)` range, no stall before capture (reserve-to-torque-on 22-26 ms;
  >= 0.74 s from 0.25 rev/s to a stop even at the model's max gravity), all planner thresholds still pass at
  0.25, dare invariants unchanged, START printf args.

## Test (Timur at the wheel)
1. `?` shows the party4 banner.
2. 6+ spins, gentle and normal, same spot and direction as the 11-loop: `engage=` rotates, RESERVE speed <=
   engage, and the landing wedge changes.  No new faults; SLIP lines no worse than flash 3.
3. If landings still repeat or anything looks worse: roll back.

Rollback: work/party3_20260923/upload_candidate.py (flash 3).
