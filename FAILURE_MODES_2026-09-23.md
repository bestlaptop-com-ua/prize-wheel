# Failure modes - every way a spin can end on a dare

2026-09-23. Scope: the live firmware (work/forcestop_guard_20260922 = the 9/22 party build + the forceStop
guard) on today's hardware: NEMA23 + TMC5160 in STEP/DIR mode, AS5600 on the motor's rear shaft, 36 in wheel
driven through a jaw coupling and an 8 mm adapter. Evidence: monitor.log 9/22-9/23 and AUDIT_2026-09-22.md.

## Requirement (Timur, 2026-09-23)

Every spin ends at rest on a safe wedge (labels 1, 2, 4-7, 9-12, 14, 15, 17, 18). Once the motor has the
wheel it never lets go until the wheel is at rest on a safe wedge. The slowdown still has to look natural.

## Where the firmware stands

Three design choices make the requirement impossible today:

1. **It is built to let go.** The header states the policy: "An infeasible plan remains a free coast."
   Every fault switches the driver off first (enterFault -> driverFreewheel), and 10 capture-abandon points
   do the same before torque-on. After any of them the spin coasts wherever friction takes it, and the
   one-attempt capture budget forbids a second try in the same spin. The appendix lists every release point.
2. **The brake is blind.** The whole stop is committed on the first 25 ms control tick as an open-loop step
   ramp (audit section 2). If the wheel stops following the motor, nothing notices and nothing corrects:
   spin 3 today ended 152 deg short of its target.
3. **Nothing knows where the wheel really is.** The encoder sits on the motor shaft. A slip between motor and
   wheel (the coupling slipped on 9/21, the map moved 38 deg on 9/22) turns every "safe" target into a
   guess, and the firmware cannot detect it.

Record 9/22-9/23: 14 spins came to rest on a dare, every one in a spin that was not under control at the
end (10 with steering locked by a fault, 3 whose stop was aborted by a speed-up fault, 1 moved by hand).
In 10 more spins dare recovery had to move the wheel off a dare (12 attempts, one timed out). Today
(23 spins): 1 slip (spin 3), 2 spins never captured (5 and 13), no dare landings.

## Failure modes

Seen = in the 9/22-9/23 logs (line numbers in monitor.log). Unless noted, every row ends with the wheel
wherever friction leaves it. P-numbers refer to the design principles below.

### A. The motor never takes the spin

| ID | Scenario | Mechanism | Seen | Closed by |
|---|---|---|---|---|
| A1 | Steering locked by an earlier fault | Every fault is saved to flash and blocks control until `r`; later spins run free | 38 spins, 10 of the dare landings | P1, P8 |
| A2 | No capture budget left | pw_capture_cycle.h allows one attempt per 1 s rest in freewheel. A spin from the hold after a recovery, a spin right after a reboot or a fault clear, and any second attempt in the same spin get nothing | 6290, 2541, 6380 | P3 |
| A3 | Capture abandoned before torque-on | launchCapture / serviceCaptureArming abandon when: encoder speed is invalid (flash save stall, today spins 5 and 13), speed above 0.40 (fixed 9/22), plan refused (shadow margin, friction model), pulse check fails (the DEADLINE bug fixed 9/22; PULSE_RATE, WHEEL_RATE remain), lease/deadline | 22 abandons: 12 from the fixed bug, 10 other | P3 |
| A4 | No reachable safe target | tryReserveTarget finds nothing on every tick: passes 1-4 empty, runway < 8 deg, and the brake-only rule never aims past the natural stop | NO_REACHABLE results | P4 |
| A5 | Slow push not seen as a spin | Speed stays under 0.12 rev/s for 700 ms -> MANUAL_ADJUSTMENT: no control and no dare check when it stops | 9 manual moves, one of 116 deg | P3, P7 |
| A6 | Steering switched off | takeover off (`e`), direction calibration missing, driver check failed at boot -> controlAvailable() false | configuration | P8 |
| A7 | Firmware busy | the `d` dump blocks the loop for about 12 s; boot and encoder priming | - | P9 |

### B. The motor lets go mid-spin

