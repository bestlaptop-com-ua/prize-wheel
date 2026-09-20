# Production compatibility and learning changes

Review branch `fix/plywood-wheel-capture`, based on `d9b514a`.
This is an integration candidate, not a validated production release. These
changes do not validate pickup on the heavy wheel. The production party
selection policy and hold settings are retained; the later pulse-first capture
integration is described in [CAPTURE_INTEGRATION.md](CAPTURE_INTEGRATION.md).
No gentle6 selector or selfspin motion commands are included. Takeover defaults
off while this integration remains unvalidated.

## Changes

- Imported the verified ESP32-S3 STEP-clock correction verbatim from the
  preserved MILL-PC snapshot. It expects FastAccelStepper 1.2.7 and ESP32 core
  3.3.10, with the single stepper on MCPWM group 0 / timer 0. Unexpected divider
  layouts latch a stepper fault. Review the helper before library/core upgrades.
- EN is high from the beginning of setup. The fault latch is restored before
  driver/encoder health checks. A fault disables the output before logging or
  NVS work; queued pulses drain with outputs disabled. Control and current-stage
  entry refuse a nonzero RAM fault.
- Existing fault IDs 0–13 retain their values. Compatibility names for deployed
  `TRACKING_LOST=14` and `SELFSPIN_ABORT=15` are appended, followed by
  `UNKNOWN_PERSISTED=16`. Capture integration subsequently appends
  `CONTROL_OVERSPEED=17` without renumbering any prior ID; raw 16 remains an
  unknown sentinel and cannot be cleared as an ordinary fault. Every nonzero
  saved byte locks control. An unknown byte
  remains unchanged in NVS and is shown as `savedFaultRaw`; ordinary `r` refuses
  it. NVS-open failure also locks control. No boot path clears a fault.
- Startup also reads diagnostic recovery journal `recovGuard` before hardware
  health initialization. Any nonzero guard locks control, even if `fltLatch=0`.
  Guard 15 with primary 0 restores `SELFSPIN_ABORT=15` in RAM; unknown primary
  bytes remain unchanged, and an unknown guard becomes an unknown RAM fault.
  This compatibility layer writes neither journal byte. Ordinary production
  `r` refuses every nonzero guard, including primary 15 / guard 15: finishing
  that journal requires the diagnostic recovery procedure and its additional
  health/settling checks. Primary 0 / guard 15 also fails the exact-primary-15
  prerequisite. Production does not silently finish interrupted recovery.
- Explicit `r` for a known fault retains the existing TMC reconfiguration check,
  additionally requires fresh, valid stopped-wheel feedback, and verifies the
  stored clear before clearing RAM. An explicit attended direction probe may
  recover `DIR_CAL`; its saved clear now succeeds before it can energize.
  A healthy-idle probe permits the already-clear `0 == 0` case. A failed
  first-fault write followed by a different NVS reread refuses recovery.
- Friction key `fricVer=0x0201` identifies the direct-drive NEMA23 / existing
  `c+b*omega` model. Missing/different versions migrate once to the existing v2
  seeds (`c=0.55`, `b=0.28`, zero fits). Current-version valid fits survive reboot;
  invalid values reset only their direction. The migration marker is written
  after both models pass write/readback checks. Failed migration is reported
  and retried on the next boot. No fault or calibration keys are touched.
- A nonterminal reservation with insufficient data or a rejected clean fit can
  continue sampling the same free coast. The same samples are not re-evaluated
  every control tick. A successful coast updates at most once. Power closes the
  clean fitting window; confirmed contact, reversal, re-push or fault discards
  the unfinished window. A physical EN/current-stage check excludes powered
  samples independently of the state transition.

## Deliberate limits

Legacy friction records have no hardware provenance. Even in-range legacy
values are reseeded on the first schema migration; they cannot be identified
as old-wheel versus new-wheel fits. The retained v2 seeds are not a new torque
or friction calibration. The estimator still excludes nonpositive speed drops;
its bias on an imbalanced coast needs physical/model work separately.

`rawZero`, `dir_ok` and `pos_sign` still have no hardware generation metadata.
They are preserved rather than silently invalidating the known current
calibration. Replacing mechanics, wiring or sensor mounting still requires
physical frame/direction verification. The AS5600 is on the motor rear shaft;
it does not independently establish disc motion or coupling integrity.

Friction values remain separate NVS keys as in the existing firmware. The
version marker makes first migration retryable; it does not make every later
multi-key fit update an atomic transaction. Invalid values are checked at boot.

Host tests exercise the actual policy/storage helpers, not the Arduino GPIO,
SPI, Preferences implementation or FastAccelStepper timing. This candidate
still needs target compilation, independent review and physical validation.
The compatibility/learning work itself did not tune motion. Subsequent capture
integration removes the ordinary precharge sequence and uses2200 mA through
braking/settling; production hold behavior remains unchanged.

## Validation

`tests/run_host_tests.cmd` builds with MSVC C++17 `/W4 /WX` and executes
`tests/production_policy_test.cpp`. It passed on 2026-09-20. Cases include:

- First migration, valid fits surviving reboot without writes, one-direction
  invalid-value repair, hardware/model change, and interrupted migration retry.
- Fault/calibration keys remaining untouched by all migration writes.
- Early unsuccessful reservation followed by more samples, one accepted fit,
  repeated-data throttling, closure before power, and contact/reversal discard.
- All 256 saved fault bytes, first-fault preservation, unknown-clear refusal,
  mismatched-read refusal and unavailable storage; all 65,536 primary/guard
  byte combinations, interrupted primary clear, unknown guard and ordinary
  clear refusal while a recovery guard remains.

See [BUILD_PRODUCTION.md](BUILD_PRODUCTION.md) for the pinned ESP32-S3 build
instructions and [completed build proof](VALIDATION_2026-09-20.md). The current
production integration passes target compilation but has no loaded-wheel
validation; successful builds do not establish readiness.

Build outputs stay under ignored `build/`. The production integration was
compiled on the wheel PC in an isolated directory and was not flashed.
The sketch retains its existing CRLF format; whitespace review uses
`git -c core.whitespace=cr-at-eol diff --check`.
