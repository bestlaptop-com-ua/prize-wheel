# Prize Wheel Firmware — Correctness Redesign Report

> **Bench-verified working baseline:** commit `3938f41`, marked by branch
> `bench-working-v1` (owner-attended 2026-08-02): seven consecutive controlled
> spins, both directions, releases 0.43–1.82 rev/s, all `CONTROLLED_SAFE`
> with landing error < 2°. Flashable images for that build are in `build/`
> of that ref.

**File:** `prize_wheel_gpt/prize_wheel_gpt.ino` (complete rewrite of the control layer
on top of the proven P1 sensing pipeline and the verified label-true encoder frame)
**Toolchain:** Arduino ESP32 core 3.3.10 · FastAccelStepper 1.2.7 · TMCStepper 0.7.3
**Build:** `arduino-cli compile --fqbn esp32:esp32:esp32 --warnings all prize_wheel_gpt`

---

## 1. What changed and why

### Architecture
The old build had one mode machine that fused spin detection, prediction, and takeover
decisions (`IDLE/FREE_SPIN/PRECHARGE/TAKEOVER/SETTLE/RECOVERY_*`). The new build is an
explicit twelve-state machine that separates *what the wheel is doing* from *what the
controller is doing*:

```
IDLE_STOPPED -> MOTION_CANDIDATE -> (MANUAL_ADJUSTMENT | SPIN_PUSH)
SPIN_PUSH -> SPIN_RELEASED -> TARGET_RESERVED -> SPEED_MATCH_CAPTURE
          -> CONTROLLED_DECEL -> LANDING_SETTLE -> SOFT_HOLD -> IDLE_STOPPED
any powered state -> FAULT_LATCHED
plus the attended DIR_PROBE state
```

### Fixes for the listed defects

1. **Spin detection is purely kinematic.** `spinConfirmLogic()` uses only speed
   (≥0.12 rev/s), sustained same-direction travel (≥6°), duration (≥60 ms), and
   backtrack cancellation. Target availability is consulted *nowhere* in
   classification. Slow movement (>700 ms without confirming) becomes
   `MANUAL_ADJUSTMENT`; a manual movement that speeds up is *promoted* to a spin.
   A weak deliberate spin is a spin.

2. **Early reservation with designed-in availability.** After hand release
   (age ≥400 ms, speed <92% of peak, no new peak for 150 ms) the controller
   continuously evaluates the brake-reachability window
   `[latency + brakedStop(friction + motor ceiling) + 15°, 0.90×naturalStop − 5°]`
   and reserves as soon as the wheel decays into the engage window (≤0.55 rev/s) —
   for weak spins that is immediately at release. The minimum braking distance is
   the coast integral with the friction constant raised by the motor's braking
   authority (`c' = c + extra`): friction and motor brake *together* (modelling the
   ceiling alone leaves the window empty at every speed — found and fixed in
   adversarial review). The 0.90 reach fraction charges for trailing-phase slip
   losses. An **urgency trigger** forces reservation if an *open* window narrows to
   60° (wider than the largest safe-interior gap, 46°). With seed friction the
   wedge-uniform window is live from ~0.16 rev/s and 165° wide at the engage point.
   Weak-spin fallbacks: nearest safe interior point ahead (bounded assist
   deceleration), the safest reachable non-interior point, and finally a **shadow
   capture** of the natural stop point itself when it already lies safely inside a
   safe wedge. No fallback ever aims beyond the natural stop — a brake cannot add
   energy. While a direction's friction model has fewer than 2 valid fits, the
   reservation defers briefly (bounded by window width >150° and 3.5 s) so release
   coasts can feed the online fit.

3. **No uncontrolled landing paths.** There is no state that releases the motor and
   later reports the wedge as if controlled. The only ways a confirmed spin closes:
   controlled landing verdict, honest `GUEST_STOPPED`/`GUEST_RESPUN` (external
   contact), honest `NO_REACHABLE_SAFE` (physics limit, see §7), honest
   `CONTROL_LOCKED` (calibration/driver lock), or `FAULTED` with a latched fault
   code. A dare landing after a controlled attempt latches `FC_LANDING_UNSAFE` and
   is printed as `LANDED-DARE ... THIS IS A FAILURE`.

