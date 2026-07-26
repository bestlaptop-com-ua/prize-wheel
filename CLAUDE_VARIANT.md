# Claude variant: v2 "adaptive" firmware (branch claude/adaptive-v2)

Builds on the P1 sensing firmware. Targets the four open issues in README.md.
Compiles clean: 420002 bytes flash (32%), 113444 RAM (34%), esp32 core 3.3.10.

## What changed and why

1. Friction auto-calibration (issue: seeded, never measured).
   Every freewheeling coast samples (omega, decel) pairs ~8x/s and
   least-squares fits decel = c + b*omega per direction at landing, blending
   25% per spin into the model. Persisted to NVS (cw_c/cw_b/ccw_c/ccw_b),
   loaded at boot. Hand contact is rejected (decel > 0.6 rev/s^2 or speedups).

2. Measured prediction margin (replaces the guessed 15 deg constant).
   A reference prediction is snapshotted mid-coast (~0.45 rev/s); every
   UNSTEERED landing folds the real miss into a per-direction rolling MAE
   (NVS: mae_cw/mae_ccw). The dare margin is now clamp(1.6*maxMAE, 6, 22) deg,
   so intervention frequency follows this wheel's actual predictability.

3. Target-first steering planner (issue: wedges 11/0/2/3/4 never landed).
   Old flow picked targets only 150-210 deg ahead at a fixed decision speed -
   a structural bias. Now, when a dare stop is predicted (speed <= 0.6 rev/s),
   a target wedge is drawn UNIFORMLY from all ten safe wedges, and the
   takeover fires at the revolution instant when the decel needed to stop on
   it is 1.08-1.70x the wheel's own measured friction decel (and inside the
   accel ceiling). Reachability comes from timing, not geometry, so the
   landing distribution is planner-uniform. The old runway-filtered picker
   remains as last-resort fallback below 0.14 rev/s (planner window covers
   roughly 85% of dare spins; the fallback keeps the guarantee absolute).

4. Disguised takeover (issue: catch was perceptible).
   All takeovers are now shaped as ONE continuous deceleration ramp: 97%
   speed match into an accel of v^2/(2*runway) - i.e. barely above natural
   friction by construction for planner catches - instead of
   cruise-then-300mA-brake. Encoder brake gate stays armed (2 deg buffer).
   TAKEOVER_MAX_REV_S raised 0.240 -> 0.320 so the planner can catch at up
   to 0.31 rev/s (38 motor RPM).

5. Accel ceiling probe (issue: 650 sps2 never characterized).
   Attended 'a' command: staircase 800/1000/1200/1500/1800 sps2, one 120 deg
   move per stage at 1600 Hz / 350 mA; >4 deg encoder disagreement = step
   loss. Ceiling := max(650, 75% of last clean stage), persisted (NVS
   "accel"), used by runway math and all launches. Wheel sweeps through
   dares - bench only, like 'p'.

6. Coil release after held landings.
   SAFE_HOLD floats the coils 2.5 s after stillness -> DONE, so a guest
   idly rocking the wheel between spins feels a free wheel, not a detent.

## New serial commands
- a : attended accel-ceiling probe (motor moves the wheel!)
- c : print calibration (friction c/b per dir, MAE, margin, accel ceiling)
- C : reset calibration to seed values and persist

## Bring-up (attended, wheel clear)
1. Flash the branch build. 'z' zero at wedge 0/11 boundary as usual.
2. 'p' direction probe (unchanged, required before any takeover).
3. 'a' accel probe; expect ceiling to land 750-1350 on this rig.
4. ~10 vigorous spins per direction WITHOUT dare involvement: watch
   SPIN#n FRICTION and PRED-ERR lines; 'c' should show c/b drifting from
   0.30/0.15 toward measured values and margin tightening below 15.
5. Aimed dare spins with 'v' verbose: expect PLAN (uniform target), then
   TAKEOVER with the small profiled accel, LANDED on target. Over ~30 dare
   spins all ten safe wedges should appear.
6. Guest-abort, opposite-motion, weak-spin behaviors are unchanged - retest
   the README acceptance list before any party use.

## Tunables (top of file, v2 block)
PLAN_ARM/FIRE_MAX/FALLBACK_REV_S, DISGUISE_DECEL_MIN/MAX_RATIO,
PLANNED_MATCH_FRACTION, FRICTION_BLEND, SAFE_HOLD_RELEASE_MS, probe stages.

## Honest caveats
- Behavior changes are bench-validated by construction, not by spin data yet.
  The 1.08-1.70 disguise band and 0.32 speed cap need ear/eye verification.
- The planner needs the friction fit to be sane before its timing is right;
  do step 4 before judging step 5.
- Recovery path ('decisive notch') is untouched.

## Update: no visible stop-then-restart (creep-carry)

The settle-on-dare recovery was the one remaining tell: a passive wheel that
stops and then moves again is physically impossible. Two changes:

