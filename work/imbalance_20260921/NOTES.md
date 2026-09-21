# imbalance_20260921 - gravity imbalance + AS5600 INL model (Claude)

Base: work/production_review_20260920 (plywood-capture-review-20260920, takeover defaults OFF).
Build string: imbalance-model-20260921. Compiled only (exit 0, +15 KB flash, +2.7 KB RAM, no new warnings). NOT flashed.
Rebuild the candidate with apply_imbalance.py (19 exact-once anchors; aborts if any anchor drifts).

Why: owner decision 2026-09-21 - the heavy spot stays, firmware models it. See ../handspin_20260921/FINDINGS.md.

What changes (all inert until a model is stored; zero model == legacy behaviour, bit for bit):
- pw_imbalance.h: PwEncoderInl (raw-frame harmonic LUT), PwGravity (label-frame A'' = g sin(A - phi)), NVS helpers, `G` line parser. Pure logic, host-tested (host_test/).
- encoderCountsMT = uncorrected accumulator + INL offset; rawZero stays in the sensor frame. Diagnostic `raw` stays uncorrected, `counts` is corrected.
- naturalStopDistanceDeg / brakedStopDistanceDeg integrate friction + gravity from the CURRENT angle (closed form for whole revs, 5 deg midpoint steps for the tail, 10 ms trajectory cache). Natural reach is evaluated at 96 % speed: with g > c the last crest is a cliff - a 1 % speed error can move the natural stop by most of a revolution.
- naturalDecelRevS2: energy-average over the natural runway, floored at 25 % of friction (the instantaneous value crosses zero every revolution).
- Online friction fit: gravity's speed contribution is removed per sample pair so c/b stay friction-only.
- Re-push test: also requires mechanical energy above the release energy (a free coast can out-run its peak on the heavy side, it cannot gain energy).
- PwSpeedupWatch is untouched: it runs under motor control, where the stepper sets the speed.
- Serial: `g` prints the model; `G<g>,<phiDeg>,<a1>,<b1>,<a2>,<b2>` + Enter sets it (at rest, IDLE/FAULT only; all zeros clears).

First-spin estimate (one ccw coast, 2026-09-21): G0.2509,101.93,-4.78,5.04,-15.65,-7.68  (rest angle 281.9 deg; c=0.0756 b=0.0082 ccw)
Check: predictor gives 901.2 deg from the start of that coast, actual 901.1; legacy closed form gives 952.6.

Open items: more spins both directions before storing constants; INL should be re-derived from a slow powered full-rev calibration; nothing here has run on the wheel.
