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
## 2026-07-27 autonomous session (Claude Code): T1-T2

### T1 - bench spin generator g/G (commit 16e1e14)
Added attended serial commands g (FAS+) and G (FAS-): ramp from rest at
accelCeiling/2 (450 sps2 at the 900 ceiling), 600 mA, to a random 0.35-0.45
rev/s target, then release the coils the instant encoder omega reaches target,
handing a natural decaying spin to the v4 pipeline (mode=DONE -> FREE_SPIN).
Verified both directions: g -> omega=-0.356 (ccw), G -> omega=+0.418 (cw); both
landed safe (wedge 3), pipeline picked them up as SPIN#n START/V4-ENGAGE. Release
is gated on measured omega, not the commanded profile, so the wheel provably
reached speed; target < GUEST_OVERRIDE (0.80) avoids re-latch.

### Anomaly seen immediately (to characterize in T3, fix in T4)
Every V4-ENGAGE -> TAKEOVER trips `# TK-END reason=wheel-speed-up` ~80 ms after
TK-START (travelled ~1.8 deg). Root cause (evidence + code read): takeoverSensedSpeedup()
aborts when forward speed rises >0.015 rev/s above the lowest-seen for >=30 ms.
On this deliberately unbalanced wheel at ~0.30 rev/s, gravity accelerates the
wheel on downhill arcs by more than that - a FALSE positive, not a guest push.
IMPORTANT: finishTakeover() does NOT stop the stepper - it only drops current
600->500 mA and switches to RECOVERY_HOLD, so the profiled move CONTINUES to the
target. That is why both test spins still landed on the planner target (wedge 3).
So "wheel-speed-up" is a near-cosmetic mislabel today, but the 600->500 mA drop
mid-profile is a slip risk and the logic is confused. Candidate T4 fix: in the
profiled-decel regime a gentle brake cannot "be helped" by the wheel, so raise
the speedup threshold / lengthen the window / disable the speedup abort while
takeoverDecelProfile is set (keep the fight watchdog + guestOverride).

### T2 - camera calibration (commit c442323, net-rotation method)
The 3072-sample diag ring (~3 s) only ever holds the tail of a real spin, and the
v4 takeover stretches every spin to ~9 s, so a g-spin capture retained only the
final ~13 deg creep - useless for a scale fit. Added k/K bounded there-and-stop
cal moves (full-ceiling accel, 0.25 rev/s cap, 0.30 rev, self-stops in ~2.9 s).
Streaming (position-vs-time) fit gave scale~0.92 but resid 7 deg - a time-alignment
artifact (1 kHz encoder vs ~20 fps cam during 70 deg/s motion). Switched to a
timing-free NET-ROTATION method: enc_net from firmware sweptDeg, cam_net from
settled cam before/after (stable to +-0.01 deg).
RESULT over 7 moves both directions: scale=0.83749 cam-deg/enc-deg, sign=+1,
resid_RMS=0.29 deg, max 0.37 deg. Independent check on 3 fresh moves at new
positions: err_RMS=0.76, max 0.86 deg -> PASS (<1.5 deg). Small (~0.8%) position
dependence in cam scale (top-arc perspective), negligible vs the 5-deg alarm.
Stored in %TEMP%\pw_cam_cal.txt. Host tools: pw_cam_cal_net.py (fit/check),
pw_baseline.py (campaign summary). Camera sees ~84% of true sweep over the top arc.

