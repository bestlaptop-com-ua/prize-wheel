# party_20260923 - flash 2 for the 2026-09-23 party: hand-stop alarm, gentle carry, re-push release

Base: work/failop_20260923 (stage 1a, flashed 2026-09-23 11:28; 11 clean spins before this was built).
Owner decisions (2026-09-23 10:32): party tonight; "If stops by hand and we are sure it is, then play
alarm sound and bright red strobe"; weak spins: "Gently carry"; hall sensor: not tonight.

## What changed

Hand-stop alarm
- Detector samples |omega| every 25 ms (3-sample medians) while a RELEASED spin coasts with the motor
  OFF (SPIN_RELEASED / TARGET_RESERVED / CAPTURE_ARMING, EN high). Suspect: speed fell from >= 0.12 rev/s
  to <= 40 % of the recent peak at >= 0.8 rev/s2 averaged over >= 100 ms within 400 ms. Motor + friction
  can decelerate the wheel by at most ~0.4 rev/s2, so only a hand does that. Log: `SPIN#n HAND? ...`.
- While a suspect is fresh (1.5 s) no capture starts (the motor is not engaged onto a hand).
- Confirmed when the wheel comes to rest within 1.5 s of the suspect without a re-push:
  `SPIN#n HAND-STOP (...)`, result GUEST_STOPPED, NO dare recovery (the wheel stays where the hand left
  it), 5 s synthesized two-tone alarm (988/1480 Hz) + bright red strobe at 3 flashes/s (kept at the
  3 Hz photosensitivity guideline). Replaces the guest jingle and any celebration for that spin.
- Not alarmed: hand stops while the motor is braking. Review of this build replayed the detector over the
  recorded 1 kHz traces: with the motor energized, pole-slip chatter of the rotor on the coupling spider
  raised suspects in 3 of 13 powered spins with nobody touching the wheel. A hand during braking is
  handled as in stage 1a (field frozen, wheel held, verdict, recovery).

Gentle carry (chooser pass 5, quality 4) - tried before the hard-braking passes 3/4
- When passes 1-2 find nothing and the natural stop is inside a dare or within 5 deg of one, the target
  is the first point beyond the natural stop that is >= 10 deg from any dare edge (the next safe wedge).
  The field starts at the wheel's speed and decelerates to that point at about the natural rate; the
  motor adds a little energy against friction. The coupling reduction is skipped for carries (the field
  must keep the wheel rolling). RESERVE lines show q=4. Below ~0.025 rev/s the pickup cannot be proven in
  time and the stage 1a free-coast recovery still takes over.
- Why before passes 3/4: on 9/23 (flash 1 test) both pass-3 stops (q=1, planned at 426 and 499 sps2,
  2-3x the natural deceleration) slipped the rotor by ~120 deg and ended in SETTLE-DRAG. A carry to the
  next safe wedge brakes at about the natural rate instead.

Re-push release (stage 1a review SHOULD-FIX 2)
- A wheel faster than the field by > 0.15 rev/s for 150 ms (dips < 60 ms tolerated; not in the 250 ms
  pickup window, not when frozen) is released as a new spin (`RE-PUSH mid-control`), which is captured
  again with a fresh plan, instead of grinding against the committed ramp.

Hold re-judge (stage 1a review MINOR 3)
- A driver re-apply while holding (24 V back after a dip) re-enters LANDING_SETTLE for a fresh verdict;
  the phase snap is never read as a guest push.

## Build
Local pre-check: exit 0, 1697739 bytes, RAM 120016, no sketch warnings. Flash build: MILL-PC.
Rollback: work/failop_20260923/upload_candidate.py (stage 1a) or work/forcestop_guard_20260922.

## Test (Timur at the wheel)
1. 10 normal spins: unchanged behaviour, no HAND? lines.
2. Spin hard and stop it firmly by hand while it is still fast (before it slows to ~0.4 rev/s): alarm +
   red strobe, wheel stays, even on a dare.
3. Slow it gently by hand and let go: no alarm.
4. Weak push that would die on a dare: q=4 RESERVE, the wheel rolls on into the next safe wedge.
5. Medium re-push during braking: `RE-PUSH mid-control`, new SPIN#, captured again, no grinding.
