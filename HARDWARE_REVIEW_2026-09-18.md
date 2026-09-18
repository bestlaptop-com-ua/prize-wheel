# Hardware and capture review — 2026-09-18

## Decision and scope

Keep the NEMA23 / TMC5160T Pro / direct-drive assembly as the diagnostic
baseline. There is not enough evidence to conclude that the motor cannot
handle this wheel. The NEMA34/DM860T already owned by Timur is a credible
fallback, and a belt reduction is another option; neither is a proven fix.
This is a documentation review, not a firmware change or hardware validation.

Reviewed baseline: `2dbc15e`, branch `new-hardware-evaluation`. The production
PC contains changes not committed here, including fault-clearing behavior.
The historical WROOM screenshots must not be treated as traces of this S3
build. No production PC connector was exposed in this review session, and
no firmware was read from or flashed to the controller.

Requirements remain: a simple, transportable wheel with natural hand-spin
feel, subtle capture/braking, no visible reverse or post-stop correction,
soft hold followed by release, and reliable local control. Internet access,
logging, remote updates, audio, and LED effects must not interrupt motion
control. Reliability is an acceptance-test requirement, not a guarantee that
hardware/software cannot fail. Do not mask genuine faults to improve counts.

## Torque, weight, and imbalance

The separate shaft and two bearings carry the wheel's weight. Motor torque
must handle rotational inertia, drag, gravity from imbalance, and engagement
transients. Holding torque does not specify allowable shaft/bearing load;
do not mount the entire disc on the motor shaft based on torque alone.

For a uniform stop from 0.55 rev/s over 4 s:

`alpha = 2*pi*0.55/4 = 0.864 rad/s^2`

| Assumed inertia | Net inertial braking torque |
| --- | --- |
| 0.6 kg*m^2 | 0.52 N*m |
| 0.7 kg*m^2 | 0.60 N*m |
| 0.9 kg*m^2 | 0.78 N*m |

These are illustrative estimates, not measured capture requirements. The
1.9 N*m NEMA23 rating is holding torque at its rated excitation. The actual
running/braking torque depends on current, speed, supply and synchronism.
The comparison neither proves sufficient operating margin nor establishes
that a larger motor is necessary. Friction generally assists braking but
opposes driving; gravity can assist or oppose rotation depending on angle.

At a 457 mm rim radius, 20/50/100/200 g imbalance contributes approximately
0.09/0.22/0.45/0.90 N*m peak gravitational torque. If a 6 kg disc's mass
center is offset 2.12 mm from the shaft, that alone contributes about
0.125 N*m, equivalent to about 28 g at the rim. This is conditional on the
actual mass-center offset, not a measurement of the assembled wheel.

Balance with the motor mechanically disconnected and the flapper lifted.
At a repeatable rest position the heavy side is physically at the bottom;
add securely attached trial weight at the top. Recheck from multiple angles.
Do not locate weights using encoder angle alone. Counterweights correct
mass imbalance but do not remove geometric rim eccentricity. A stronger
motor cannot prevent gravitational roll after its outputs are disabled.

## What the fault actually tells us

In `prize_wheel_gpt/prize_wheel_gpt.ino`, `fasWheelRevS()` derives speed from
FastAccelStepper's pulse/ramp state, not measured rotor motion. After a
150 ms capture grace period, `MOTOR_FIGHT` checks whether commanded pulse
speed exceeds 0.06 rev/s while measured forward wheel speed remains below
45% of that speed for the 150 ms confirmation interval.

This is a speed-mismatch detector, not proof of torque deficiency, friction,
or a guest fighting the motor. It does not detect the wheel overtaking the
commanded motor speed. Candidate causes include binding, imbalance, capture
transients, step loss, encoder errors, and pulse-ramp tracking behavior.

Timur reports no rattle during braking. That weakens sustained loss of
synchronism as an explanation, but brief slip or early disable may be quiet.
The reported successful open-loop bench cycles support basic capability;
they do not reproduce engagement of a freely spinning rotor. Matching
mechanical speed does not establish electrical phase alignment. Review the
precharge interval, enable transition and low-current pickup as well as the
subsequent deceleration. A larger motor could amplify an engagement jerk.

