# Prize Wheel

ESP32 firmware for a hand-spun 24" prize wheel that quietly steers away from two
designated "dare" wedges while otherwise behaving like an ordinary free-spinning
wheel.

> **Party-v1 note:** the historical concept/architecture sections below describe
> earlier firmware. The current accepted `prize_wheel_gpt` controller captures
> every confirmed spin and guides it to a safe target; use `DELIVERY.md`,
> `RISK_AUDIT.md`, and the source constants for current party operation.

## Concept

A guest spins the wheel by hand, in either direction. A hidden ESP32 watches the
wheel through a magnetic encoder on the wheel shaft and predicts where it will
naturally come to rest.

- If the predicted stop is a **safe** wedge, the firmware does nothing at all.
  The motor coils stay floating and the wheel coasts to rest completely untouched.
- If the predicted stop is a **dare** wedge (wedge 1 or 5), the motor engages
  mid-coast, matches the wheel's speed slightly *from behind*, and brakes it onto
  a safe wedge — always continuing in the direction the guest spun it.

Two rules govern every intervention:

1. **Brake, never drive.** The motor is commanded at ~90% of the wheel's measured
   speed so it can only ever trail and retard the wheel, never pull it forward.
2. **Never reverse.** Reversing is both a visible tell and the confirmed
   mechanical cause of step skipping on this build.

## Hardware

| Part | Detail |
|---|---|
| MCU | ESP32-WROOM-32, 30-pin DevKit (CP2102), Arduino core 3.3.10 |
| Driver | BTT TMC2209 V1.3, single-wire UART on the RX pad |
| Motor | NEMA17, 1.7 A, 51 N.cm |
| Transmission | GT2 belt, 2:1 (40T wheel / 20T motor) |
| Encoder | AS5600 magnetic, on the **wheel** shaft, I2C 0x36 |
| Wheel | 24", ~0.9 kg, 12 wedges of 30 deg |

### Pinout

| Signal | GPIO |
|---|---|
| TMC UART TX | 17 (via 1k resistor to driver RX pad) |
| TMC UART RX | 16 (direct to driver RX pad) |
| Motor EN | 25 (active low) |
| Motor STEP | 26 |
| Motor DIR | 27 |
| AS5600 SDA | 21 |
| AS5600 SCL | 22 |

Driver TX pin is unconnected. The driver is enabled at boot to avoid a startup
detent snap.

## Party v1: audio, rim lighting, and phone console

The party build is `prize_wheel_gpt/prize_wheel_gpt.ino`. WiFi and FX are
fail-silent consumers of the accepted control state; they do not decide when
to reserve, capture, brake, or fault. Send `t` over USB serial or telnet to
print the measured maximum combined WiFi/FX service time. Send `T` to reset
that tracker before a bench run.

### DFPlayer Mini wiring

`FX_TASK.md` requests DFPlayer UART2 on GPIO16/17, but those pins and UART2 are
already the production TMC2209 link. Connecting the DFPlayer there would put
two protocols and, if DFPlayer TX were attached, two transmitters on the motor
driver bus. **Do not connect a DFPlayer to GPIO16 or GPIO17.** Party v1 keeps
the accepted TMC link untouched and uses isolated UART1 pins instead:

| DFPlayer signal | Party v1 connection |
|---|---|
| RX | ESP32 GPIO32 (UART1 TX) through a **1 kOhm series resistor** |
| TX | ESP32 GPIO33 (UART1 RX), optional; firmware never waits for replies |
| VCC | regulated 5 V |
| GND | common ESP32/driver/LED ground |
| SPK_1 / SPK_2 | speaker directly, per DFPlayer rating |

The raw protocol driver is fire-and-forget and rate-limited to one command per
120 ms. Format a microSD card as FAT32 and copy `media/mp3` to `/mp3`, keeping
the filenames `0001.mp3` through `0006.mp3`. `VOL n` sets volume from 0 to 30.

### WS2812B stationary rim