### T1 hang postmortem + loop-watchdog fix (2026-07-27 relaunch ~07:00)
SYMPTOM: at 00:49:50 a `G` spin printed `# SPIN-GEN START fasDir=-1 targetRevS=0.436
targetHz=2794` and then the board went fully silent — no RELEASE, no further serial
all night — until a manual RTS reset. Identical params (fasDir=-1, target 0.436)
had completed cleanly at 00:49:02, so the failure is INTERMITTENT, not a
deterministic logic error. (Evidence: %TEMP%\pw_agent.log, last board line is that
START.)
DIAGNOSIS: the spin generator's own 12 s software timeout (SPIN_GEN_TIMEOUT_MS)
provably never executed — it would have printed `# SPIN-GEN RELEASE reason=timeout`
at 00:50:02. So `loop()` itself was blocked, not merely stuck in SPIN_GEN state.
Static review of the whole hot path found NO software infinite loop (only the
bounded I2C-drain `while(Wire.available())`), every AS5600 read is bounded by
`Wire.setTimeOut(3)` and records failures instead of blocking, and serviceDiagnostic
Capture is gated/bounded. That rules out I2C lockup and busy-loops and leaves the
block inside a lower library/peripheral call reached during the high step-rate motor
ramp (FastAccelStepper's ESP32 step engine is the sole remaining suspect). Spin-gen
is the highest-stress operation on the rig — it is the only mode that accelerates the
full wheel from rest at 600 mA up toward ~2800 Hz — which fits "worse during a g/G
ramp." The exact blocking call could not be isolated from the logs.
ROOT-CAUSE-CLASS FIX (minimal, additive, evidence-first): there was NO hardware
watchdog, so ANY loop stall hangs forever needing a manual reset. Subscribed the
loop task to the ESP32 Task WDT (esp_task_wdt) at 4000 ms with trigger_panic=true and
feed it once per loop(). Now any future stall self-recovers by reset within 4 s;
setup() re-runs and calls driverFreewheel() so the wheel comes back a safe free
wheel, and the panic backtrace on any recurrence will finally pin the blocking call.
Does not touch the proven TMC2209 init block (invariant 6). The core pre-inits the
TWDT, so esp_task_wdt_init returns ESP_ERR_INVALID_STATE and the code falls back to
esp_task_wdt_reconfigure (boot log shows the expected `E task_wdt: ... already
initialized` followed by `# loop watchdog armed: 4000 ms`).
VERIFY: compile clean (32% flash). Flashed COM3 (Hash of data verified + Hard
resetting). 4 supervised spins alternating g/G all completed START->RELEASE(target)->
SPIN#n->V4-ENGAGE->TAKEOVER->LANDED with zero hangs, zero watchdog resets, zero
refusals: wedge 6/11/10/3, all isDare=0. Spin #4 hit target 0.442 rev/s (above the
0.436 that hung the old build) and still completed. T1 hang: FIXED.
OPEN: the underlying rare stall trigger is now masked by the watchdog but not
eliminated. If a reset ever fires mid-campaign, capture the panic backtrace (it will
name the blocking frame) to fix the true cause.

## 2026-07-27 Supervisor-v2 Session 1: dump-stall root cause + two robustness fixes

Context: relaunched after the loop-WDT fix (6ab587a) shipped. Owner re-steer said
treat spin-gen as disposable, take the simplest robust path, protect the party night.

Finding 1 — the WDT actually fired in the wild. Armed `d` (dense 1 kHz capture) then
ran a `g` spin. Clean spin (LANDED wedge, isDare=0), but ~6.5 s into the post-stop
dump the loop task WDT tripped: `E task_wdt: - loopTask (CPU 1) ... Aborting ...
Rebooting`. So the earlier "spin-gen hang" class was really: dumpDiagnostics() streams
~3072 lines; once the UART TX ring fills, Serial.printf busy-waits and blocks loop()
for ~16 s >> the 4000 ms WDT. The ramp was never the culprit for THIS stall. The WDT
did its job (self-recovered) — but the reboot wiped frame zero.

Fix A: feed `esp_task_wdt_reset(); yield();` every 32 lines in the dump loop. Verified:
identical arm-d + g spin now completes the full 3069-line dump with zero reboot markers
and the board stays responsive. Encoder trace itself was pristine (accepted=3072,
errors/gaps/alias/flips=0, maxAbsDelta=1) — no sensor jitter, so acoustic silence on
this spin was genuine, not a masked capture fault.

Finding 2 — frame zero was volatile. wedge0OffsetDeg was RAM-only; only sign/cal/accel
lived in NVS. So every WDT reboot AND every supervisor RTS reset silently reset the
wheel-0 boundary to 0.0. That is the exact "reset wipes frame zero, re-run zero-verify"
hazard the mission rules call out, and it bites automatically, not just on demand.

Fix B: `z` now persists wedge0OffsetDeg via preferences.putDouble("wedge0",...) and
setup() restores it with getDouble (same proven mechanism as the accel ceiling).
Verified with an RTS-reset round-trip: set z at raw~54deg (angle then reads 0.00),
RTS-pulse reboot (mode 10->0 confirms a real boot), angle still 0.00 after boot ->
offset survived. Frame zero now outlives both the WDT self-reset and supervisor resets.

Caveat left for the owner: the value persisted this session is a PLACEHOLDER at an
arbitrary rest position (used only to prove the round-trip), so the current wedge map
is misaligned. One attended `z` with the rim screw under the red pointer seeds the true
zero, and from now on it sticks. Documented at the top of AGENT_NOTES.

Acoustic note (T2b): built pw_mic.py (50 ms cadence: RMS dBFS, 1-6 kHz band, crest,
EWMA floor, auto 4 s anomaly clips, last 20). Ambient floor ~-97 dBFS / band ~-48 dB.
The clean g spin registered no band-energy rise and no clips; at the current mic
distance the peg clacks sit below the floor, so the mic cannot yet anchor the
"natural sound" reference. Needs closer placement or a lower threshold next session.