4. **Correct FastAccelStepper 1.2.7 usage.** One `runForward()`/`runBackward()` per
   capture (return code checked), entered at wheel-matched speed via
   `setJumpStart(v²/2a)`. Every live speed change is `setSpeedInHz()` **+
   `applySpeedAcceleration()`** at a bounded 25 ms tick — the documented way to
   retarget an active continuous run. The FAS acceleration is a *tracking* rate set
   to 3× the command-profile deceleration (capped 2000 sps², min 1.5×): with a 1:1
   rate, `stepsToStop()` equals the whole remaining runway at capture (jump-start
   seeding) and the stop gate degenerates the entire braking phase into one
   open-loop ramp — found in adversarial review; at 3× the gate correctly fires
   only in the last few degrees. Position is only ever reset at motor standstill
   (asserted; a stale queue at capture is an `FC_STEPPER_API` fault). Speed updates
   stop once `stopMove()` is issued (the library ignores them while stopping).
   Every supersede path releases through `driverFreewheel()`, which floats the
   coils *first* and then clears any still-draining pulse queue — a mechanically
   inert queue reset, so a later capture can never energize onto a stale ramp.

5. **Truly monotonic deceleration.** `serviceDecelTick()` computes
   `newCmd = min(prevCmd, profile)` and *faults* (`FC_MONOTONIC_VIOLATION`) if a
   computed command ever tries to increase. The 0.88 × trailing-minimum bound is
   applied at **capture entry** (the field starts strictly behind the wheel);
   hardware testing showed it must *not* be applied continuously — tracking a
   fraction of a decaying wheel re-opens the slip gap every tick and turns the
   whole takeover into an audible pole-slip ratchet with ~5× the planned braking
   force. Instead the wheel decays onto the constant entry field once, couples,
   and is paced down the profile in synchronization (silent load-angle braking).
   Never-pull is guaranteed structurally: targets are capped below the natural
   stop so a coupled wheel always pushes into the field, a *chase-down* drops the
   command to the wheel whenever the wheel is slower than the field, and a
   wheel-speed-up detector trims the command immediately and latches
   `FC_SUSTAINED_SPEEDUP` after 400 ms; the maximum rise is recorded in the spin
   summary.

6. **No accelerating minimum-speed clamp.** There is no `max(desired, fixedMin)`
   anywhere. When the command falls below the 40 Hz practical floor the controller
   goes straight into the `stopMove()` taper; the soft field-hold happens only
   after the landing verdict, inside the safe wedge.

7. **No abrupt stops in normal control.** Normal landing is: encoder-gated
   `stopMove()` when remaining runway ≤ `stepsToStop()` + 2°, tapering at the
   planned deceleration. `forceStop()` appears exactly once, inside
   `enterFault()`, and only for `FC_MOTOR_FIGHT` / `FC_UNEXPECTED_REVERSAL` where
   continued pulses actively harm the mechanism; every other fault ramps down via
   `stopMove()`.

8. **Landing = position AND speed.** `landingVerdict()` runs only after the pulse
   train has tapered to zero *and* |ω| ≤ 0.02 rev/s for 500 ms; it then requires
   the settled angle to be ≥5° inside the target wedge for `CONTROLLED_SAFE`,
   downgrades honestly to `EDGE_SAFE`/`OFF_TARGET_SAFE`, and **latches
   `FC_LANDING_UNSAFE`** for a settle on a dare *or within 2° of a dare boundary*
   (within measurement error of the invariant). Small pre-stillness creep is the
   wheel's own residual momentum and belongs to the verdict; travel no residual
   creep can produce (>45° while moving) means a hand is dragging the wheel and
   is released and closed honestly as `GUEST_STOPPED`; sub-threshold drift
   restarts the stillness window so the verdict samples a truly settled position.

9. **Wedge-uniform interior targets.** Targets live in `[start+8°, end−8°]` of a
   safe wedge. Each safe wedge is counted **once** even when its interval
   qualifies in two laps (the per-wedge lap with the most natural runway is kept),
   then one wedge is drawn uniformly, then one uniform random point inside its
   qualifying interval. No extra powered revolution: the runway ceiling is the
   predicted natural stop minus 5°.

10. **Reachability with a live friction model.** Separate CW/CCW `dω/dt = −(c+bω)`
    models, fitted from genuine free-coast samples between release and engagement,
    with rejection of: user contact (pair deceleration >1.2 rad/s² discards the
    coast), reversal/speed-up pairs, invalid-encoder samples, short intervals, and
    out-of-bounds coefficients. NVS persistence only after ≥2 corroborating fits.

11. **Event-based current.** `setCurrentStage()` is the only post-boot writer of
    TMC current registers, on transitions of
    `FREEWHEEL/PRECHARGE(100mA)/CAPTURE(600mA)/BRAKE(450mA)/TAPER(300mA)/
    HOLD1(150mA)/HOLD2(80mA)`. No periodic UART writes; encoder timing preserved.

