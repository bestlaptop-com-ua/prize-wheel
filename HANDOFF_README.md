# Prize Wheel Firmware Handoff

## What this is
A hidden ESP32 secretly steers a hand-spun 24" prize wheel so it never lands on two
"dare" wedges (1 and 5), while looking completely fair. All hardware works; the
TAKEOVER CONTROL is unstable. Full details in BRIEF.md.

## Files
- BRIEF.md                         <- READ FIRST. Full spec, hardware, bugs, priorities.
- prize_wheel_CURRENT.ino          <- the sketch to diagnose/rewrite (latest, unstable)
- debug_log_omega_garbage.txt      <- KEY EVIDENCE: velocity signal is garbage during
                                      takeover (impossible omega values). Priority 1.
- REFERENCE_uart_current_WORKS.ino <- proven-good driver config + motor control. The
                                      driverConfig / current / SpreadCycle setup here
                                      is known-correct; reuse it verbatim.
- REFERENCE_as5600_test_WORKS.ino  <- proven-good AS5600 read that works AT REST and
                                      low speed. Compare its I2C read against the
                                      takeover read to spot what breaks under motor load.

## The one thing to internalize
The wheel is NOT torque-limited and the encoder is accurate AT REST. The failure is
that the angular VELOCITY estimate becomes garbage during an active-motor takeover
(sign flips, +/-4 rev/s spikes - impossible). Fix the SENSING first; the control gets
simple once omega is trustworthy. Do not rebuild control logic on top of a broken
velocity signal.

## Build / flash
- Arduino IDE OR arduino-cli. Board: ESP32 core 3.3.10, FQBN esp32:esp32:esp32.
- Libraries: TMCStepper, FastAccelStepper (v1.2.7 works), Wire.
- IMPORTANT: build/open from a LOCAL folder, NOT a UNC/network path. cmd.exe rejects
  UNC working dirs ("UNC paths are not supported") and the toolchain fails.
- Sketch folder name must match the .ino filename.

## Test protocol
Calibrate (`z` at a wedge boundary) -> enable debug -> hand-spin BOTH directions aimed
at wedges 1 and 5 -> confirm it steers away both directions, lands safe, never
reverses, motion looks natural, and LANDED matches a manual angle check.
