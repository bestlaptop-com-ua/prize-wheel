# party3_20260923 - flash 3 for the 2026-09-23 party: more motor current, hold through settle drags, slip log

Base: work/party_20260923 (flash 2, flashed 2026-09-23 12:38; tested by Timur 12:50-12:53: hand-stop
alarm, gentle carry and re-push release all worked).

## Why
- Timur (12:35): "Cw turns are smooth, ccw are not so. Nobody stopped the wheel."
- His CCW is the firmware's dir=+1.  The rough moments are pole slips of 1-3 electrical cycles
  (7.2 deg each; SUMMARY syncErr 13-22) that land the wheel past its target: 8-18 % of the 9/23 spins,
  about twice as often CCW (4 of the last 19 CCW spins, 1-2 of the last 20 CW).
- The CCW free-coast friction fit doubled during the session (c ~0.3 -> ~0.6 rad/s2) while CW stayed
  ~0.35: something rubs in the CCW direction (flapper, rim, coupling or bearing collar) - asked Timur
  to check.  Firmware cannot fix a rub; it can hold the wheel harder.
- Flash-2 spin 1 (1 kHz trace, final approach, CCW): the rotor ran 1.5-2.3 deg ahead of the field -
  about the torque peak of the motor (1.8 deg) - while the field decelerated at ~0.08 rev/s2.
- 9/23 flash-1 spin 70: after a clean braking run (syncErr 3.5) the wheel slid 55 deg during
  LANDING_SETTLE with nobody touching it, and SETTLE-DRAG released it (freewheel, GUEST_STOPPED).
  That is a release of a moving wheel, against the owner requirement (never let the wheel go).

## What changed
1. CUR_CAPTURE_MA / CUR_BRAKE_MA 2200 -> 2700 mA RMS: ~23 % more pull-out torque for the whole
   capture, braking ramp and settle.  Motor rated 3 A; TMC5160 at R_SENSE 0.075 reaches 3.06 A RMS.
   Braking lasts ~5 s per spin; hold stays at 800 mA.  Recovery nudges stay at 2200 mA.
2. SETTLE-DRAG no longer releases: the field stays where it is (a slipping wheel ratchets to a
   stop, a hand always wins at ~4 N on the rim), one `SETTLE-DRAG ... motor keeps holding` line and
   a MOTOR_FIGHT anomaly are logged, and the rest position gets the normal verdict (hold, or dare
   recovery).  No guest jingle for such a spin (it closes with a normal result).
   Re-spins during settle are still handed to spin detection (then captured and steered): above
   0.8 rev/s at once (unchanged), and - new, review SHOULD-FIX 1 - above 0.3 rev/s held for 150 ms
   without a dip (`RE-PUSH in settle`).  No dip tolerance: a ratcheting rotor reads as spiky speed.
   A wheel still moving > 0.05 rev/s at the 20 s settle timeout (a slow drag) is held and judged
   once slower, instead of a verdict whose recovery refuses to start (review MINOR 3).
3. Slip telemetry: `SPIN#n SLIP k: wheel ahead of|behind field by X deg, T ms after torque-on,
   wheel/field speed, angle, remain, pickup|decel|ramp|settle` once per further 7.2 deg of
   encoder-vs-STEP drift, first at 8 deg (no-slip baseline 2-7 deg).  Tracked through settle too;
   invalidated when the spin closes or a dare recovery re-zeroes the STEP count.
4. Diagnostic dump current column is 16 bit (2700 mA no longer clips at 2550).
5. Build banner names this build.

Not changed: planner, ramp, carry, hand-stop alarm, re-push release.

## Review (general-purpose agent, 2026-09-23 13:22): flash, no blockers
- SHOULD-FIX 1 (gentle re-spin in settle no longer handed back) fixed as above.
- SHOULD-FIX 2 (no map check): +23 % torque and holding against drags load the coupling/adapter,
  which slipped on 9/21 and 9/22.  Check the map before and after the test (pointer on the 18|1
  line reads ~0 deg with `s`, or any landing's printed wedge matches the pointer); paint-mark the
  coupling.
- MINOR 3 (20 s timeout verdict while moving) fixed as above.  MINOR 5 (settle after a recovery
  runs at the recovery's 2.2 A) left as is.
- TMCStepper math checked: 2700 mA -> CS 31, GLOBALSCALER 225, 2.693 A RMS (2200: 2.190 A).

## Test (Timur at the wheel)
0. Map check (pointer vs printed wedge) before and after.
1. 10+ normal spins, both directions: no new faults; fewer pickup/decel/ramp SLIP lines than the
   flash-2 syncErr > 8 rate (settle is now included in syncErr, so compare SLIP phases).
2. Listen at capture: 2.7 A may make the pickup slightly firmer.
3. Push the landed wheel slowly by hand within half a second after it stops: `SETTLE-DRAG ...
   motor keeps holding`, the wheel resists, then gets a normal verdict.  A brisk push then:
   `RE-PUSH in settle`, a new spin that is captured again.

Rollback: work/party_20260923/upload_candidate.py (flash 2).
