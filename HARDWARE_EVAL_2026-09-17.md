# Wheel v2 bring-up session summary (2026-09-17)

Live bring-up of the 36" NEMA23/TMC5160T Pro hardware on MILL-PC. This
supersedes the "in progress" status in HARDWARE_V2.md for everything below;
that file's mechanical/electrical spec is otherwise still current.

## What got verified working, in order

1. **AS5600 encoder (I2C, GPIO 38/39).** Initial bring-up showed
   `MD=0 ML=1 raw=0` (magnet undetected) - the diametric magnet on the motor's
   rear stub was either too far from the sensor or off-axis. After
   re-seating: `MD=1 ML=0 AGC=100-128 raw` sweeping correctly through a full
   revolution. Confirmed stable at both driver-disabled and driver-enabled
   (2-2.4 A) current levels.

2. **TMC5160T Pro over SPI (bent CFG pins into a DIP-switch breakout).**
   `driver.version()==0x30`, `test_connection()==0`, no `s2ga/s2gb/ola/olb`
   flags at any current tested. SPI link intermittently dropped once during
   handling (recovered on its own, or on re-seating - not conclusively
   isolated) - worth reseating CS/MISO/VIO if it recurs.

3. **Motor current ceiling.** R_SENSE=0.075ohm on this board caps rms_current
   at ~2385 mA (irun=31, vsense=0) - this is a hard ceiling, not a setting.
   Confirmed via bench sweep from 400 mA to 2400 mA; SpreadCycle
   (`en_pwm_mode(false)`) needed for real torque, StealthChop
   (`en_pwm_mode(true)`) is voltage-mode and too weak under load.

4. **Open-loop motion (wheel_v2_run.ino / wheel_v2_cycle.ino bench
   sketches).** Continuous spin, direction reversal, and a full
   accel-cruise-decel-hold cycle all ran cleanly at 2000 mA SpreadCycle with
   no faults, confirming the motor/driver/mount/coupling have enough torque
   and mechanical integrity to drive this wheel. This became the key
   reference point later: a system with no closed-loop targeting handles
   this wheel fine, so failures in the closed-loop system are about the
   *model*, not raw capability.

5. **Wheel imbalance identified.** A free hand-spin coast-down (logged via
   repeated `s` polling) showed the wheel consistently decelerating to a
   stop in the 150-180 degree range (wedge 5) regardless of starting
   conditions - a real mass imbalance, not measurement noise. Counterweight
   fix (washers on the rim opposite, ~330-360 degrees) was identified but
   not completed this session.

## Firmware: v1 -> v2 port (prize_wheel_gpt.ino)

Ported from the UART TMC2209/NEMA17/2:1-belt bench rig to the SPI
TMC5160T Pro/NEMA23/1:1-direct-drive v2 hardware:

| Change | From (v1) | To (v2) |
|---|---|---|
| Driver object | `TMC2209Stepper` via `Serial2` UART | `TMC5160Stepper` via SPI (CS=10, MOSI=11, MISO=13, SCK=12) |
| `R_SENSE` | 0.11f | 0.075f |
| `PIN_EN` | 4 | 7 |
| `PIN_SDA` / `PIN_SCL` | 8 / 9 | 38 / 39 |
| `GEAR_RATIO` | 2.0f (belt) | 1.0f (direct drive) |
| SpreadCycle enable | `driver.en_spreadCycle(true)` | `driver.en_pwm_mode(false)` (TMC5160: false=SpreadCycle) |
| `driver.I_scale_analog(false)` | present | removed - TMC2209-only GCONF bit, not on TMC5160's register map (compile error) |
| Boot UART init | `TMC_SERIAL.begin(...)` | removed - no serial link to the driver exists |
| `PW_LED_PIN` (pw_party.h) | 21 | 40 |
| `PW_FX_AUDIO_ENABLE` (pw_party.h) | 1 (DFPlayer on UART1) | 0 - no DFPlayer in v2; effects path is S3 I2S -> PCM5102A -> TPA3116D2, not yet wired into this sketch |

`checkTmcUartRaw()`/`checkTmcUartOrFault()` needed **no changes** despite the
name - `driver.test_connection()` is chip-agnostic in TMCStepper and works
identically over SPI.

## Tuning changes (leftover-from-v1 constants, corrected against real data)

- **`DIR_PROBE_CURRENT_MA`**: 350 -> 1600 mA. At 350 mA the direction probe
  stalled the motor outright (real hardware; not a wiring fault) - far below
  anything bench-confirmed to move this wheel.
- **`DIR_PROBE_TIMEOUT_MS`**: 7000 -> 15000 ms. Even at 1600 mA, each probe
  leg genuinely needs more than 7 s to settle against this wheel's friction;
  the old timeout was cutting off moves that were still succeeding, not
  hung ones.
- **Current staging** (`CUR_PRECHARGE/CAPTURE/BRAKE/TAPER/HOLD1/HOLD2_MA`):
  100/600/450/300/150/80 -> 350/2200/1650/1100/550/300. The old values were
  NEMA17-scale and caused `FC_SUSTAINED_SPEEDUP` (insufficient brake
  authority) on every real spin attempt.