## 2026-07-27 SESSION 3 (~07:31) — LEGACY LATCH located + characterized (open item closed)

Context: session opened to a HARD blocker — frame zero is still the placeholder from
07:24:32 (only `# wedge-0 boundary set at current encoder angle (persisted)` event in
the serial log; no true attended `z` since). So the dare mask is misaligned and NO
isDare / landing / acceptance data is physically valid. I ran ZERO motor spins.
Also observed two EXTERNAL hand-spins at 07:31:27 (dir+1, omegaPeak 0.220) and
07:31:42 (dir-1, omegaPeak 0.257) — no SPIN-GEN markers, low peak omega, no campaign
driver process alive => a human (likely the owner) hand-spun the wheel at the bench,
not my work. Bench idle again from 07:31:59. Did not send motor commands into that.

Used the frame-blocked time on a pure code-audit item that needs no motor/frame:

### The "DARE RECOVERY FAILED: held" latch — FOUND. It is not hidden; it is the
>=3-attempts branch of the RECOVERY_HOLD state machine.
- prize_wheel.ino RECOVERY_HOLD case, lines ~2662-2671:
    if (isDare(currentWedge())) {
      if (recoveryAttempts < 3) { startDareRecovery(); }
      else {
        Serial.println(F("# DARE RECOVERY FAILED: held; do not use until inspected"));
        recoveryHoldStartedMs = millis();   // <-- only resets its own timer
      }
      break;                                 // <-- returns with coils STILL energized
    }
- Coil state: RECOVERY_HOLD is entered via driverActive(RECOVERY_HOLD_CURRENT_MA)
  (=500 mA; lines 2621 / 2638 / and the recovery-move completion paths ~1509/1521/1596/
  1685). The latch branch never calls driverFreewheel(). So on every subsequent loop it
  re-enters RECOVERY_HOLD, waits RECOVERY_HOLD_MS (400 ms), finds the wheel still on a
  dare with attempts>=3, reprints, and holds again => an INFINITE energized-hold loop at
  500 mA. This is the grip the owner felt.
- Why forcing the wheel doesn't free it: the only branches that touch this state early
  are `!encoderMotionReady()` (2648) and `fabsf(omega) > STILL_REV_S` (2652); both merely
  reset recoveryHoldStartedMs and break WITHOUT floating the coils — so pushing the wheel
  just makes the motor fight your hand, exactly as reported. recoveryAttempts only resets
  to 0 inside startSpinEvent-class entry points (~1626/1687), which never run while stuck
  here because no new spin is recognized under an energized hold. Self-clearing is
  impossible; it needs a power cycle / RTS reset.

### Minimal safe fix (READY, deliberately NOT yet committed — see why below)
Replace the latch branch body with a float-and-fault:
    else {
      driverFreewheel();                    // never grip a human
      Serial.println(F("# DARE RECOVERY FAILED: floating coils; hardware/cal fault"));
      updatePredictionError(); frictionFinalizeSpin(); persistCalibration();
      printLandedEvent();                    // records the fault outcome honestly
      sawSpinThisCycle = false; settleT0 = 0;
      mode = DRIFT_WATCH;                    // stay armed: a re-spin or drift is handled
    }
Rationale: after 3 genuine recovery failures this is by definition a hardware/calibration
fault, not a steer-able spin. Floating the coils is SAFE — an unbalanced dare rest with
floating coils is an ordinary free-wheel the guest can simply re-spin; it does not "declare"
the dare as an engineered outcome, it just stops the motor from clamping a person. Leaving
DRIFT_WATCH armed keeps the existing net for a re-spin or a roll-off. This trades a rare,
already-failed correction for the elimination of an indefinite grip on the wheel — a clear
safety win consistent with invariant 3 (nothing visibly restarts) and the "guests never
notice the motor" guarantee.

### Why NOT committed/flashed this session
Invariant 5 requires flashing after any firmware commit, and mission cadence requires >=4
verification spins after a firmware change. Exercising THIS branch means deliberately
landing on a dare and failing recovery 3x — which is only meaningful with a correct frame
zero (a misaligned mask makes "dare" physically meaningless). With frame zero still a
placeholder I cannot verify the fix, so committing a blind change to the core dare-recovery
path would violate the evidence-first discipline and risk a worse regression. The patch is
staged here verbatim; apply + flash + verify it in the FIRST session that has a valid frame
zero (fail-recovery test: force a dare rest, confirm the coils float and print the fault,
confirm a hand can move the wheel freely during the "failed" state).