12. **Manual adjustment.** Slow repositioning of a stopped wheel never counts a
    spin, never selects a target, never touches the motor, and is summarized as
    `MANUAL-ADJUST`. Promotion to a spin needs the same deliberate-spin evidence
    as any spin.

13. **Latched faults.** All nine brief-listed fault sources latch
    `FAULT_LATCHED` with a specific code, use the safest shutdown, close the spin
    record honestly as `FAULTED`, and lock automatic control until the operator
    sends `r` (which re-checks the TMC UART and stays latched while it still
    fails) — or re-runs `p` when calibration was invalidated (a failed probe
    itself latches `FC_DIR_CAL_INVALID`). The TMC UART is health-checked at boot
    and between spins (wheel at rest). Encoder loss in the *unpowered* motion
    states is caught by a 1 s outage watchdog, so no state can wedge forever with
    an open spin record; the powered precharge has its own velocity-wait timeout.
    A spin during a latched fault produces exactly one honest `CONTROL_LOCKED`
    record, closed only when the wheel actually rests.

### What was deliberately kept
The entire P1 sensing pipeline (1 kHz scheduled sampling without burst catch-up,
completion timestamps, explicit I2C failure, freshness gates, long-gap re-prime,
half-turn alias rejection, implausible-delta rejection, windowed velocity + real-dt
EMA), the label-true `rawZero` frame and `z` semantics, the two-leg direction probe
(same NVS keys `pos_sign`/`dir_ok`, so an existing calibration survives this
flash), the bench-proven `driverConfig()`, and the `prizewheel` NVS namespace.
The nearest-turn re-prime snap was switched to the round-to-nearest-turn form
(`lroundf(diff/4096)`), which is exact for negative multiturn counts too.

---

## 2. States and invariants

### States (12)
| State | Motor | Purpose |
|---|---|---|
| `IDLE_STOPPED` | floating | waiting; wheel loose in hand |
| `MOTION_CANDIDATE` | floating | motion seen; classifying |
| `MANUAL_ADJUSTMENT` | floating | slow repositioning; promotable |
| `SPIN_PUSH` | floating | deliberate spin, hand still on |
| `SPIN_RELEASED` | floating | free coast; friction fit; reserving |
| `TARGET_RESERVED` | 100 mA, no pulses | 80 ms phase settle; target held |
| `SPEED_MATCH_CAPTURE` | 600 mA | pulse train at 0.88× trailing wheel speed |
| `CONTROLLED_DECEL` | 450 mA | monotonic profile to target |
| `LANDING_SETTLE` | 300 mA | taper done; wait for still + verify |
| `SOFT_HOLD` | 150→80 mA→float | imperceptible release |
| `DIR_PROBE` | 350 mA | attended two-leg calibration |
| `FAULT_LATCHED` | ramp-down→float | honest, latched, `r` to clear |

### Runtime-enforced invariants
| Invariant | Enforcement |
|---|---|
| target wedge ∉ {1,5} | checked at reservation → `FC_TARGET_INVARIANT` |
| motor direction == spin direction | single signed `runForward/Backward` from probe sign; never reissued |
| `newCmd ≤ prevCmd + 1e-4` | checked every tick → `FC_MONOTONIC_VIOLATION` |
| `cmd ≤ 0.88 × trailing-min wheel speed` while wheel leads | applied every tick; chase-down when wheel trails |
| no `forceStop*` with coils energized in normal control | only `enterFault` for fight/reversal; `driverFreewheel` clears a stale queue *after* floating the coils (mechanically inert) |
| landing wedge ∉ {1,5} and ≥2° from any dare line | `landingVerdict` → `FC_LANDING_UNSAFE` latch |
| landing ≥5° inside wedge for success | `landingVerdict` downgrade otherwise |
| control impossible without valid probe + TMC OK | `controlAvailable()` gate |
| position freshness during power | → `FC_ENCODER_STALE` |

---

## 3. Verification and compilation

The firmware went through two adversarial multi-agent review rounds against this
brief (30 agents round 1: six specialized reviewers, every non-minor finding
independently re-derived by a verifier; 21 confirmed findings — including two
critical flaws in the original reachability-window math and the stop-gate
geometry — all fixed; round 2 re-verified each fix and swept the changed regions).

