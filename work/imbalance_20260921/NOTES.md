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

## 2026-09-21 17:10 - flashed and configured (Claude)
- Rebuilt with NUM_WEDGES 18, dare_mask 3/8/13/16 (owner: 8 is the hard one), z help text 17|0. Flashed to COM7 (upload exit 0), boots as imbalance-model-20260921.
- Encoder magnet re-seated by owner after AGC hit 128 / ML=1 at 16:57; now AGC 34, MAGNITUDE ~2090, MD=1 ML=0 MH=0.
- `z` with pointer on the 17|0 line: rawZero=2376 persisted.
- `G0.574,171.8,0,0,0,0` persisted (phi converted from the rawZero=88 frame: 330.7 + (2376-88)*360/4096). Rest angle 351.8 deg = wedge 17 next to the 0 line; crest ~171.8 deg = wedge 8 (the hard dare) - a coast that dies within ~16 deg of the crest can balance on 8/9, otherwise it swings back to 17/0.
- Fault latch SELFSPIN_ABORT (15) left in place; takeover OFF; no motor use (Loctite 648 curing until 2026-09-22).

## 2026-09-22 morning (Claude) - party day
- 09:03 build: recovery nudge + shadow caps -> 9/9 controlled. Frozen as work/PARTY_CANDIDATE_20260922 (still had the WRONG dare mask, see below).
- 09:17 headroom test at 2240 mA: 14/14 controlled, no slip -> >=20 % torque margin. Party runs at 2800.
- 09:57 wheel labels are 1-18 (photo); firmware indices = label-1. Owner dares by label 3/8/13/16 -> indices 2/7/12/15 (was wrongly 3/8/13/16). Fixed. Monitor prints "labels".
- ~10:10 owner counterweighted the wheel (weight on label 18, opposite the heavy spot at label 9). Rest positions now scatter (210/229/337/359 deg) => residual G < static friction. Gravity model CLEARED (G0,0,0,0,0,0); uphill gate therefore inactive; firmware runs the plain policy.
- takeoverEnabled now persisted in NVS (key "takeover").
- 11:50 shadow-mode margin: runway natural-6 (was -2), plan check remaining <= 1.03*natural+3 (weak spins were refused as "plan infeasible").
- Serial monitor for shared use: work/monitor/monitor.py (console window on MILL-PC; Claude sends via cmd.txt; log monitor.log).
- Open: rough stops (decel spikes 1.4-3.2 rev/s^2 in summaries) and target clustering (17/18, 3/4) on same-strength spins from the same rest. 1 kHz stop trace being captured.

## 2026-09-22 evening (party build) - Claude
- LED strip: WS2815 12 V, data GPIO40 -> 74AHCT125 pin2, pin3 -> DI (first pixel BI -> GND). Was dark because DI was open; fixed at bench.
- Audio: DFPlayer replaced by I2S sample player (pw_audio_i2s.h + pw_samples.h, 6 tracks decoded from media/mp3 @22.05 kHz mono, 773 KB in flash).
  PCM5102A on BCK15/LRCK16/DIN17 is set LEFT-JUSTIFIED (MSB); 32-bit slots (BCK 64fs). TPA3116 on 12 V (24 V went silent), 100 kOhm series into LIN, input GND wired. Volume 30 = gain 1.0, live `V<n>`, `P` plays fanfare.
- SUSTAINED_SPEEDUP: noise 0.05 -> 0.15 rev/s, 400 -> 800 ms; auto-clear via 'r' path after 3 s rest (only this fault code).
- Root cause of all "capture arming prerequisites" / "plan infeasible" abandons: encoder speed jitter puts the launch tick at 0.40x > CAPTURE_MAX_WHEEL 0.40. +0.05 tolerance in launchCapture AND prepareCapturePlan. Prereq failures now print which check failed; driver health re-reads SPI up to 2x.
- LEDs: spin = original rainbow bands; landing celebration slams the landing wedge colour (palette by label in pw_party_impl.h).
- Re-zeroed 18|1: rawZero 2376 -> 1947 (something shifted ~38 deg during the day).
- Open: speaker silent under party firmware while playlist test build plays - hardware vs firmware split pending; friction fit rejected (bounds/contact) on the last spins -> seeds used.
