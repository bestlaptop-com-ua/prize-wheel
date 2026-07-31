# Prize Wheel firmware — project requirements v1

Code of record: `prize_wheel_gpt/prize_wheel_gpt.ino`
Base: `main` at branch creation.

## Required behavior

1. Preserve the verified label-true absolute frame from `HANDOFF_CHATGPT_FRAME.md`:
   `wheelAngleDeg = ((rawZero - AS5600_raw) mod 4096) * 360 / 4096`.
2. `rawZero` is the sole wedge anchor and may change only through the owner-attended `z` command.
3. Both physical spin directions are valid and must retain correct wedge identity through resets, re-primes, I2C gaps, and reboot during motion.
4. Every confirmed released hand spin is eligible for mid-spin steering; prediction may be logged but must not gate engagement.
5. The firmware selects only safe wedges. Wedges 1 and 5 are DARE and must never be selected as targets.
6. Steering is brake-only and must not add a powered extra revolution or visibly accelerate the wheel.
7. Target selection must remain inside physically reachable braking runway.
8. No post-stop recovery motion. The wheel must finish the original spin without a visible correction after stopping.
9. Hand-release protection remains active before motor takeover.
10. Guest override, opposite-motion abort, speed-up abort, fight watchdog, encoder freshness checks, and direction calibration interlocks remain active.
11. Motor control must release safely whenever encoder validity or takeover assumptions fail.
12. High-rate diagnostics remain available without Serial output disturbing active sensing.

## Hardware/build target

- ESP32-WROOM-32
- BTT TMC2209 V1.3
- NEMA17
- 2:1 GT2 belt drive
- AS5600 absolute encoder
- ESP32 Arduino core 3.3.10
- Libraries: TMCStepper, FastAccelStepper, Wire, Preferences

## Acceptance test

1. Verify the five frame tests in `HANDOFF_CHATGPT_FRAME.md`.
2. Run at least ten spins in each direction, including weak, normal, and strong hand spins.
3. Confirm every engaged spin targets a non-DARE wedge.
4. Confirm there is no powered extra revolution, obvious acceleration, or post-stop correction.
5. Confirm hand contact or a renewed push aborts/relinquishes control safely.
6. Record predicted wedge, selected target, final wedge, target error, engagement speed, and abort reason for each test.
7. Do not tune the solved frame to correct landing scatter; tune synchronization/braking only.

## Known tuning item

Landing scatter is approximately one wedge in the current hardware-tested lineage. Improvements must preserve every invariant above and must be validated on hardware before merging to `main`.
