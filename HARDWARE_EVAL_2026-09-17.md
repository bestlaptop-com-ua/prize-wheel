# Wheel v2 bring-up session summary (2026-09-17)

Live bring-up of the 36" NEMA23/TMC5160T Pro hardware on MILL-PC. This
supersedes the "in progress" status in HARDWARE_V2.md for everything below;
that file's mechanical/electrical spec is otherwise still current.

## Review status (2026-09-18)

The follow-up review and owner discussion supersede the original diagnosis and
belt decision below. **Insufficient NEMA23 torque has not been established.**
Keep the current direct-drive assembly for diagnosis; neither the belt nor the
spare NEMA34 is an approved, validated fix. See
[the follow-up review](HARDWARE_REVIEW_2026-09-18.md) for evidence, corrections,
confirmed spare hardware, and the next-session procedure. Production firmware
includes uncommitted changes and must be reconciled before interpreting tests.

## Reported bring-up observations, in order

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

3. **Driver/current bring-up.** A bench sweep reported about 2385 mA at
   `irun=31`. This does **not** establish a hardware ceiling: TMC5160 current
   also depends on `GLOBALSCALER`. The original 2.385 A ceiling claim is
   withdrawn; verify the installed library and register values. SpreadCycle
   was used successfully; this session does not establish that StealthChop
   is universally incapable of the load.

4. **Open-loop motion (reported bench sketches).** Continuous spin,
   reversal, and an accel-cruise-decel-hold cycle reportedly ran at 2000 mA
   SpreadCycle. This supports basic motion capability, but does not validate
   capture of a freely spinning rotor or braking at the firmware's lower
   brake/taper currents. The bench sketches and raw traces are not committed
   here, so their exact conditions cannot be independently checked.

5. **Wheel imbalance identified.** A free hand-spin coast-down (logged via
   repeated `s` polling) showed the wheel consistently decelerating to a
   stop in the 150-180 degree range (wedge 5) regardless of starting
   conditions - a real mass imbalance, not measurement noise. Counterweight
   balancing was not completed. Determine the physical heavy side with the
   motor mechanically disconnected and flapper lifted; do not infer the
   counterweight location directly from the logged encoder angle.

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
  NEMA17-scale; `FC_SUSTAINED_SPEEDUP` was reported on real spin attempts.
  Insufficient brake authority was the session hypothesis, not an isolated
  measurement of available torque.
- **Friction model seed** (`cw_c/cw_b/ccw_c/ccw_b`, persisted in NVS):
  0.30/0.15 -> 0.55/0.28, force-written at boot. Old values (tuned for the
  lighter NEMA17/belt wheel) were suspected to understate drag. The reported
  `FC_MOTOR_FIGHT` mismatch does not by itself establish that explanation. **This override is a one-time
  fix and currently reruns on every boot** - it needs to become
  NVS-flag-guarded (write once, then let the online fit take over
  permanently) before this goes near an actual party, or every power cycle
  will discard whatever the online fit has learned.

## Revised diagnosis and hardware decision

Three real hand-spins reportedly faulted around 150-180 degrees, near the
natural rest position. This supports investigating imbalance, but does not
prove it is the sole fault cause. The earlier claims that capture errors and
inadequate torque had both been ruled out are withdrawn.

`fasWheelRevS()` reads FastAccelStepper's pulse/ramp speed, not rotor speed.
`MOTOR_FIGHT` fires when measured forward wheel speed remains below 45% of
that speed for the confirmation interval. It detects a speed mismatch; it
cannot identify whether the cause is gravity, binding, capture transients,
missed steps, encoder errors, or command-tracking behavior. In particular,
it is not a detector for the wheel overtaking the motor's commanded speed.

The owner's observation that braking produces no rattle weakens a sustained
loss-of-synchronism hypothesis, but a brief slip or early shutdown may be
quiet. Starting a pulse train close to measured mechanical speed does not
prove electrical phase alignment with the free-spinning rotor.

A 3:1 HTD 5M belt (60T wheel / 20T motor) remains an option, not the current
next action. Ideal torque multiplication and reflected-inertia reduction do
not establish actual torque at the higher motor speed. **It requires more
than changing `GEAR_RATIO`:** relocate the encoder to the wheel shaft or
redesign encoder scaling/unwrapping and startup reference, then review
step-based limits and retune capture. Pulley bore, mounting, belt width,
tension, and availability also remain unverified.

The spare NEMA34/DM860T remains an alternative after diagnosis. Its greater
holding torque does not establish a cure. DM860T loses software-programmable
current staging and SPI diagnostics, but the pictured revision has alarm
terminals. Its noise/free-spin behavior requires testing. The current TMC
firmware runs SpreadCycle; it does not currently switch into StealthChop.

## Open items for next session

- [ ] Obtain and preserve the exact flashed source, uncommitted changes,
      library versions, and production settings from MILL-PC.
- [ ] Balance with motor mechanically disconnected and flapper lifted;
      check bearings/coupling/mount alignment over a full revolution.
- [ ] Verify TMC current configuration including GLOBALSCALER and the
      motor's current-rating convention before increasing current.
- [ ] Record timestamped angle, measured speed, pulse/ramp speed, target
      speed, state, current stage, and fault transitions through capture.
- [ ] Compare starts from rest with hand-spin captures in both directions,
      at several starting angles and at actual brake/taper currents.
- [ ] Replace the every-boot friction seed overwrite with a versioned,
      one-time migration or explicit reset, preserving learned values.
- [ ] Confirm new-spin release during soft hold and disabled-driver startup
      handling without weakening genuine fault protection.
- [ ] Integrate and validate the bench-reported I2S audio path; it is not
      currently enabled in the main sketch.
- [ ] Choose a hardware change only after the above evidence identifies
      the remaining limitation; follow the follow-up review if needed.