| Signal | Connection |
|---|---|
| DIN | ESP32 GPIO13 (`PARTY_LED_DATA_PIN`) |
| 5 V | external regulated LED supply sized for the installed strip |
| GND | common with ESP32 and motor electronics |

Mount the strip on the stationary rim; no slip ring is required. The default
is 36 pixels (`PARTY_NUM_LEDS`). A 330-470 ohm data resistor, a bulk capacitor
at the strip input, and a 74AHCT-series 3.3-to-5 V level shifter are recommended.
Use `[` and `]` to nudge the persisted wedge-0 LED index and `\` to reverse the
persisted LED direction. FastAccelStepper is explicitly pinned to MCPWM/PCNT;
Adafruit NeoPixel therefore uses the ESP32 RMT output without sharing the
step-pulse peripheral.

### SoftAP and telnet

The ESP32 creates `PW-####` (the final two SoftAP MAC bytes), fixed channel 6,
with at most two clients. Change `PARTY_WIFI_PASSWORD` in
`prize_wheel_gpt/party_addons.h` before guest use; the committed value is only
a buildable placeholder. Connect a phone telnet client to `192.168.4.1:23`.
USB serial remains active and both transports feed the same command parser.
There is no OTA, web server, venue-WiFi mode, mDNS, or cloud dependency.

Useful build-time switches and pins are grouped at the top of
`party_addons.h`. `PARTY_WIFI_ENABLED`, `PARTY_FX_ENABLED`,
`PARTY_AUDIO_ENABLED`, and `PARTY_LED_ENABLED` default to 1 and can be set to 0
for isolation tests.

### Geometry

```
200 full steps x 16 microsteps x 2.0 gear ratio = 6400 motor microsteps / wheel rev
AS5600                                          = 4096 counts / wheel rev
12 wedges                                       = 30 deg each
dare_mask                                       = wedges 1 and 5 (0-indexed)
```

### Proven driver configuration

This block is bench-validated (motor warm, `cs_actual=25`, no cycling) and should
be reused verbatim:

```cpp
driver.I_scale_analog(false);   // internal Iref, NOT the Vref pot - critical
driver.en_spreadCycle(true);    // full torque, no RPM cap
driver.TCOOLTHRS(0);
driver.semin(0);
driver.semax(0);                // CoolStep off
driver.rms_current(1450, 1.0);  // IHOLD == IRUN
// R_SENSE = 0.11, microsteps = 16
```

## Firmware architecture

`prize_wheel/prize_wheel.ino` is a single-file sketch built around a mode
machine driven by a 1 kHz encoder sampler.

```
IDLE -> FREE_SPIN -> [PRECHARGE -> TAKEOVER] -> RECOVERY_HOLD -> DONE -> IDLE
                  \-------- no intervention --------/
```

| Mode | Role |
|---|---|
| `IDLE` | coils floating, waiting for a deliberate spin |
| `FREE_SPIN` | tracking the spin, predicting the natural stop |
| `PRECHARGE` | 80 ms at 100 mA to settle the motor's electrical phase |
| `TAKEOVER` | one finite hardware-timed braking move |
| `RECOVERY_HOLD` | soft hold while the wheel settles and the wedge is confirmed |
| `DONE` | landed wedge reported |

### 1. Sensing — the foundation

The original failure mode of this project was that angular velocity `omega`
became garbage during an active-motor takeover: sign oscillation and impossible
+/-4 rev/s spikes on a 0.9 kg wheel. Since every control decision keys off
`omega`, no control architecture could work on top of it.

The current sensing path fixes this with five specific measures:

- **Completion timestamps.** Each I2C read is timestamped immediately after the
  final received byte (`doneUs`), not at the start of the transaction. Velocity
  uses true completion-to-completion intervals, so a stalled loop can no longer
  inflate the derivative.
- **Scheduled 1 kHz sampling with no burst catch-up.** If the sampler falls
  behind, the schedule is reset forward rather than firing a rapid catch-up
  burst that would corrupt the timebase.
