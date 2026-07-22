# BRIEF: Prize Wheel Firmware - Diagnose and Rebuild

## Task
Analyze the attached firmware (prize_wheel_CURRENT.ino), diagnose why the takeover
control is unstable, and rewrite it as a single compilable .ino that satisfies the
requirements below. Prioritize correctness and stability over features.

Build target: Arduino, ESP32 core 3.3.10, arduino-cli against esp32:esp32:esp32.

--------------------------------------------------------------------------------
## HARDWARE (all bench-validated - do NOT re-litigate these)
- ESP32-WROOM-32 (30-pin DevKit, CP2102 USB). ESP32 arduino core 3.3.10.
- BTT TMC2209 V1.3 over UART (single-wire on the RX pad):
    ESP32 TX=GPIO17 -> 1k resistor -> driver RX pad
    ESP32 RX=GPIO16 -> driver RX pad (direct)
    driver TX pin unconnected
  test_connection() returns 0 -> UART CONFIRMED WORKING.
- Driver config that is PROVEN correct (motor warm, cs_actual=25, no cycling):
    driver.I_scale_analog(false)   // use internal Iref, NOT the Vref pot (critical)
    driver.en_spreadCycle(true)    // SpreadCycle = full torque, no RPM cap
    driver.TCOOLTHRS(0); driver.semin(0); driver.semax(0)   // CoolStep OFF
    driver.rms_current(1450, 1.0)  // 1450 mA, IHOLD == IRUN
    R_SENSE = 0.11, microsteps = 16
- Pins: EN=GPIO25 (active low), STEP=GPIO26, DIR=GPIO27.
  Driver ENABLED at boot avoids a startup detent snap.
- Motor: NEMA17 1.7A / 51 N.cm. GT2 belt 2:1 (40T wheel / 20T motor).
  => 200*16*2 = 6400 motor microsteps per WHEEL revolution.
- Encoder: AS5600 magnetic, on the WHEEL shaft. SDA=GPIO21, SCL=GPIO22, 3.3V.
    I2C addr 0x36, RAW ANGLE register 0x0C (12-bit, 0-4095).
    Reads ACCURATELY AT REST (verified: manual wedge checks always correct).
    Magnet status OK, good air gap.
- Libraries: TMCStepper, FastAccelStepper, Wire.

## BENCH-PROVEN FACTS
- Motor drives and holds the 24" / ~0.9 kg wheel fine at 1450 mA. NOT torque-limited.
- Step skipping occurs ONLY under aggressive commanded acceleration.
  Gentle accel (~1000-1200 steps/s^2) runs clean. Exact max-clean accel NOT
  precisely characterized - the new code should help determine it.
- Bare motor (belt off) spins perfectly smooth. Wheel spins freely by hand and is
  well balanced (coasts a long time, settles at varied positions).

--------------------------------------------------------------------------------
## REQUIREMENTS
1. Wheel is HAND-SPUN by a guest and may be spun in EITHER direction (CW or CCW).
   A new spin can begin ~2 seconds after the previous one stops (must be robust to
   rapid re-spin from any state).
2. During free coast, coils FLOAT (TMC2209 freewheel) so the wheel feels loose in a
   guest's hand.
3. The wheel must NEVER come to rest on wedge 1 or wedge 5 (0-indexed; 12 wedges of
   30 deg each). All other wedges are valid landings ("dares" = 1 and 5).
4. If a spin would naturally land on a safe wedge, leave it 100% UNTOUCHED. Only
   intervene when a spin would otherwise land on a dare.
5. When intervening: the motor engages mid-spin, matches the wheel's motion, and
   gently decelerates it to a safe wedge, FINISHING IN THE SAME DIRECTION. It must
   NEVER reverse the wheel - reversing is both a visible tell AND physically causes
   step skipping (confirmed in testing).
6. The final motion must look like a NATURAL slowdown, not a motor driving the wheel.
7. Wedge-0 boundary calibration via a serial command; ideally persisted to NVS so it
   survives reboot.

--------------------------------------------------------------------------------
## KNOWN BUGS / PRIORITIES (in order)

### PRIORITY 1 - VELOCITY SIGNAL INTEGRITY (do this FIRST)
In the takeover debug logs (see debug_log_omega_garbage.txt), the computed wheel
angular velocity `omega` OSCILLATES IN SIGN and SPIKES to +/-4 rev/s within 100 ms
intervals. This is PHYSICALLY IMPOSSIBLE for a 0.9 kg wheel - it cannot reverse and
hit 4 rev/s that fast. Example consecutive samples (~100-180 ms apart):
    omega=0.759, -0.610, -0.138, -0.615, -0.687, -0.084, -0.539, -3.126, -4.395,
    -0.901, 1.266, 0.291, -3.836 ...
