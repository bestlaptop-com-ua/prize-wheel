# party6_20260925 - flash 6: a free coast that stops is held (800 mA)

Base: work/party5_20260925 (flash 5, on the wheel since 2026-09-25 15:02).
Source = flash-5 source + `apply_flash6.py` (exact-once anchors); `stage6.py` stages it on MILL-PC.
Local build: exit 0, no sketch warnings, 1,701,187 bytes (54%).  Normalized .ino sha256
f569ccfb3679a4fbb1ff42b7267027a81075f266b30811f895efa48477a85cf2 (same on MILL-PC after stage6.py).

## Why (Timur, 2026-09-25 15:16)
"Even if motor coasts freely we should hold it after it stops."
Log: spin 16 (let-coast, late grab abandoned) stopped at 350 deg with the motor off; the unbalanced wheel
then rolled back (promoted to spin 17 at -0.166 rev/s) onto dare wedge 15 and needed a P7 recovery.
Every controlled landing already ends in SOFT_HOLD (800 mA until the next push); free coasts did not.

## What changed
1. Free-coast rest in SPIN_RELEASED (let-coast spins, abandoned captures, contact): with control available
   the stop is declared after 150 ms at <= 0.02 rev/s (was 0.5 s / 2.5 s), i.e. at the turning point
   before a rollback can start.  Hand check, P7 dare recovery and the result are unchanged; then
   `engageCoastHold()` switches on a static field (CS_PRECHARGE -> CS_HOLD1, no pulses) and enters
   SOFT_HOLD.  Without control (locked/fault) it freewheels to IDLE as before.
2. The field comes on at the driver's old electrical phase, so the rotor snaps up to 3.6 deg and rings
   ~1-2 s.  While `coastHoldPending`: no deflection anchor (no false "pushed" release); release on the
   usual >= 0.12 rev/s or on > 25 deg from the engage point (a hand dragging it; a gravity ratchet of
   1-3 poles is caught and re-checked instead of released).  Once the wheel
   stays inside a 0.3 deg window for 0.5 s the anchor is set at the window middle and the dare zone is
   re-checked (P7 if the snap carried it within 2 deg of a dare) - not after a hand stop (owner
   decision 2: the wheel stays where the hand left it).
3. Review fixes (general-purpose agent, 15:2x): P7 skipped after a hand stop; slip release; min/max
   window + midpoint anchor; SOFT_HOLD leaves at once after a driver re-apply redirect or fault;
   landingVerdict and the re-apply path clear `coastHoldPending`.
4. Banner party6-20260925.  Log lines: `# COAST-HOLD: ... hold field on`, `# COAST-HOLD anchored ...`,
   `# COAST-HOLD settled ... (dare zone): P7`, `# COAST-HOLD released: moved ...`.

Not changed: let-coast rule, capture, carry, braking, hold current, hand-stop alarm, recovery.
Known, not addressed: the ~9 deg RAMP_STOP overshoot and pull-back after controlled landings; the
imbalance itself (balance the wheel, or measure it and enable the gravity model).

## Test
1. `?` shows party6; `f` friction unchanged; `s` no fault.
2. Gentle pushes (peak < 0.3 rev/s): stop -> `COAST-HOLD ... hold field on` -> `anchored` within ~2 s;
   no rollback; a small tick at engage is expected.
3. Push a held wheel: `HOLD released` / `COAST-HOLD released`, spins normally.
Rollback: work/party5_20260925/upload_candidate.py (flash 5).

## Result (on the wheel since 2026-09-25 15:47)
Arm approved 15:41 (Telegram).  MILL-PC compile exit 0, no sketch warnings, 1,701,315 bytes; upload exit 0
(all regions hash-verified).  `?` party6 banner, `f` friction unchanged (cw 0.4499/0.1169 fits 22, ccw
0.3225/0.2020 fits 40), `s` IDLE_STOPPED fault=NONE tmc=1 takeover=1 dirCal=1.
Spins 18-28 (15:09-15:47, flash 5) for reference: all safe; spin 18 missed its target by 65 deg (slips) and
was recovered off dare 12 by P7.