- **Long-gap re-prime.** If more than 20 ms passes without a good sample, the
  turn count cannot be chosen safely across that blind interval, so the encoder
  re-primes instead of guessing. This eliminates unwrap aliasing, which was the
  leading suspect for the apparent reversals.
- **Explicit alias and plausibility gates.** A delta of exactly 2048 counts
  (half a turn) has no unique signed interpretation and is rejected. Any delta
  exceeding `ENCODER_MAX_PLAUSIBLE_REV_S` (8 rev/s) scaled by the *actual*
  elapsed dt is rejected, and — importantly — the last known-good sample is left
  untouched so the next genuine sample is compared over its true longer interval.
- **Windowed velocity, not a per-sample derivative.** `omega` is the slope across
  a ~30 ms window of accepted samples (minimum 20 ms), then passed through an
  exponential filter whose alpha is computed from real elapsed time. I2C failures
  are never papered over with a stale angle; they explicitly invalidate velocity.

**Diagnostics.** Send `d` before a test spin to arm a high-rate RAM capture with
microsecond timestamps. The buffer is dumped only after the wheel reaches `DONE`,
so serial output cannot perturb the measurement it is measuring. Each sample
records dt, raw value, raw diff, unwrapped delta, I2C duration, omega, mode, and
a flag byte (`VALID`, `PRIMED`, `LONG_GAP`, `ALIAS`, `RATE`, `TX_ERROR`,
`SHORT_READ`, `DIR_FLIP`).

### 2. Decision

`predictStopAngle()` extrapolates the natural stop from current angle and speed
using a per-direction friction model (`cw_c`/`cw_b`, `ccw_c`/`ccw_b`). If the
predicted stop falls in or within `PREDICTION_DARE_MARGIN_DEG` of a dare, the
firmware looks for a safe target.

`chooseRandomSafeTargetAngle()` picks a random interior point of a safe wedge
that is reachable in the current direction, respecting a minimum runway
(`requiredTakeoverRunwayDeg()`), a maximum runway of 210 deg, an 8 deg edge
margin, and 3 deg of jitter so repeated interventions do not cluster on the same
landing spot.

### 3. Takeover

The prior architecture re-issued `runForward()` / `setSpeedInHz()` /
`setCurrentPosition()` every loop iteration, which restarted the FastAccelStepper
motion planner constantly and made the motor lurch. The current design issues
**one** finite, hardware-timed, calibrated relative move and then leaves the
planner alone:

- **Direction is measured, not assumed.** The `p` command runs an attended 9 deg
  probe that measures the physical relationship between FastAccelStepper's
  positive direction and the encoder's, and persists it. The high-speed path is
  locked out until this probe has run — the firmware never infers direction from
  `INVERT_DIR`.
- **Staged current.** 100 mA precharge to settle the phase, then a modest 300 mA
  braking current only after STEP pulses have established motion. Full 1.45 A is
  never restored mid-spin.
- **Speed-matched entry.** The move starts at 90% of measured wheel speed via
  `setJumpStart()`, so the motor picks the wheel up from behind rather than
  arriving from zero after the wheel has nearly stopped.
- **The encoder gates braking, not the planner's virtual coordinate.** Braking
  begins when measured travel reaches the target minus `stepsToStop()` plus an
  8 deg interior buffer.
- **Bounded aborts.** Sustained reverse motion or a genuine wheel speed-up ends
  the takeover cleanly rather than fighting it. A shortfall is never chased with
  a second move from rest, which would look like the wheel accelerating after
  its coast had ended.

### 4. Landing

Rather than freewheeling at the nominal endpoint — which previously allowed the
wheel to coast off a safe target into a dare — the firmware holds softly at
650 mA, waits for `|omega|` to stay below `STILL_REV_S` for a 500 ms settle
window, then samples the settled angle and reports the landed wedge. A dare
recovery backstop can nudge the wheel to safety if the settled wedge is wrong.

## Serial commands

115200 baud.

| Key | Action |
|---|---|
| `z` | set current pointer position as the wedge-0 boundary |
| `p` | attended 9 deg direction probe (run on a safe wedge center) |
| `s` | print angle / wedge / velocity / sensor health |
| `d` | arm high-rate RAM encoder capture, auto-dumps after a true stop |
| `v` | toggle live takeover logging |
| `m` | print the dare mask |
| `?` | help |

