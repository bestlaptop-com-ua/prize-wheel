# Hand-spin encoder audit - 2026-09-21 (Claude, via roam)

Firmware: phase-audit-readonly-20260920 (motion disabled, EN high). Owner hand-spun the wheel; `d` capture, 16384 rows @1 kHz, re-dumped with `D` into serial.raw.

## Data quality
- 0 I2C slow reads, 0 dt glitches, 0 single-sample jumps, firmware counts == raw unwrap for all rows. The encoder data path is clean.
- Magnet at rest: MD=1 ML=0 MH=0, AGC=76, MAGNITUDE=2083.

## Model fit of the free coast (2.5 rev, 0.26 rev/s -> stop)
theta'' = -c0 - c1*omega + G*sin(theta-phi), theta=0 at raw 37
- friction + gravity imbalance only: residual 1.03 deg rms, clearly periodic
- plus physical 2x/rev torque (coupling bind): 0.47 deg rms
- plus encoder INL (1x + 2x harmonics of angle): 0.14 deg rms; with both allowed the 2x torque term goes to ~0
- Encoder INL: 2x/rev amplitude 1.5 deg, 1x/rev 0.6 deg, 3.8 deg peak-to-peak (max error near raw 3448, min near raw 2352)
- Imbalance: G = 0.25 rad/s^2, stable rest at raw ~976 = label angle ~282 deg (matches every at-rest status: 280.6-287.7). c0 = 0.076 rad/s^2, c1 = 0.008 1/s.

## Cross-check against 2026-09-20 powered self-spin traces (capture_v2r2)
The INL curve from this unpowered spin predicts the step-count-vs-encoder discrepancy in both directions:
dip of about -1 deg at 45 deg travel, zero crossing ~85 deg, +2.0 deg at 135 deg (measured +2.03 / +2.65; remainder is genuine load angle while accelerating).
=> The apparent 1.4-1.9 % "slip" is AS5600 nonlinearity, not shaft/coupling slip and not lost steps.

## Consequences
- 2x error of 1.5 deg amplitude = +/-5 % false speed ripple; on top of a real +/-24 % speed swing per rev at low speed from the imbalance. Both feed the speed-up / travel-limit detectors.
- Fix order: (1) balance the wheel (counterweight at top when pointer reads ~282 deg), (2) re-seat magnet: centred, non-magnetic spacer between steel stub and magnet, (3) harmonic/LUT correction in firmware from a slow powered full-rev calibration (steps vs raw, both directions), NVS-stored.
- Also seen: TMC5160 GSTAT=05 (reset+uv_cp), CHOPCONF at power-on default during this audit - VM was off or browned out since boot.
