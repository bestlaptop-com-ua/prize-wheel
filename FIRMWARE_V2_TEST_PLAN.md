# Firmware v2 bench test plan

This branch replaces the one-shot position takeover with a continuous same-direction electronic-drag controller. It is not production-ready until the attended tests below pass.

## 1. Build

```text
arduino-cli compile --fqbn esp32:esp32:esp32 prize_wheel
```

Target environment:

- ESP32 Arduino core 3.3.10
- TMCStepper
- FastAccelStepper 1.2.7

## 2. Calibration

1. Open serial at 115200 baud.
2. Confirm `TMC test_connection=0`.
3. Put the pointer exactly on the wedge-0 boundary and send `z`.
4. Move the pointer to the center of safe wedge 3 or 7-11 and send `p`.
5. Send `s`; confirm `zero=1`, `dir` is `+1` or `-1`, and `tmc=1`.
6. Reboot and send `s` again to verify both calibrations survived.

Automatic control remains locked unless the encoder, TMC UART, wedge zero, and motor direction are all valid.

## 3. Observation-only predictor run

Send `o` so `observeOnly=1`. Perform at least 25 clockwise and 25 counter-clockwise natural spins at varied strengths.

Capture the `PRED`, `DECISION`, `PRIMARY_STOP`, and `LANDED` lines. In observation mode the motor and emergency recovery remain inactive, including when the wheel naturally lands on a dare.

Use the records to compare prediction error by direction and speed threshold before changing the friction constants or the 8-degree uncertainty margin.

## 4. First powered tests

Send `o` again so `observeOnly=0`. Keep a hand near the power switch.

1. Test only one direction at first.
2. Aim for several predicted dare landings.
3. Verify `DRAG_START` direction matches the original spin.
4. Verify there is no visible acceleration and no reversal.
5. Verify current rises only from 100 mA precharge to 180 mA entry and 300 mA drag.
6. Verify the controller chooses the nearest reachable forward safe wedge center.
7. Repeat in the opposite direction.

Stop testing on any `FAULT`, `opposite-motion`, repeated `wheel-speed-up`, or obvious step skipping.

## 5. Acceptance run

Run at least 100 spins split across both directions and weak/strong throws.

Required:

- no commanded reversal;
- no skipped-step symptoms;
- all untouched safe predictions remain completely freewheel;
- `PRIMARY_STOP source=DRAG` is safe for every successful intervention;
- emergency recovery is rare and always explicitly logged;
- final wedge agrees with a manual pointer check;
- holding torque releases automatically after 350 ms or immediately when a guest touches the wheel.

The seeded friction model is still uncalibrated. Do not tune current or drag fractions to compensate for systematic prediction error; fit the directional prediction model from observation data first.
