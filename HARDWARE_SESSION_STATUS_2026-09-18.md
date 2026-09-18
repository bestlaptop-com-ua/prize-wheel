# Hardware test session — 2026-09-18

## Current status

The direct-drive NEMA23 assembly has completed controlled stops and persistent
holding, but braking is **not yet reliably quiet or accurate**. Several trials
produced skipping clicks or rattling. This remains an attended experiment on
`new-hardware-evaluation`.

This report supplements [the earlier hardware review](HARDWARE_REVIEW_2026-09-18.md)
with tests performed through Roam. It preserves that review's commit, `447dcb1`.
**The firmware changes and detailed artifacts remain in the wheel PC's working
copy and are not included in this documentation commit. The GitHub sketch does
not yet reproduce the latest field build.**

Roam disconnected after the six-second test firmware's upload process started.
Compilation passed, but upload completion and startup verification have not been
retrieved. Do not assume that build is running or its new recorder is armed.
No loaded test of the six-second build has been performed.

## Assembly and current settings

- 36-inch, 3/4-inch hickory plywood disc; owner estimates no more than 9 kg for
  the disc and around 10 kg for the rotating assembly. These are not weighed values.
- Go-kart hub, 3/4-inch shaft, two pillow-block bearings, Lovejoy coupling,
  direct-drive NEMA23, ESP32-S3, and TMC5160T Pro over SPI.
- AS5600 is on the motor's rear shaft. The owner observed synchronized
  shaft/coupling motion during the latest powered trial.
- Latest verified powered build requests 350 mA precharge, 2,200 mA capture and
  braking, and 1,650 mA continuous hold until sustained hand-spin motion.
  These are driver RMS-current requests, not measured phase currents. The motor
  nameplate says 3 A; its RMS/peak convention remains unverified.
- Startup leaves outputs disabled. The latest coast trial explicitly disabled
  takeover, so it had no final motor hold.

## Changes tested on the wheel PC

1. Corrected a STEP-clock divider error: requested 500/1,000/2,000 Hz had
   produced approximately 100/200/400 pulses per second. GPIO audits verified
   the corrected clock and representative deceleration ramps.
2. Matched pulse acceleration to planned deceleration, removed abrupt command
   reductions, and retained braking current through pulse stop and settling.
3. Replaced the speed-up watchdog's lowest-sample baseline with a seven-sample
   median. Replay reproduced the old false trip and verified sustained
   acceleration still faults. Other fault checks remain active.
4. Increased persistent hold for the heavier wheel. An earlier successful
   trial held its encoder angle for more than eight minutes.
5. Retained 2,200 mA through braking instead of dropping to 1,650 mA after
   capture. Excessive travel after pulse stop now latches a tracking-loss
   fault instead of automatically retrying continued motion.

The most recently verified deployed build was `v2-brake2200-20260918`.
Its application SHA-256 is
`cda94f972150907728016af50fbd603d68ba2813635cd3a1ccd7990d8a5bc4a0`.

## Latest powered trial: stopped with skipping clicks

Spin 6 started around 16:56:55 local time. The owner reported synchronized
shaft motion and several skipping clicks while stopping.

| Measurement | Result |
| --- | --- |
| Release / capture speed | 0.472 / 0.470 rev/s |
| Initial pulse speed | 0.446 rev/s, 1,428 Hz |
| Deceleration | 518 steps/s², or 0.1619 rev/s² |
| Nominal pulse-ramp duration | About 2.8 seconds |
| Selected / final angle | 254.9° / 300.8° |
| Overshoot | About 45.9° |
| Final hold | 1,650 mA; subsequently stable at 300.67° |

The complete 16,384-row trace showed repeated encoder advances while the
pulse ramp decreased smoothly. This is consistent with rotor loss of
synchronization with the commanded field; it does not establish the cause
of every earlier failure. Driver snapshots showed no thermal or short-circuit
faults. `EDGE_SAFE` does not mean accurate targeting or quiet braking.
An earlier encoder-speed collapse during precharge remains unexplained.

## Motor-disabled coast

Spin 7 released around 17:03:49 and was classified stopped about 24.8 seconds
later. The ring buffer retained only the final 16.383 seconds before freezing,
not the whole coast. All 16,384 retained samples showed zero requested current,
disabled outputs, no STEP-count change, and valid encoder reads.

The retained segment covered approximately 857.9°. Speed fell from about
0.285 to 0.182 rev/s, rose to 0.229, fell to 0.078, rose to 0.150, then approached
zero. These periodic changes with the motor off support an imbalance contribution.

An exploratory fit with angle-dependent terms estimated cyclic acceleration
around 0.045 rev/s². Friction coefficients varied between subsets, so this
one-direction partial trace has **not** been persisted as a validated calibration.
The forced boot seeds (`c=0.55`, `b=0.28`) predict substantially more drag than
the observed average. Constant drag alone does not describe this coast well.

## Six-second experiment: compiled, deployment unverified

A cap-only reduction to 320 steps/s² was rejected before deployment: with the
existing friction assumptions, target-selection windows were empty across the
checked capture-speed range. Its saved candidate is marked `DO_NOT_UPLOAD`.

The replacement `v2-gentle6-20260918` diagnostic build selects an allowed wedge
center with enough distance for a nominal pulse ramp of at least six seconds,
capped at 320 steps/s² (0.10 rev/s²). It recalculates that target from actual entry
speed after precharge, bypassing the uncalibrated friction target prediction.
Braking remains 2,200 mA and holding 1,650 mA; monotonic commands and existing
fault/landing checks remain. The temporary targeting mode is explicitly labeled TEST.

Compilation passed: 895,619 bytes flash and 41,336 bytes global variables.
Compile-time checks exercised the production target helper in both directions
over 24 angles and 15 entry speeds: forbidden wedges, count rounding, integer
acceleration limits, minimum duration, and invalid inputs. This does not prove
quiet loaded braking, accurate landing, or production prize-selection behavior.
Very weak spins remain a limitation.

## Resume checklist

1. Restore Roam and inspect `work/gentle6_wheel_upload.exit`,
   `gentle6_wheel_verified.exit`, the verified boot log, and any upload error.
   The process may already have finished; do not blindly repeat the upload.
2. After verified startup, start `work/gentle6_capture.py`. Confirm fresh idle
   status, takeover enabled, and diagnostics armed before another spin.
3. Perform one attended spin in the same direction; inspect capture, the longer
   ramp, landing, and hold before additional directions.
4. Retrieve and review the exact firmware, helper headers, reproducible tests,
   and detailed session notes from the wheel PC. Preserve the newer GitHub
   hardware review; exclude scratch builds and connection data.
5. Correct calibration/boot reseeding and evaluate imbalance across directions
   and starting angles before treating the controller as reliable.

Detailed evidence currently on the wheel PC includes
`HARDWARE_EVAL_2026-09-18.md`, `work/brake2200_spin6.csv`,
`work/coast_spin7.csv`, their analyzers, and earlier pulse/replay tests.