```
Sketch uses 422462 bytes (32%) of program storage space. Maximum is 1310720 bytes.
Global variables use 122428 bytes (37%) of dynamic memory, leaving 205252 bytes
for local variables. Maximum is 327680 bytes.
```

`--warnings all`: **0 warnings from the sketch.** 9 warnings remain, all inside
FastAccelStepper 1.2.7's ESP-IDF platform files (`missing-field-initializers` in
MCPWM/I2S config structs, one deprecated `gpio_iomux_in` call) — library code,
reviewed, benign. Return values are checked for `setSpeedInHz`, `setAcceleration`,
`move`, `runForward`, `runBackward`; `applySpeedAcceleration` returns void by API.

## 4. Tuning / constants table

| Constant | Value | Meaning |
|---|---|---|
| `SPIN_DETECT_REV_S` | 0.12 | deliberate-spin speed floor |
| `SPIN_CONFIRM_TRAVEL_DEG` / `_MS` | 6° / 60 ms | confirmation travel & time |
| `MANUAL_CLASSIFY_MS` | 700 | slow motion this old ⇒ manual |
| `RELEASE_MIN_AGE_MS` / `RELEASE_PEAK_FRACTION` / `RELEASE_DECAY_MS` | 400 / 0.92 / 150 | hand-release gate |
| `ENGAGE_MAX_REV_S` | 0.55 | reserve at/below this (owner-set intercept) |
| `ENGAGE_URGENCY_WINDOW_DEG` | 60 | force-reserve before an open window < largest safe gap |
| `ENGAGE_LATENCY_S` | 0.22 | equivalent free-coast loss before braking bites |
| `NATURAL_REACH_FRACTION` | 0.90 | reach ceiling (trailing-phase loss charge) |
| `MIN_BRAKE_HEADROOM_DEG` / `NATURAL_SHAVE_MARGIN_DEG` | 15 / 5 | window margins |
| `DARE_PROXIMITY_FAULT_DEG` | 2 | settle this close to a dare line ⇒ fault |
| `CAL_DEFER_MIN_WIDTH_DEG` / `CAL_DEFER_MAX_MS` | 120 / 3500 | friction-bootstrap deferral bounds |
| `FAS_TRACK_ACCEL_FACTOR` / `MAX` | 3× / 2000 sps² | pulse-generator tracking rate vs profile |
| `LANDING_DRAG_ABORT_DEG` | 45 | settle travel no residual creep can produce ⇒ guest drag |
| `RESERVED_VEL_TIMEOUT_MS` / `ENCODER_OUTAGE_FAULT_MS` | 500 / 1000 | powered-wait / motion-state encoder watchdogs |
| `SAFE_WEDGE_EDGE_MARGIN_DEG` | 8 | target interior margin |
| `LANDING_INTERIOR_MIN_DEG` | 5 | landing verification margin |
| `TRAIL_FRACTION` | 0.88 | cmd ≤ 0.88 × trailing-min wheel speed |
| `TRAIL_WINDOW_TICKS` | 8 (~200 ms) | trailing-min window |
| `CAPTURE_MAX_CMD_REV_S` | 0.48 | initial command cap |
| `CMD_UPDATE_MS` | 25 | control tick |
| `DECEL_CEILING_SPS2` | 650 | natural-motion decel ceiling (motor sps²) |
| `ASSIST_DECEL_MAX_SPS2` | 1100 | weak-spin fallback cap (≤ bench-clean 1000–1200) |
| `FAS floor` | 40 Hz (0.00625 rev/s) | below ⇒ stopMove taper |
| `SPEEDUP_NOISE_REV_S` / trim / fault | 0.02 / 30 ms / 400 ms | speed-up detector |
| `FIGHT_SPEED_FRACTION` / grace / confirm | 0.45 / 150 / 150 ms | fight watchdog |
| `OPPOSITE_ABORT_REV_S` / `_MS` | 0.05 / 75 | reversal fault |
| `STILL_REV_S` / `SETTLE_MS` | 0.02 / 500 | stillness definition |
| current ladder | 100/600/450/300/150/80 mA | coupling stiffness for synchronized load-angle braking (owner ladder: 180 rattles, 650 hums) |
| `PRECHARGE_MS` / `PICKUP_COHERENCE_MS` | 80 / 250 | phase settle / capture verify |
| `TAKEOVER_TIMEOUT_MS` / `SETTLE_TIMEOUT_MS` | 30 s / 10 s | hard budgets |
| friction seeds | c=0.30, b=0.15 (rad/s) | replaced by online CW/CCW fits |

## 5. Bench-test checklist

