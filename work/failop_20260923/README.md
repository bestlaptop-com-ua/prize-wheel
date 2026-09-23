# failop_20260923 - stage 1a: the motor never lets a moving wheel go

Base: work/forcestop_guard_20260922 (live since 2026-09-22 22:48, commit 95694cf).
Plan and reasoning: FAILURE_MODES_2026-09-23.md, stage 1a (closes A1-A3, A7, B1-B5, F1-F3).
Stages 1b (margins), 1c (slip re-lock + re-plan, weak-spin carry) and 2 (hall index) are NOT in here.

## What changed

Fail-operational policy (P1)
- No monitor switches the driver off while the motor steers or holds the wheel
  (capture, braking, settle, hold, dare recovery). enterFault() in those states becomes an
  `ANOMALY` log line and control continues.
- Speed-up (rotor hunting): logged, braking continues (was: fault + free coast, 3 dare landings on 9/22).
- Driver SPI health failure while powered: logged; a driver reset (GSTAT reset bit, also with uv_cp)
  gets its configuration and the stage current re-applied on the spot. STEP/DIR keep driving.
- Encoder stale during braking: the stop continues open-loop from the STEP count since the last good
  encoder tick. Stale during settle: the wheel stays held; after 3 s under a static field it is released
  at rest and ENCODER_STALE latches (clears itself when the encoder is back).
- Fight (wheel far below field speed) or reversal (wheel moving backward): the pulse train is frozen
  (guarded forceStop) and the energized field holds the wheel; settle -> verdict -> recovery as usual.
  Before, both released the wheel.
- The instantaneous 0.80 rev/s CONTROL_OVERSPEED fault is gone, so the debounced guest re-spin path is
  reachable again (AUDIT s6): a re-spin mid-control floats the coils while the hand drives, starts a new
  spin and is captured again. This, SETTLE-DRAG and a push on the hold are the only releases.
- Position implausible, 30 s control timeout, monotonic violation, setSpeedInHz rejection: logged; the
  stop is finished (ramp stop) instead of releasing.
- Landing on a dare or within 2 deg of one: dare recovery retries up to 4 times, alternating sides,
  instead of latching LANDING_UNSAFE after 2. Within 2 deg of a dare edge but in a safe wedge, it now
  steps to the centre of that same wedge (a few degrees) instead of a whole wedge over.
  If all attempts fail it holds, logs `DARE_RECOVERY gave up`, and the next spin is still steered.

No lock-out (A1, A2, A3, F1, F2)
- S1 (fault latch persisted in NVS) is off. The remaining latched faults (encoder outage while the motor
  is off, encoder lost at rest) auto-clear after 3 s of rest with valid feedback. Only a failed direction
  probe and a missing STEP timer/clock still need attention.
- No capture budget: a capture that fails before torque-on (nothing energized) is retried every 60 ms
  while the wheel moves, including right after boot, after a fault clear and after a guest re-spin.
  CAPTURE_ABANDON lines are rate-limited (first one, then one per 500 ms with a count).
- Boot before 24 V: no TMC_UART fault. `# driver not ready at boot: steering waits for it`; the driver is
  re-checked once a second at rest and configured + verified the moment it answers.
- S2 config mismatch at rest: re-applied and verified at once instead of latching TMC_UART.

Loop timing (F3, A7)
- Friction fits are persisted only after 1 s at rest (IDLE or hold); the flash write during the spin
  was what blinded the encoder and cost spins 5 and 13 on 9/23 their capture.
- Serial has an 8 KB TX buffer (prints no longer block the loop).
- The `d` RAM dump streams a few lines per pass while 3 KB of TX buffer stay free (was ~12 s blocked).

Visibility (P8)
- At rest the LEDs show the dim steady amber fault look whenever the next spin could not be steered
  (takeover off, driver not answering, no direction calibration, encoder stale, latched fault).

Telemetry
- SPIN SUMMARY gains `attempts= anomalies= syncErr= frozen=` (syncErr = max |encoder - STEP| travel since
  torque-on in degrees; one full step is 1.8, the pickup snap alone up to 3.6). `fault=` now shows the
  first anomaly of the spin.

## Build

Local pre-check (Linux, arduino-cli 1.3.1, esp32 3.3.10, FastAccelStepper 1.2.7, TMCStepper 0.7.3,
FastLED 3.10.5): exit 0, 1695395 bytes, RAM 119776 bytes, no new warnings (97, all in core/libraries,
same set as the live build). The flashed binary is compiled on MILL-PC with compile_candidate.py.

Rollback: work/forcestop_guard_20260922/upload_candidate.py (binaries on MILL-PC, not in git).

## Test plan (Timur at the wheel, monitor running)

Normal play first: 30 spins, both directions, weak to strong. Expect no FAULT lines, ANOMALY rare,
`attempts=1` on most spins, safe landings as before.

Then fault injection, one at a time, each during a controlled stop (after `PULSEFIRST TORQUE_ON`):
1. Hand drag: rest a hand on the rim until it stops. Expect `ANOMALY code=MOTOR_FIGHT ... field frozen`,
   no FAULT, wheel held, verdict, recovery if it stopped on a dare. Next spin steered.
2. Pull back briefly against the direction of travel. Expect UNEXPECTED_REVERSAL anomaly, freeze, hold.
3. Re-spin mid-stop. Expect `GUEST-RESPIN detected mid-control`, a new SPIN#, and a new capture.
4. Encoder: unplug the AS5600 connector for ~1 s during braking, then plug back. Expect ENCODER_STALE
   anomaly, the stop finishes open-loop, verdict after the encoder returns.
5. 24 V: switch the motor supply off while the wheel rests in the hold, wait 10 s, on. Expect
   `driver unhealthy for 1 s while powered`, LEDs amber, then `driver reset flag seen ... config
   re-applied` and `driver healthy again` (from IDLE instead: `driver answered ... steering available`).
6. Boot order: ESP powered (USB) first, 24 V later. Same as 5, no FAULT.
7. Weak push onto a dare and let it stop there by itself. Expect DARE_RECOVERY (free coast).
8. `d` then a spin: the dump streams afterwards and a spin during the dump is still captured.