## Build

```
arduino-cli compile --fqbn esp32:esp32:esp32 prize_wheel
```

- ESP32 Arduino core **3.3.10**
- Libraries: **TMCStepper**, **FastAccelStepper** (v1.2.7 known good), **Wire**
- Build from a **local** folder. `cmd.exe` rejects UNC working directories
  ("UNC paths are not supported") and the toolchain fails.
- The sketch folder name must match the `.ino` filename.

## Bring-up procedure

1. Flash, open serial at 115200.
2. Rotate the wheel so the pointer sits exactly on a wedge boundary, send `z`.
3. Position the wheel on a safe wedge center and send `p` to calibrate motor
   direction. The takeover path stays locked until this succeeds.
4. Send `d`, hand-spin, and review the dumped capture. Confirm the sample
   interval is stable, raw angle is monotonic through a one-direction spin, and
   no `ALIAS` / `RATE` flags appear at peak speed.
5. Send `v` and hand-spin **both directions**, aimed so the natural stop would be
   wedge 1 or 5. Confirm it steers away in both directions, lands on a safe
   wedge, never reverses, the motion reads as a natural slowdown, and the
   reported `LANDED` wedge matches a manual check.

## Key tuning constants

| Constant | Value | Meaning |
|---|---|---|
| `ACCEL_CEILING_SPS2` | 650 | conservative accel ceiling; skipping appears above this |
| `TAKEOVER_REV_S` | 0.26 | intercept speed — above this there is still real runway |
| `TAKEOVER_MATCH_FRACTION` | 0.90 | motor trails the wheel; brake, never lead |
| `TAKEOVER_PRECHARGE_CURRENT_MA` | 100 | phase-settling current |
| `TAKEOVER_BRAKE_CURRENT_MA` | 300 | braking current once motion is established |
| `TAKEOVER_MAX_RUNWAY_DEG` | 210 | longest intervention allowed |
| `SAFE_WEDGE_EDGE_MARGIN_DEG` | 8 | keeps targets off wedge boundaries |
| `ENCODER_MAX_PLAUSIBLE_REV_S` | 8.0 | rate gate for rejecting impossible deltas |
| `VELOCITY_WINDOW_US` | 30000 | velocity slope window |

## Open issues

- **Takeover is still perceptible.** The engagement is smoother than earlier
  revisions but not invisible. The levers are `TAKEOVER_MATCH_FRACTION`,
  `TAKEOVER_BRAKE_CURRENT_MA`, and the precharge duration.
- **Landing distribution is uneven.** In testing, landings cluster and wedges
  11, 0, 2, 3, 4 are rarely or never selected. This is a consequence of the
  runway constraints in `chooseRandomSafeTargetAngle()`: the minimum-runway
  requirement biases target selection toward wedges further ahead of the current
  position, so safe wedges immediately following a dare are structurally hard to
  reach. Worth addressing — an uneven distribution is its own tell.
- **Friction model is seeded, not calibrated.** `cw_c`/`cw_b` and `ccw_c`/`ccw_b`
  are placeholder values and have not been fitted to this wheel, which limits
  prediction accuracy and therefore intervention timing.
- **Max clean acceleration is not precisely characterized.** 650 steps/s^2 is a
  conservative attended-test value, not a measured ceiling.

## Repository layout

```
prize_wheel/prize_wheel.ino        current firmware
HANDOFF_README.md                  original handoff notes
BRIEF.md                           full spec, hardware notes, bug priorities
CODEX_TASK2.md                     follow-up task notes
debug_log_omega_garbage.txt        evidence of the original velocity failure
prize_wheel_CURRENT.ino            superseded unstable revision, kept for reference
REFERENCE_uart_current_WORKS.ino   proven-good driver config and motor control
REFERENCE_as5600_test_WORKS.ino    proven-good AS5600 read at rest and low speed
```
