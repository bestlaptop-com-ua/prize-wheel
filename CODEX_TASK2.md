# CODEX TASK 2 - Diagnose why the wheel lands on dare wedges, then fix

## Context
prize_wheel.ino (in this folder) is the P1-sensing build you wrote earlier. The
velocity sensing is now good. The remaining problem: the wheel sometimes comes to
rest on a DARE wedge (dares = wedge 1 and wedge 5, 0-indexed), which must never
happen. The firmware self-reports these as "LANDED wedge N <-- DARE!".

## IMPORTANT - do NOT repeat a bad analysis
A previous analysis looked at a SINGLE mid-spin serial line:
   sensor=VALID age_us=55 angle=52.73 wedge=1 omega=0.16 predStop=126.1(w4)
and wrongly concluded "the wheel is dying on wedge 1 and the friction model
over-predicts." That conclusion is UNSOUND: a real spin makes SEVERAL FULL TURNS
before landing, so any single mid-spin sample (a given wedge + a given omega) tells
you nothing about the final resting wedge. omega=0.16 in the middle of a multi-turn
spin is not "about to stop." Do not infer end-state behavior from isolated mid-spin
frames. Instrument the FULL causal sequence of each spin instead.

## STEP 1 - Add clean per-spin event logging (do this FIRST, change no control logic yet)
Add a concise, unambiguous log that prints EXACTLY these events per spin and nothing
else noisy (gate the existing high-rate/live spam behind a separate flag so it does
not drown this):

  - On spin detected:
      "SPIN#<n> START dir=<+1/-1> omegaPeak=<rev/s>"
  - At the SINGLE decision instant (when the state machine evaluates whether to
    intervene):
      "SPIN#<n> DECISION omega=<> curWedge=<> predStopAngle=<> predStopWedge=<>
       action=<STEER targetWedge=<> | LEAVE>"
  - If it steers, when the intervention starts:
      "SPIN#<n> TAKEOVER targetAngle=<> runwayDeg=<> tgtU=<>"
  - On final true stop (after settle):
      "SPIN#<n> LANDED wedge=<> angle=<> isDare=<0/1> steered=<0/1>
       predWedgeWas=<> predAngleWas=<>"

Keep a per-spin counter n. Record predStopWedge/predStopAngle AT the decision instant
and echo them again in the LANDED line so we can compare prediction vs reality for the
SAME spin. Make it work for BOTH spin directions.

This yields, per spin, a 3-4 line story: what it predicted, what it decided and why,
and where it actually ended up. That makes the failure mode unambiguous:
  - predicted safe + LEAVE + landed dare  => PREDICTION error (model wrong)
  - STEER + landed dare                   => STEERING/settle error (control wrong)
  - decision made when omega already tiny => DECISION TIMING error (too late)

## STEP 2 - Collect data (the human will run this)
The human will: calibrate (z), enable this per-spin log, and perform ~10 real
hand-spins in BOTH directions, including several that land on dares, and paste the
per-spin lines back. DO NOT guess the root cause before seeing this data. A typical
spin is multiple full revolutions over a few seconds.

## STEP 3 - Fix based on what the data actually shows
Only after the per-spin logs identify which of the three failure modes is occurring,
implement the minimal correct fix:
  - If PREDICTION error: the friction model (predictStopAngle, coefficients cw_c/cw_b/
    ccw_c/ccw_b) is not calibrated to this wheel. Either (a) fit coefficients from the
    high-rate capture data, and/or (b) make the intervention decision robust to
    prediction error with a margin, but JUSTIFY the margin from the measured
    prediction error magnitude, not a guess.
  - If STEERING/settle error: fix beginTakeover/takeoverStep/settle so the wheel
    actually reaches and stops in the intended safe wedge. Never reverse direction
    (reversing is a visual tell AND causes step skipping - confirmed).
  - If DECISION TIMING error: the decision fires at too low an omega to steer
    effectively; move the decision earlier (higher omega) while keeping enough spin
    for the takeover to look natural.

## HARD CONSTRAINTS (unchanged, bench-validated - keep verbatim)
- Board: ESP32 core 3.3.10, FQBN esp32:esp32:esp32, arduino-cli.
- Keep the proven TMC2209 driverConfig (I_scale_analog(false), en_spreadCycle(true),
  CoolStep off, rms_current(1450,1.0), R_SENSE 0.11, microsteps 16).
- Pins EN=25 STEP=26 DIR=27, AS5600 SDA=21 SCL=22.
- ENCODER_DIR_SIGN = -1 (wedges count clockwise) must stay.
- Dares = wedge 1 and wedge 5. During free coast, coils freewheel (loose in hand).
  Never reverse the wheel. Determine LANDED only after a TRUE sustained stop.
- Must be robust to a new spin starting ~2 s after the previous stop, from any state.
- Build must compile clean:
    arduino-cli compile --fqbn esp32:esp32:esp32 <sketchdir>

## NOTE ON A PENDING EDIT TO REVERT
A hand edit added a margin-based "needSteer" block and a DARE_MARGIN_WEDGES define to
the FREE_SPIN decision. This was premature (added before proper diagnosis). REVERT
that decision block back to the original single-decision form, add the per-spin
logging from STEP 1 instead, and only reintroduce a margin later if STEP 2 data
justifies it. The original decision was:
    if (sawSpinThisCycle && fabsf(omega) <= TAKEOVER_REV_S && fabsf(omega) > 0.08f) {
      if (isDare(predictStopWedge())) { beginTakeover(); }
      else { mode = SETTLE; settleT0 = 0; }
    }

## DELIVERABLE
Updated prize_wheel.ino that (1) compiles clean, (2) has the clean per-spin event log
for diagnosis, (3) has the premature margin hack reverted. Do NOT finalize a control
fix until the human returns per-spin data. If you want, after adding logging, briefly
state your HYPOTHESES for each of the three failure modes and what the log would show
for each - but do not commit a fix yet.