Current stages in the reviewed committed source, in mA requested RMS:

| Precharge | Capture | Brake | Taper | Hold 1 | Hold 2 |
| --- | --- | --- | --- | --- | --- |
| 350 | 2200 | 1650 | 1100 | 550 | 300 |

The reported 2000 mA bench cycle is not equivalent to the 1650 mA braking
or 1100 mA taper stage. Record actual production values before comparing.

The friction model `c + b*omega` omits angle-dependent gravitational torque.
That is a plausible contributor, not an established sole cause. The code
also reads learned NVS values and then overwrites them with c=0.55, b=0.28
on every boot. Use a versioned one-time migration or explicit calibration
reset so subsequent learned values survive restart.

## TMC current ceiling correction

The original report's 2.385 A hard-ceiling claim is not supported by IRUN=31.
TMC5160 current also depends on GLOBALSCALER. In the reviewed upstream
TMCStepper implementation, TMC5160 delegates current calculation to the
TMC2160 implementation, which uses both registers:

`Irms = (GLOBALSCALER/256) * ((IRUN+1)/32) * (0.325/Rsense) / sqrt(2)`

GLOBALSCALER=0 represents full scale. With Rsense=0.075 ohm, the nominal
full-scale result is about 3.06 A RMS; BTT advertises approximately 3.1 A RMS.
This is not authorization to set a motor marked 3 A to 3.1 A RMS. Confirm
motor rating convention, installed library version, register readback and
thermal conditions. A library getter is not a physical current measurement.
The single reported SPI dropout also remains unresolved.

## Belt option: more than a ratio change

A 3:1 ratio ideally triples torque at the wheel and divides load inertia
reflected to the motor by nine, but triples motor speed for a given wheel
speed. Actual available torque, losses, free-spin feel and mounting stiffness
need evaluation. The earlier approximate 4.5 N*m output was not measured.

The encoder currently measures the motor rear shaft; multiple functions
assume 4096 counts per wheel revolution. `GEAR_RATIO` changes pulse scaling,
not those encoder conversions. Prefer moving the encoder to the wheel shaft.
Otherwise rewrite scaling/unwrapping and provide a startup reference: a
single-turn motor reading at 3:1 cannot distinguish three wheel positions
120 degrees apart on power-up.

Review step-based acceleration/deceleration and speed limits, direction
probe travel, pulse generation capacity, and tuning. For example, unchanged
650 steps/s^2 means 0.203 wheel rev/s^2 at 3200 steps/rev direct drive but
0.0677 at 3:1. At 0.68 wheel rev/s, 3:1 requires 6528 pulses/s. Recalibrate
zero and friction and validate both directions. Pulley/belt sizing and
bore compatibility remain design work, not a ready-to-install kit.

## Confirmed spare motor and pictured driver

The supplied Amazon webarchive identifies STEPPERONLINE `34HS31-5504S`.
Manufacturer specifications confirm:

| Property | Value |
| --- | --- |
| Motor | NEMA34, bipolar, four wires, 1.8 degrees/step |
| Holding torque | 4.5 N*m |
| Rated phase current | 5.5 A |
| Phase resistance / inductance | 0.4 ohm / 3.5 mH |
| Body / mass | 86 x 86 x 80 mm / 2.3 kg |
| Shaft | SINGLE shaft, 14 mm keyed, 35 mm long |

This has about 2.37 times the NEMA23's rated holding torque, not a verified
2.37 times braking margin. It cannot take the planned rear-shaft encoder.
Use an accessible wheel-shaft end for the AS5600; confirm physical access
before selecting this layout. Retain the separate shaft/bearings, use a
compatible 14 mm motor-side coupling hub, and a stiff NEMA34 bracket.

The supplied DM860T listing picture differs from the older 2017 manual:

| Visible label feature | Consequence |
| --- | --- |
| 18-80 V AC or 24-110 V DC | 24 V is within the pictured revision's range; verify actual unit matches |
| 5 V / 24 V signal selector | Does not establish direct ESP32 3.3 V compatibility; use an appropriate interface |
| ALM+/ALM- | Alarm output available; verify electrical interface and fault coverage, not an encoder or guaranteed step-loss detector |
| BRK+/BRK- | Present; function must be verified before use, not assumed to be regenerative braking terminals |
| SW9 OFF | PUL/DIR mode |
| SW10 OFF | Smoothing disabled; initial diagnostic setting |
| SW5 OFF, SW6 OFF, SW7 ON, SW8 ON | 3200 pulses/revolution for the 1.8-degree motor |
| SW4 OFF | Half idle current; fixed reduction does not reproduce the TMC current fade |

The earlier 36 V DC minimum advice came from the older manual and does not
apply to the photographed 24 V revision if the physical unit matches.
The motor's 2.2 V listing is winding voltage (5.5 A x 0.4 ohm), not required
chopper-driver supply voltage. Start assessment at the existing 24 V;
higher-voltage operation remains an option after current and capture checks.
Never retain the existing 35 V capacitor on a 48 V motor bus or connect
24 V audio equipment to that higher-voltage rail.

The pictured current table labels its columns Peak and REF, not RMS. Do not
assume their equivalence or apply an older revision's current conversion
blindly. The discussed 4.45 A peak setting (SW1 OFF, SW2 OFF, SW3 ON) is a
conservative reduced-current commissioning point, not full-rated motor
performance. Obtain the matching revision documentation before final current
selection. The 6.52 A peak / 5.43 A REF row alone does not prove a correct
5.5 A motor setting.

DM860T integration needs a separate driver path: STEP/DIR/enable interface,
verified enable polarity and startup behavior, pulse timing, alarm handling,
and replacement of TMC SPI/current-stage operations. Hardware idle reduction
cannot provide the current six-stage fade. Check free-spin feel when disabled
and capture behavior; do not assume absence of StealthChop makes it too noisy.

## Next-session evidence and acceptance

1. Preserve production source, build identity, dependency versions and NVS
   settings before changes. Distinguish original WROOM events from S3 tests.
2. Check mechanical freedom, static balance, magnet health and SPI stability.
3. Log timestamp, angle, measured speed, pulse/ramp speed, requested target
   speed, controller state, current stage, enable state and fault reason,
   including the period before capture and before fault shutdown. Buffer
   locally; network logging must not delay motion service.
4. Compare controlled starts from rest against hand-spin captures, both
   directions and several wheel angles, using real brake/taper settings.
5. Validate restarting during soft hold, disabled-driver startup, natural
   release and disturbance handling. Explicitly distinguish intended disable
   from an unavailable driver; do not indiscriminately bypass protection.
6. Preserve learned friction values across a reboot, then repeat the same
   representative trials. Judge landing and hand feel as well as fault count.
7. Only then choose whether control fixes/balance suffice or hardware needs
   more torque. If trying NEMA34, repeat the same trials for comparison.

## Sources and evidence boundaries

- Repository source at `2dbc15e` and the original bring-up report. Bench
  sketches `wheel_v2_run.ino` / `wheel_v2_cycle.ino` and raw fault traces were
  not present in the reviewed branch.
- Owner's observations and supplied DM860T image / motor webarchive.
- [Motor specifications](https://www.omc-stepperonline.com/nema-34-cnc-stepper-motor-4-5nm-637-25oz-in-5-5a-86x86x80mm-key-way-shaft-34hs31-5504s).
- [BTT TMC5160T Pro specifications](https://global.bttwiki.com/TMC5160T%20Pro%20V1.0.html).
- [TMCStepper current calculation](https://github.com/teemuatlut/TMCStepper/blob/74e8e68/src/source/TMC2160Stepper.cpp) (reviewed upstream revision; production library not verified).
- [Older DM860T manual](https://www.omc-stepperonline.com/download/DM860T.pdf), version 1.0 / 2017: useful only where confirmed against the actual revision.
