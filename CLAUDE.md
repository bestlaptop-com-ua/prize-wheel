# Prize Wheel — Autonomous Engineering Brief

You are working on a 24" hand-spun party prize wheel with a concealed ESP32 +
stepper that must NEVER let the wheel rest on dare wedges 1 or 5, while
looking and feeling like an ordinary free wheel. Guests must never notice the
motor. Naturalness is a hard requirement equal to the no-dare guarantee.
The owner has authorized fully autonomous operation: you may run the motor,
flash firmware, and iterate without approvals. Log a `MOTOR RUN:` line to
your notes before every motor action for the audit trail.

## Invariants (never violate)
1. Dares (wedges 1 and 5) are never the final resting outcome.
2. The wheel never moves opposite to the spin direction of the current spin.
3. A cleanly stopped wheel must not visibly restart (corrections happen in
   motion or within ~300 ms of stopping).
4. Branch `claude/adaptive-v2` is yours; NEVER commit to `main`. When you
   declare the mission complete, leave the repo checked out on clean `main`.
5. After every firmware commit, flash the board (procedure below).
6. Experimental currents <= 900 mA; commanded accel <= persisted ceiling.
   Do not modify the proven TMC2209 init block (rms 1450, spreadCycle, etc.).
7. The wheel is deliberately unbalanced and stays so. Hardware is what it
   is: characterize limits, design around them; if you hit a true hardware
   wall, PROVE it with three-way evidence and propose the simplest fix in
   your report instead of hammering the joint.

## Hardware facts (measured, trust these)
- ESP32-WROOM (FQBN esp32:esp32:esp32, core 3.3.10), COM3, 115200.
- AS5600 on wheel shaft, 4096 counts/rev, ENCODER_DIR_SIGN=-1. 12 wedges of
  30 deg; firmware angle 0 = wedge 11/0 edge; a bright screw on the rim sits
  at physical 0 (under the red pointer when aligned).
- NEMA17, GT2 2:1, 6400 usteps/wheel-rev. Accel ceiling (probe `a`):
  currently 900 sps2 persisted; it was 1125 earlier the same day - the drivetrain
  weakened during the day; treat the CURRENT persisted ceiling as truth and
  re-probe sparingly (each probe run includes a step-loss stage = stress).
- Engagement (matched-speed jump start) is PROVEN <= 0.30 rev/s and DESYNCS
  at ~0.47-0.49 rev/s (both directions). Below 0.14 rev/s the old v2 nets own
  the spin. These are the known dead zones - characterize precisely if you
  need to move them; do not assume.
- Imbalance physics: gravity rivals friction below ~0.3 rev/s; stop
  prediction is hopeless (MAE 40-60 deg); motor-placed stops can roll after
  coil release (DRIFT_WATCH guards this); pole slip on hills below ~600 mA;
  energizing a moving rotor snaps up to ~1.8 deg (carry reversal guard 3 deg).

## The three frames (core discipline)
Physical face | encoder (firmware) | camera. Historic face-vs-encoder
divergences (33/78/150/255 deg) occurred ONLY during grind/stall/latch events,
never during clean sweeps (proven by camera three-way on 26 Jul). If camera
and encoder disagree beyond 5 deg (after calibration): STOP motor work,
preserve logs, attribute (cam full + enc short = magnet chain; cam short +
enc full = disc slipped; both short = motor/belt), write it up.

## Instruments already running (use via files; do not re-implement)
- Serial bridge (owns COM3): send = write single char to
  %TEMP%\pw_serial_cmd.txt (no newline); read = %TEMP%\pw_serial.log
  (timestamped). Stop = create %TEMP%\pw_serial_stop (also kill pid in
  %TEMP%\pw_bridge.pid). Restart = Start-Process powershell -File
  %TEMP%\pw_bridge.ps1 (hidden), save new pid. MUST stop before flashing;
  wait >= 5 s after esptool before restarting or it opens a dead handle
  (symptom: pid alive, no log).
- Camera tracker v2 (owns webcam): %TEMP%\pw_cam_tracker2.py running;
  output %TEMP%\pw_cam.log lines `HH:MM:SS.mmm cam=  angle d= delta q= qual`;
  stop file %TEMP%\pw_cam_stop, pid %TEMP%\pw_cam.pid. It integrates arc
  phase-correlation over the visible TOP HALF of the wheel (camera sees only
  the top; lower half is a camera dead zone - fine). SCALE AND SIGN ARE
  UNCALIBRATED: first mission task fits cam-vs-encoder over a gentle motor
  move and stores them in %TEMP%\pw_cam_cal.txt. If the laptop or camera
  moves, geometry constants (CX,CY,R in the script) must be re-measured.
  NVIDIA Broadcast steals the camera; kill processes matching
  'broadcast|nvcam' if frames stop.
- Serial commands: z zero (wheel held at 0 edge) | p attended dir probe
  (refuses unless pointer centered in wedge 3 or 7-11) | a accel staircase
  (STRESSFUL - contains a step-loss stage; use sparingly) | s status |
  c cal print | C cal reset (also resets ceiling seed - requires re-probe) |
  v toggle takeover 200ms telemetry | d diag | m mask | ? help.
- Log grammar per spin: SPIN#n START/PLAN/PLAN-CANCEL/DECISION/V4-ENGAGE/
  TAKEOVER/CREEP-CARRY/RECOVERY/FRICTION/PRED-ERR/LANDED (+ `# ...` lines).
  LANDED isDare must always be 0. targetErrorDeg is misleading on gated
  profiled moves (known cosmetic; wedge numbers are authoritative).

## Flash cycle (exact)
cli = 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe'
libs = "$env:USERPROFILE\OneDrive\Documents\Arduino\libraries"   (mandatory --libraries)
sketch = <repo>\prize_wheel   ; board esp32:esp32:esp32 on COM3
compile-only first; judge upload by 'Hash of data verified' + 'Hard
resetting'; esptool 5.3 delta-reflash writes partial sectors - NORMAL.
Board reset without flashing (latch escape): stop bridge, open COM3 with
DTR=false RTS=false, pulse RTS true 150 ms then false, close, restart bridge.

## Known open items in firmware (branch claude/adaptive-v2, v4.4 = 47eebd1)
- v4 pipeline: engage every decaying spin 0.14-0.30 rev/s, stretched single
  ramp (soft cap 400 sps2), 600->450 mA two-stage, soft fight-abort with
  1.5 s cooldown, DRIFT_WATCH after release. v2 planner/guards remain as
  dormant nets below 0.14.
- A LEGACY LATCH prints '# DARE RECOVERY FAILED: held; do not use until
  inspected' and holds coils after repeated failed recoveries. Its source
  has never been located. Find it, understand it, make it safe (it once
  gripped while the owner held the wheel).
- Slow-speed engage produces an audible click; ccw direction has shown a
  rattle-under-braking; recovery slip moves stalled once when pushing uphill
  near the 4/5 edge (moved <1 deg of a commanded 16). All need evidence-first
  diagnosis, minimal fixes.
- CLAUDE_VARIANT.md is the running engineering log - append decisions and
  evidence there as you go.