Coordinate frame
- [ ] Pointer at physical zero → raw ≈ 3807, angle ≈ 0.00°, wedge 0
- [ ] Slow CW rotation through AS5600 rollover: angle increases continuously; wedge numbers match labels
- [ ] Repeat CCW
- [ ] RTS-reset 5×: identical reading each boot; reset at random wedge keeps identity

Direction probe
- [ ] `p` from a safe wedge center: legs move opposite; retrace error < 3°; `PASS`, takeover enabled
- [ ] Simulated stuck DIR (short GPIO27 high): probe FAILS, calibration cleared, takeover locked

Manual adjustment
- [ ] Slowly move the stopped wheel 5–20°: motor stays floating, `MANUAL-ADJUST` printed, no spin count, no target
- [ ] Longer slow repositioning (≥90°, slow): still manual
- [ ] Immediately after, a real spin is detected and controlled normally
- [ ] A slow movement that speeds up mid-way is promoted to a spin

Spins (both directions, all of): very weak, medium, hard, short push, long push,
starting near wedges 1/5, naturally aimed at 1/5, naturally aimed at safe wedges
- [ ] Every spin prints `START` → `RELEASE` → `RESERVE` → `CAPTURE` → `SUMMARY`
- [ ] Direction never reverses; no visible speed-up; no abrupt braking; lands inside a safe wedge interior
- [ ] `SUMMARY` shows `rise≈0`, `cmdMax ≤ 0.88×` takeover speed, monotonic profile (verify with `d` capture)

Distribution (≥200 spins, both directions)
- [ ] Wedges 1 and 5: zero controlled landings
- [ ] All ten safe wedges reachable under appropriate spin conditions; no center-only clustering; no lap double-weighting

Motion profile (from `d` captures)
- [ ] `cmd_mrev` column never increases during control
- [ ] no sustained `omega` rise after capture; `cmd ≤` trailing wheel speed while wheel leads
- [ ] deceleration ≤ ceiling; no step-frequency discontinuity at landing

Fault injection
- [ ] Disconnect AS5600 mid-control → `FAULT ENCODER_STALE`, ramp-down, latched, honest summary
- [ ] Disconnect TMC UART → boot/landing check fails, takeover locked, `r` re-checks
- [ ] Stuck DIR / belt off / motor power off → fight or reversal or probe failure; no false `CONTROLLED_SAFE`
- [ ] After any fault: automatic control stays locked until `r` (and `p` where calibration was cleared)

## 6. Known limitations

1. **Physics floor for ultra-weak spins.** A spin released at ≲0.13 rev/s whose
   entire assist-braking band *and* natural stop point lie inside (or within a
   few degrees of) a dare wedge cannot be saved by a brake-only controller
   under the no-abrupt-braking rule — the engagement latency alone can exceed
   the distance to the dare entry. Verified by trace: the firmware closes such
   a spin honestly (`NO_REACHABLE_SAFE`, `LANDED-DARE ... THIS IS A FAILURE`)
   rather than masking it. For all spins released at normal strength the
   urgency trigger reserves while the window is ≥60° wide, so this corner is
   confined to deliberately dying releases aimed at a dare.
2. **Coupled endgame.** In the final degrees the wheel is belt-locked to the
   tapering field; the strict "command below wheel speed" inequality is enforced
   while the wheel leads the field, and by the chase-down + speed-up detector once
   coupled. This is inherent to braking *to a point* with a stepper.
3. **Friction model cold start.** Until ~2 valid coasts per direction the seeds
   (c=0.30, b=0.15) drive reachability; margins absorb seed error, the
   bootstrap deferral (≤3.5 s while the window is ≥120° wide) feeds the fit,
   and fits update online. Releases below ~0.8 rev/s may not individually
   yield a valid fit (insufficient coast span); calibration then accrues from
   the stronger spins. The engage/defer gates evaluate one tick ahead of the
   final pre-target fit blend — a ≤1 ms model mismatch, self-correcting.
   `F` resets the model.
4. **TMC UART health** is verified at boot, at `r`, and between spins (wheel at
   rest) — not during control, because a blocking UART read would corrupt the
   1 kHz encoder cadence mid-takeover. Register writes at stage transitions are
   fire-and-forget by TMC2209 UART design.
5. **Spin numbering** restarts at 1 each boot (not NVS-persisted, to avoid flash
   wear per spin).
6. **Bench acceptance tests** in §5 require the physical wheel; they have not been
   executed in this change — compile-level verification only. Run the checklist
   before guest use.
