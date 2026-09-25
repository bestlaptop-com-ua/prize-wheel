# party5_20260925 - flash 5: weak pushes coast free when their stop is safe; 3.0 A

Base: work/party4_20260923 (flash 4, on the wheel since 2026-09-23 21:41).
Source = flash-4 source + `apply_flash5.py` (exact-once anchors); `stage5.py` stages it on MILL-PC.
Local build: exit 0, no sketch warnings.

## Why (Timur, 2026-09-25)
- 13:32 "It always tries to move to the center of the wedge ... it does not look natural."
- 13:35 "At slow spins it moves extremely erratically. Can it move smoothlier maybe with slower speed and more power"
- 13:55 "Build. We have two hours left"
- Log, spins 10-12 (13:34, peaks 0.17-0.22 rev/s): the motor grabbed each weak push at release
  (0.15-0.20 rev/s) and forced the friction-only slowdown plan (natural - 6 deg) on a wheel that gravity
  was still moving.  Spin 10: wheel 0.20 -> 0.26 rev/s on its own while the field slowed to 0.16
  (SLIP 1 ahead, syncErr 13.4, landed 12 deg past target); spin 12 likewise (syncErr 9.6).
- Friction model is inflated by the unmodelled imbalance (cw c 0.45, ccw c 0.32 vs 0.17 gravity-separated on 9/21).

## What changed
1. CUR_CAPTURE_MA / CUR_BRAKE_MA 2700 -> 3000 mA (motor rating 3 A; TMCStepper: CS 31, GLOBALSCALER 250,
   2.99 A RMS).  Hold 800 mA, recovery 2200 mA unchanged.
2. Let-coast: in tryReserveTarget, a spin whose peak was <= 0.30 rev/s is not captured while every point from
   6 deg before to 12 deg past the friction-model natural stop is >= 4 deg from any dare (re-checked every
   tick; `LET-COAST` log line).  When the band touches a dare the normal reservation (shadow / carry) runs.
   A let-coast wheel that stops closes through the existing free-coast rest path (hand check, P7 dare
   recovery, RES_NO_REACHABLE_SAFE = celebrated on a safe wedge).
3. Review fixes (three general-purpose agent passes 14:0x-14:2x):
   - slow reverse rolls (< 0.35 rev/s) of a let-coast wheel stay in SPIN_RELEASED (the old reclassification
     via MOTION_CANDIDATE could end in MANUAL/IDLE without P7); a reversed roll is not a re-push;
   - a let-coast stop must be still 2.5 s (not 0.5 s) before it counts, so a rollback from a swing's turning
     point starts inside the spin;
   - for 20 s after a let-coast landing, an unconfirmed movement that settles on/near a dare gets P7
     (`# LET-COAST rollback ... P7`).
4. SUMMARY prints letCoast (1 = left to coast, 2 = handed back to capture); banner party5-20260925.

Not changed: catch-speed rotation (flash 4), targeting, carry, braking, hand-stop alarm, recovery.
Deferred: gentler torque-on at pickup, braking short of a dare instead of carrying past it (needs the ~9 deg
RAMP_STOP overshoot fixed first: overshoot moves toward the dare), gravity measurement + model.

## Test
1. `?` shows party5; `f` friction unchanged; `s` no fault.
2. 6+ gentle pushes (peak < 0.3 rev/s) in different spots: most print LET-COAST and stop by themselves,
   no motor; any near a dare hand over to capture or end with a P7 recovery (never rest on a dare).
3. A few normal spins: behave as flash 4 (varied landings), no new faults, SLIP lines no worse.
Rollback: work/party4_20260923/upload_candidate.py (flash 4).

## Result (on the wheel since 2026-09-25 15:02)
Upload exit 0 (all regions hash-verified); `?` party5 banner, `f` friction unchanged, `s` no fault.
Test spins 2-16 (15:03-15:08): all ended on safe wedges, no dare rests, no faults except spin 3
(MOTOR_FIGHT: wheel slowed 0.35 -> 0.20 rev/s within 0.2 s at 270-285 deg, motor slipped ~45 deg,
landed on 15 instead of 6).  Five let-coast spins (8, 11, 13, 15, 16): all handed back near the end
(0.02-0.14 rev/s) because the gravity-blind model under-predicts the coast 3-4x (spin 8: predicted 64 deg,
rolled ~290 deg); spins 8 and 15 were moved 11-20 deg off a dare edge after nearly stopping; spin 16's
late grab was abandoned (pulse-first rejected at ~0 speed) and it rolled on to 18.
Timur 15:08-15:09: "Two spins moved backwards after stop. A couple was not smooth" / "I guess rapid
slowdowns is where the wheel is not balanced" - agreed: the remaining roughness is the imbalance; next step
is balancing or the gravity model, not more current.