1. Creep-carry (new, primary path). The old dead band was 0.02-0.07 rev/s:
   too slow for a takeover, so the wheel was allowed to stop on the dare and
   recovery restarted it. Now tryCreepCarry() catches a dare-bound wheel in
   that band WHILE STILL MOVING: 80 ms low-current precharge, then the pulse
   train picks up at the wheel's live speed (jump-start matched, 450 mA,
   gentle 250 sps2 profile, <= ~20 deg/s) and rolls it to the first angle
   clear of the dare + prediction margin. Motion never stops, so there is
   nothing to notice - it reads as the wheel carrying slightly farther than
   expected. Log: SPIN#n CREEP-CARRY ... then RECOVERY ... carry=1.

2. Rest recovery reshaped as a flapper slip (residual path: prediction miss,
   guest-placed stop, encoder-degraded cases). Dare confirmation now takes
   180 ms of stillness (safe landings still wait the full 500 ms), so motion
   resumes ~300 ms after the stop - inside the "did it stop?" ambiguity -
   with a crisper 1200 sps2 start, and the target is now the NEAREST clear
   angle (one short notch) instead of a randomly chosen distant wedge.

Bench checks to add: creep a spin into wedge 1 and 5 in both directions and
watch for CREEP-CARRY (wheel should never stop); force a true stop on a dare
(hold and release the rim) and time stillness-to-motion (~0.3 s, one short
slip). Tunables: CREEP_CARRY_SPEED_HZ/ACCEL_SPS2, DARE_CONFIRM_MS,
CREEP_CLEAR_EXTRA_DEG.
## Bench session 26 Jul: findings and fixes

44 landings recorded, ZERO on dares - the guarantee held even through faults.
Three root causes of the "very unnatural" takeover were identified and fixed:

1. Pole slip at 300 mA. On the (deliberately) unbalanced wheel, gravity
   torque on hill segments exceeds the stepper's pullout torque at the old
   300 mA brake current. Long profiled ramps desynced into a grinding drag
   (fight aborts on SPIN#22/26/27/28/31, overshoots on #29/#30). Profiled
   takeovers now run at DISGUISE_TAKEOVER_CURRENT_MA = 600.
2. Phase-capture back-snap killed carries. Energizing 450 mA on a moving
   rotor snaps it up to ~1.8 deg (wheel) backward to phase alignment - the
   exact -1.6 deg signature of every aborted carry. Reversal guard is now
   3.0 deg for carries (1.5 deg unchanged for rest recoveries).
3. Friction fitter bias. Rejecting negative-decel samples as "hand contact"
   discarded the downhill half of the gravity oscillation and inflated cw to
   c=0.464. The sampler now accepts +-1.2 rev/s^2 symmetrically so the
   least-squares mean recovers pure friction. Calibration was reset after
   flashing this build; re-run calibration spins.

## Spin direction handling (documented on request)

- Direction latches at spin detect (|omega| >= 0.12 rev/s) from
  IDLE/DONE/SAFE_HOLD, using the sign of omega at that moment.
- A pre-rotation opposite to the intended spin (winding the wheel backward
  >= 0.12 rev/s before flicking it the other way) latches that direction
  first; the real spin re-latches via guest override once it exceeds
  0.80 rev/s for 60 ms. Verified on the bench: every motor move (takeover,
  carry, recovery) is issued strictly in the latched direction, and the
  encoder->FAS direction mapping is correct for BOTH cw and ccw.
- Known edge: a gentle real spin (< 0.80 rev/s) immediately after an
  opposite wind-up keeps the stale latch until the wheel settles. The fight
  watchdog aborts any takeover launched with a wrong latch, and the settle
  path still guarantees no dare outcome.

## Known cosmetic issue
targetErrorDeg in LANDED lines is misleading for gated profiled takeovers
(reports commanded-vs-travelled, not target-vs-landed); actual landings were
on target. Fix later; logs' wedge numbers are authoritative.
## v4: every-spin high-speed engagement (user-directed redesign)

Prediction is out of the outcome path. Every confirmed spin is engaged as its
speed decays through 0.50 rev/s (0.14 floor; weaker spins fall to the old
nets), the target is a uniformly random safe wedge, and the whole slowdown is
ONE continuous decel ramp stretched by up to 5 extra whole revolutions to a
randomized 8-11 s roll-out (clamped so commanded accel never drops below the
60 sps2 disguise floor - the motor only ever brakes). Profiled moves now run
at 600 mA from the FIRST step (the 100 mA pickup stage was the residual
desync). TAKEOVER_MAX_REV_S raised to 0.55.

New DRIFT_WATCH mode closes both unguarded holes: every coil release after a
held landing, and every recovery abort, now land in an armed state where a
fresh spin restarts normally, a forward drift toward a dare is nudged inside
the roll (carry, max 4 attempts/spin), and a rest on a dare gets the fast
slip. Aborted takeovers clear the spin decision so v4 re-engages at a lower
speed instead of abandoning the spin.

v2 planner/guards remain in the binary as dormant safety nets below the v4
floor. Log line: SPIN#n V4-ENGAGE omega/target/fwdDeg/extraRevs/durS.

Acceptance for guest use: over >=30 spins - zero fight aborts, zero rattles,
zero dare rests (including after release), roughly uniform landings over the
ten safe wedges, and the owner's eye/ear sign-off on the roll-out.