- **Friction model seed** (`cw_c/cw_b/ccw_c/ccw_b`, persisted in NVS):
  0.30/0.15 -> 0.55/0.28, force-written at boot. Old values (tuned for the
  lighter NEMA17/belt wheel) understated this wheel's real drag, so the
  planned brake curve was consistently overtaken by faster-than-planned
  natural deceleration (`FC_MOTOR_FIGHT`). **This override is a one-time
  fix and currently reruns on every boot** - it needs to become
  NVS-flag-guarded (write once, then let the online fit take over
  permanently) before this goes near an actual party, or every power cycle
  will discard whatever the online fit has learned.

## Root-cause chain on the two live-spin faults

Three consecutive real hand-spins all faulted in the same ~150-180 degree
zone (wedge 5) - the same location the free-coast test identified as the
wheel's imbalance-driven natural rest point. Diagnosis, arrived at through
elimination:

1. First hypothesis (capture-phase ramping too fast into the wheel) -
   **ruled out** by reading the actual `ST_SPEED_MATCH_CAPTURE` code: `cmd0`
   is jump-started at the wheel's already-captured speed and `aPlan` is a
   deceleration from the first tick, not a ramp-up.
2. Second hypothesis (motor torque insufficient to decelerate) - **ruled
   out** by the user's observation that the motor stays quiet under the
   fault (a genuine stall/slip growls, this doesn't) and by the open-loop
   cycle test cleanly decelerating the same wheel at comparable current.
3. Confirmed mechanism: `fasWheelRevS()` (the fight-check's `fasActual`)
   reads FastAccelStepper's own current commanded speed. The fault fires
   when real wheel speed falls *below* that commanded speed by more than
   `FIGHT_SPEED_FRACTION` - meaning the wheel is decelerating **faster**
   than the flat `c + b*omega` friction model predicts, not slower. A flat,
   speed-only model cannot represent a wheel with a mass imbalance, which
   adds a position-dependent (pendulum-like) torque on top of ordinary
   friction. Raising the friction constants (done above) shifts the
   average assumption but cannot fix an error that is a function of
   *angle*, not speed - which is why the fault kept recurring at the same
   physical location across multiple different constant values.
4. Torque itself was separately confirmed adequate (open-loop cycle test),
   so the outstanding gap is entirely: (a) the imbalance itself, and/or
   (b) giving the control loop enough authority that the imbalance's
   disturbance torque is a smaller fraction of what it can reject.

## Decision: 3:1 belt reduction (not NEMA34/DM860)

Considered switching to a spare NEMA34 (4.5 N*m) + DM860 driver to add
torque margin against the imbalance. Rejected in favor of a 3:1 HTD belt
reduction on the existing NEMA23/TMC5160T Pro, because:

- The belt multiplies wheel-side torque 3x for the same current (~4.5 N*m
  at ~2.2 A capture current) - comparable to the NEMA34's rated torque -
  and divides reflected inertia by 9, which directly helps the exact
  disturbance-rejection problem causing `FC_MOTOR_FIGHT`.
- Zero firmware rework beyond `GEAR_RATIO` (1.0 -> 3.0). Every other v2
  change (SPI driver, current staging, StealthChop/SpreadCycle, the fight
  watchdog, SPI health checks) stays exactly as tuned this session.
- DM860 would have cost StealthChop (audible hum at standstill/slow phases
  - the thing this whole build was chosen to avoid), all six current
  stages (collapses to one fixed DIP-switch value), and SPI health
  monitoring (`test_connection`, `DRV_STATUS`) - a much larger regression
  for a torque gain the belt achieves anyway.

**Parts identified for the belt (not yet ordered/built):** HTD 5M, 60T
(wheel side, ream or press to 3/4" bore) + 20T (motor side, 8mm bore,
direct fit) + matching belt, sourced as a pre-matched 3:1 kit for fast
delivery. HTD 8M was considered for more torque margin but sourcing a
60-tooth pulley with a near-3/4" bore without an outside machine-shop step
added real lead time; 5M's margin is more than adequate for this load.
Motor moves off-axis onto a flat mounting plate, which also resolves the
earlier motor-mount-flex issue from the direct-drive attempt.

## Open items for next session

- [ ] Build the 3:1 belt reduction (parts sourced, not yet ordered/built)
- [ ] `GEAR_RATIO` 1.0 -> 3.0 once the belt is installed
- [ ] Guard the friction-seed override in `setup()` behind a one-time NVS
      flag so the online fit's learned corrections survive power cycles
- [ ] Wheel counterweight balance pass (rim washers opposite the ~165-176
      degree rest point) - still worth doing even with the belt, since it
      reduces the disturbance the control loop has to reject in the first
      place
- [ ] PCM5102A -> TPA3116D2 audio path is wired and bench-tested
      (wheel_v2_audio.ino) but not yet integrated into prize_wheel_gpt.ino
      (`PW_FX_AUDIO_ENABLE` is currently 0)
- [ ] Re-run the direction probe (`p`) and a handful of real spins once the
      belt is in, to get the online friction fit correcting from real 3:1
      data rather than the direct-drive numbers above