The angle column in the SAME log moves smoothly and monotonically, so the raw angle
looks OK but the VELOCITY DERIVATION is garbage - OR the angle read is glitching only
intermittently in a way the velocity amplifies.

Every control decision keys off omega (and the predicted stop derived from it). If
omega is garbage during takeover, NO control architecture can work. Candidate causes
to investigate:
  - I2C timing/contention: does the AS5600 read get disrupted when the motor is
    actively stepping (FastAccelStepper uses RMT/timers)? Loop rate may change.
  - Loop-rate assumption: velocity = d(angle)/dt with a fixed/assumed dt. If the loop
    stalls (serial printing, I2C retries), dt is wrong and velocity explodes.
  - Shortest-path unwrap aliasing: if a loop iteration takes long enough that the
    wheel moves >180 deg (2048 counts) between samples, the unwrap picks the wrong
    direction -> apparent reversal + huge velocity spike. At high spin speed this is
    plausible if the loop ever stalls.
  - Velocity low-pass filter instability or bad dt handling.

REQUIRED: add high-rate diagnostic logging (buffer samples to RAM with MICROSECOND
timestamps, dump AFTER the wheel stops so serial printing doesn't perturb timing).
Characterize: actual sample interval (is it stable?), max counts-between-samples at
peak speed (near the 2048 aliasing limit?), and whether raw angle is monotonic
through a one-direction spin. FIX the sensing so angle+velocity are trustworthy
during an ACTIVE-MOTOR takeover, not just at rest. This is very likely the true root
cause of everything below.

### PRIORITY 2 - TAKEOVER CONTROL STABILITY
Prior attempts that FAILED:
  (a) Predict natural landing, then open-loop FastAccelStepper moveTo() to a computed
      target motor position. Result: skipped steps during decel, lost position, landed
      wrong. LANDED wedge disagreed with a manual check at the same position.
  (b) Continuous per-tick closed loop that re-issued runForward()/setSpeedInHz()/
      setCurrentPosition() EVERY loop iteration. Result: violent omega oscillation and
      skipping - re-commanding FastAccelStepper every few ms restarts its motion
      planner constantly, so the motor lurches and never runs smoothly.
Design a control method that is STABLE, respects the accel ceiling, SELF-CORRECTS for
any skipped steps using the encoder as ground truth, and NEVER reverses. Note:
FastAccelStepper is designed to be commanded once and left to ramp on its hardware
timer; re-commanding every tick is wrong. If finer closed-loop control is needed,
consider generating steps directly (LEDC/MCPWM/RMT or timer ISR) so the loop can set
instantaneous step rate each cycle without a planner fighting it. Choose the approach
you can make provably stable.

### PRIORITY 3 - LANDED DETECTION
Determine the landed wedge ONLY after the wheel is TRULY stopped: |omega| below a
small threshold sustained for a settle window, then sample the settled encoder angle.
Do NOT sample mid-drift (this caused LANDED to report the wrong wedge while the wheel
was still creeping).

--------------------------------------------------------------------------------
## DELIVERABLE
A single compilable prize_wheel.ino (ESP32 core 3.3.10) satisfying all requirements,
building cleanly with:
  arduino-cli compile --fqbn esp32:esp32:esp32 <sketchdir>
Include serial debug output for calibration and diagnosis. Audio/LEDs are OUT OF
SCOPE for v1. Keep the existing proven driver config verbatim.

## TEST PROTOCOL THE CODE MUST SUPPORT
1. Calibrate: rotate so pointer sits on a wedge boundary, send `z`.
2. Enable debug logging.
3. Hand-spin BOTH directions, aimed so the natural stop would be wedge 1 or 5.
4. Confirm: it steers away in BOTH directions, lands on a SAFE wedge, never reverses,
   the motion looks like a natural slowdown, and LANDED matches a manual angle check.

## NOTES ON THE ATTACHED CODE
prize_wheel_CURRENT.ino is the latest state. It contains the proven driver config
(driverConfig()), the AS5600 read (readRaw/updateEncoder), the spin/state machine in
loop(), beginTakeover()/takeoverStep() (the unstable part), chooseSafeTargetAngle()
and the friction-model prediction (predictStopAngle) which is seeded, NOT calibrated
to this wheel. The velocity is computed in updateEncoder() - scrutinize its dt
handling and unwrap first.