| ID | Scenario | Mechanism | Seen | Closed by |
|---|---|---|---|---|
| B1 | Speed-up alarm | SUSTAINED_SPEEDUP trips on speed hunting of the wheel on the motor's magnetic spring | 4 trips; 3 aborted stops ended on dares (4943, 5020, 5070) | P1, P2 |
| B2 | Driver health read fails | TMC_UART check every 100 ms in control, settle and hold: one bad SPI read after 2 retries, otpw warning, or a GSTAT reset | 2493 (in hold) | P1, P9 |
| B3 | Encoder hiccup | I2C failure for 50 ms (ENCODER_STALE) or speed invalid for 300 ms (ENCODER_VELOCITY), e.g. after a loop stall over 20 ms | - | P1, P9 |
| B4 | Guest touches or re-spins | CONTROL_OVERSPEED latches first (the guest re-spin path is unreachable, audit section 6); MOTOR_FIGHT, UNEXPECTED_REVERSAL and SETTLE-DRAG release | 5013 (certified_v2 test) | P1, P3 |
| B5 | Plausibility checks and timers | POSITION_IMPOSSIBLE, TAKEOVER_TIMEOUT (30 s, settle 20 s), MONOTONIC_VIOLATION, STEPPER_API | - | P1 |

### C. The motor holds on, but the wheel doesn't follow

| ID | Scenario | Mechanism | Seen | Closed by |
|---|---|---|---|---|
| C1 | Step sync lost | Pickup at an arbitrary phase (snap up to +/-3.6 deg) with the wheel up to 5% faster than the field (0.38 rev/s cap). The open-loop ramp runs on; the wheel chatters and coasts | spin 3: 152 deg short; 1 of 21 captures today | P2, P6 |
| C2 | Drivetrain slip | Coupling, 8 mm adapter or encoder magnet turns relative to the wheel. The encoder follows the motor, so the firmware believes it landed safe | coupling slip 9/21; map moved 38 deg 9/22 | P5 |
| C3 | Motor torque drops | 24 V dip: charge-pump undervoltage switches the outputs off briefly without a reset (not caught); a full reset loses the 16-microstep setting | GSTAT=05 seen 9/21 | P2, P9 |
| C4 | Plan asks more than the motor has | Pass-3/4 stops brake at up to 600 sps2 (about 3x natural); higher demand, higher slip risk | 3 such stops 9/22 morning | P2, P4 |

### D. The motor follows the plan, but the plan lands wrong

| ID | Scenario | Mechanism | Seen | Closed by |
|---|---|---|---|---|
| D1 | Wrong wedge map | rawZero drift from C2, a mis-zero, or AS5600 non-linearity (up to +/-1.9 deg, uncorrected) | 38 deg shift 9/22 | P5 |
| D2 | Overshoot | The stop is planned with zero margin, so stops end past the target: median +3.5 deg, max +11 deg; the margin to a dare is 8 deg | 8 of 46 controlled stops overshot by more than 8 deg | P6 |
| D3 | Pickup snap | Up to +/-3.6 deg at torque-on, random | every capture | P6 |
| D4 | Targets close to dares | Pass-4 and shadow targets may sit 3 deg from a dare; pass 1 and 3 targets 8 deg | - | P6 |

### E. After the stop

| ID | Scenario | Mechanism | Seen | Closed by |
|---|---|---|---|---|
| E1 | Recovery fails | A second failed attempt or an 8 s timeout latches LANDING_UNSAFE: the wheel can stay on the dare and steering is off for the next spins | 5188 (timeout) | P7, P1 |
| E2 | Recovery with a wrong map | Walks a safe landing onto a real dare | - | P5 |
| E3 | Guest stops the wheel on a dare by hand | A coast that ends on a dare triggers recovery, so the wheel moves itself off | - | decision 2 |

### F. Power, boot and timing

| ID | Scenario | Mechanism | Seen | Closed by |
|---|---|---|---|---|
| F1 | ESP32 reset mid-spin | No control during boot; afterwards the capture budget waits for a 1 s rest | - | P3, P9 |
| F2 | ESP boots before 24 V | TMC_UART latches at boot -> A1 | 6260-6274 | P9 |
| F3 | Loop stalls | Flash saves and long prints (23-34 ms) blind the encoder -> A3, B3 | spins 5, 13 | P9 |
| F4 | Driver enable pin during an ESP reset | Unknown whether the driver floats or brakes a spinning wheel | - | test |

## Design principles

- **P1 Fail-operational.** While the wheel moves, nothing may release it. Faults are logged and the controller
  keeps steering with what still works: encoder lost -> finish the planned stop open-loop; driver SPI lost ->
  STEP/DIR still drives it. Faults are acted on only at rest, after the landing. Holding on is safe for
  hands: at the rim the motor pushes with only about 4 N (1.9 Nm at 0.46 m), so a hand always wins.
- **P2 Closed loop to the end.** Compare motor steps with the encoder on every tick and keep the gap under one
  full step (1.8 deg). On a slip, match the motor field to the wheel's measured speed so the rotor locks
  again (the motor stays on), then re-plan from where the wheel actually is.
