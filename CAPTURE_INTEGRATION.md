# Unvalidated production capture integration

This is local code for review, with automatic takeover **off at boot**. It is
not a production-ready firmware. The target build passes, but this integration
has not been physically tested. The separate pulse-first diagnostic produced a failed positive pickup;
passing helper tests does not establish smooth pickup in either direction.

## Control and selection

- Retains the production deficit-weighted safe-wedge selector and its shadow,
  nearest-interior and edge-safe fallback order. Every pass now includes the
  coherent pulse-ramp stopping floor. No gentle6 nearest-center selector is
  imported, and no fallback invents extra runway beyond the friction model.
- Capture requires forward speed from 0.02 through 0.20 rps, at most 640 Hz, and
  plan acceleration at most 320 STEP/s². Faster spins remain freewheeling until
  inside the entry envelope. Integer deceleration above the cap is rejected;
  it is never silently clipped to a plan that cannot reach the selected prize.
  The 320 STEP/s² cap is a configured ceiling, not a physically validated range.
  The reported negative trial used 68 STEP/s²; the positive trial planned
  92 STEP/s² but failed during pickup. Neither validates the full ceiling.
- The reserved absolute encoder target remains fixed. The plan is calculated
  initially and recalculated once using remaining runway after pulse proof.
  An infeasible final plan abandons capture with outputs off. It does not choose
  another prize, restart arming, or return permission for another pickup.
- The driver is prepared at 2200 mA with EN high. Calibrated DIR polarity selects
  either physical direction while both use FastAccelStepper's forward path.
  At least eight pulses and 20 ms of rate evidence precede torque. The same
  synchronous pulse/DIR/encoder check runs after SPI/configuration traffic.
- A separate 150 ms ESP task-timer lease checks the EN edge under its cutoff
  lock, including a maximum 2 ms age for the final pulse proof. Expiry/cancel
  cannot re-enable. This is loop-independent, not a hard hardware cutoff:
  flash/cache or scheduler delays can postpone timer-task execution.
- The planner, monotone speed limiter, FAS acceleration and final stop share
  one deceleration. The candidate median-based speed-up monitor replaces the
  old single-low-sample baseline and repeated abrupt speed trimming.
- An immediate absolute 0.30 rps limit in powered capture, braking and settling
  complements the existing debounced monitors. It latches the appended
  `CONTROL_OVERSPEED=17` fault. HOLD retains its separate hand-release path.
  Strict DRV_STATUS/GSTAT/version/microstep checks run every 100 ms in capture,
  braking, settling and hold; failure disables outputs and latches the existing
  TMC fault. No status acknowledgement or automatic fault clearing is added.
- Capture, braking and settling retain 2200 mA. Existing hold behavior is
  unchanged: 550 mA for 1.5 s, then 300 mA for 1.2 s, then freewheel. This code does
  not yet implement holding the selected prize until the next hand spin.

Pulse-speed agreement does **not** measure rotor electrical phase. A decreasing
field speed does not prove exclusively braking mechanical torque. Production
selection keeps the inherited natural-stop model as a reachability policy;
that model and its fit quality still need validation on the new mechanics.

## Physical-spin ownership and outcomes

`PwCaptureCycle` is independent of logical spin records. Reservation does not
consume the attempt; beginning pulse arming does. A failed plan, failed pulse
proof, reversal or later record reclassification never replenishes it.
Normal requalification requires a full 1 s of fresh stationary encoder observations,
at most 1 degree anchor drift, no open spin, fault-free IDLE, freewheel current
stage, EN high and an empty pulse queue. Powered hold cannot requalify it.

A completed safe landing has a separate next-spin path. Only the existing
fresh, stable 500 ms landing verdict can authorize it, after an energized
capture that was not abandoned. This does not reset the attempt during HOLD.
Actual hold release must leave EN high, freewheel and an empty queue; then only
the ordinary speed/travel/duration hand-spin confirmation consumes that
one-use authorization. A record reclassification, failed pickup or arbitrary
powered stop cannot create it. A fault revokes it and requires normal fresh
freewheel rest after recovery. Thus a guest can spin again during the existing
2.7 s hold fade without having to wait for an extra unpowered rest first.

`CAPTURE_ABANDON` names the failure. Every normal spin summary reports
`captureAttempted`, `captureEnergized` and `captureAbandoned` for the current
physical cycle, including records opened by reclassification. The existing
`NO_REACHABLE_SAFE` result can mean an unavailable initial target or abandonment
of its capture plan; those fields and the event explain the distinction. Fault
and calibration bytes retain their previous compatibility handling.

The existing friction seeds remain `c=.55`, `b=.28` in the radian-speed model,
not directly in rps². The narrow capture envelope and coherent ramp can leave
no feasible party target. That outcome remains a free coast, not forced
selection. A 0.20 rps synthetic coast also cannot cover the existing 1.2 rad/s
fit-span minimum after the 0.045 rps sampling floor. Such a trial cannot be
claimed to calibrate the model; broader unpowered coast evidence may be needed.

## Proposed diagnostic adapter interface — not implemented

The adapter should own bounded spin-up exclusively, then disable EN and drain
the pulse queue before handing a synthetic released spin to normal production
classification. A proposed entry is `beginBoundedSyntheticCoast(direction,
validatedStartEvidence)`. It must verify the existing physical-cycle rest
qualification instead of resetting the capture budget itself.

The adapter retains independent total-time, pulse, travel, speed and driver
health limits through ordinary production capture, plus an abort hook that
disables outputs before bookkeeping. It cannot select prizes, rewrite targets,
bypass capture proof or directly enable takeover torque. Its test-build
permission hook must prevent ordinary `e`, probe, calibration or any other
runtime command from enabling motion while the one-shot adapter is inactive.

This current source has no such adapter or exclusive test-build permission
hook. It retains the ordinary production command handler, so a deployable test
variant needs that separate integration and review first. The existing smaller
production diagnostic recorder also needs review for evidence completeness;
the richer candidate PSRAM recorder was not silently imported here.

## Validation performed

`tests/run_host_tests.cmd` passes with MSVC C++17 `/W4 /WX`. It exercises the
actual capture-arm, lease, physical-cycle, brake-profile and speed-up headers:
both DIR polarities, missing/stale encoder or pulses, upper-speed rejection,
timer expiry without loop service, stale proof, cancellation, failed output
enable, one-edge lease behavior, failed-pickup/re-push ownership, drift and
rest requalification, successful-HOLD/new-spin ownership, fault invalidation,
acceleration-cap rejection and absolute/debounced speed-up logic.
The prior persistence/model/lifecycle tests, including 65,536 recovery-journal
byte combinations, still pass. GPIO/timer calls use host stubs; they do not
establish hardware timing. The frozen source also compiled successfully on
the actual pinned ESP32-S3 toolchain; see [build proof](VALIDATION_2026-09-20.md).
That production binary was not uploaded.