- **P3 Every spin gets captured.** No one-attempt budget: retry on every tick while the wheel moves, capture
  right after a boot, treat a guest re-spin as a new spin, and watch slow pushes too.
- **P4 A safe stop is always reachable.** The planner keeps a live set of reachable safe stops. If braking
  cannot reach one (a very weak spin dying inside a dare), the motor carries the wheel gently to the next
  safe wedge as the last resort.
- **P5 Know where the wheel is.** A hall sensor on the frame and a magnet on the wheel check the map once per
  revolution (small drift corrected, large drift alarmed), and the coupling gets pinned so it cannot slip.
- **P6 Margins from measured error.** Plan the stop with margin (fixes the overshoot), match the field to the
  wheel at pickup (no 0.38 cap), then size the dare margins to the measured landing spread plus map error.
- **P7 Last resort at rest.** If the wheel is ever at rest on a dare or within 2 deg of one, move it off
  (existing recovery), only with a verified map, and retry until it succeeds.
- **P8 Never silently unarmed.** If steering is unavailable for any reason, the LEDs show it before the next
  spin.
- **P9 Robust basics.** Wait for the driver at boot instead of faulting, save to flash only at rest, give
  Serial a TX buffer, and test what the driver does when the ESP resets.

## Plan

| Stage | What | Needs | Closes |
|---|---|---|---|
| 1a | Fail-operational policy, capture retries, flash saves at rest, boot waits for the driver, Serial TX buffer | firmware | A1-A3, A7, B1-B5, F1-F3 |
| 1b | Stop planned with margin, pickup matched to wheel speed, pass 3 picks the farthest safe wedge | firmware | D2, D3, C4 |
| 1c | Step-vs-encoder tracking, re-lock and re-plan on slip; weak-spin carry | firmware | C1, C3, A4, A5 |
| 2 | Hall index sensor + coupling pin + map check | about $5 of parts, one GPIO, an hour of mechanical work | C2, D1, E2 |
| 3 | AS5600 linearity calibration + phase-aligned pickup (no snap) | firmware + one slow calibration revolution | D1, D3, C1 root cause |
| 4 | Only if slips persist: encoder-commutated motor control (TMC5160 direct coil mode) | firmware | C1 completely |

Each of 1a, 1b, 1c is its own flash and test session. Stage 1 is about 1-2 days including tests.

## Acceptance tests

- 100 consecutive spins across speeds and directions: 0 dare landings, 0 releases, at most 1 last-resort
  recovery.
- Fault injection during a stop: unplug the encoder, glitch the driver SPI, dip the 24 V, drag a hand, re-spin
  mid-stop, push weakly onto a dare, reset the ESP mid-spin, boot before 24 V.
- Map check: the pointer on the 18|1 line reads 0 +/- 1 deg after the 100 spins.

## Decisions for Timur

1. Party date: it sets how far to go before it.
2. A guest stops the wheel on a dare by hand: move it off (it looks like magic) or leave it (their choice)?
3. Weak spins that would die inside a dare: OK for the motor to carry the wheel gently into the next wedge?
4. Hall sensor: do you have one (A3144, DRV5023 or SS49E) and a small magnet?

## Appendix - release points in the live firmware

- enterFault always switches the driver off first. Calls that can fire with the wheel moving:
  controlSpeedSafe (CONTROL_OVERSPEED), controlDriverSafe (TMC_UART), controlSafetyChecks (ENCODER_STALE,
  ENCODER_VELOCITY, UNEXPECTED_REVERSAL, SUSTAINED_SPEEDUP, MOTOR_FIGHT, POSITION_IMPOSSIBLE,
  TAKEOVER_TIMEOUT), serviceDecelTick (MONOTONIC_VIOLATION; STEPPER_API via fasSetSpeedHz), LANDING_SETTLE
  (ENCODER_STALE, TAKEOVER_TIMEOUT), RECOVERY_NUDGE (ENCODER_STALE, LANDING_UNSAFE on timeout), the loop's
  encoder-outage watch (ENCODER_STALE).
- driverFreewheel without a fault: guest re-spin in control and in settle, SETTLE-DRAG.
- abandonCapture (switches off, no retry until the next 1 s rest): capture ownership unavailable, capture
  lease unavailable, arming prerequisites, initial plan infeasible, zero acceleration, pulse-first launch
  rejected, arming interlock/deadline, pulse-first observation rejected, final health/plan infeasible,
  torque-on interlock.
