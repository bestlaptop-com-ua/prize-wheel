# Post-fix hand spins - 2026-09-21 afternoon (Claude)

Owner fixed slip at jaw coupling hub / 8 mm adapter / motor shaft (Loctite 648, retightened), made the wheel lighter and slightly less balanced. Board power-cycled; AS5600 AGC 90 (was 76), MAGNITUDE 2069, MD=1.
All pre-fix encoder-vs-wheel conclusions (incl. the morning "INL" curve) are void: the slip point sat between encoder (motor rear stub) and wheel.

## Coasts captured (phase-audit-readonly build, motion disabled, 1 kHz, session.raw + serial.raw re-dump)
| spin | dir | travel | note |
| 15:14 | ccw | 402 deg | died 118 deg uphill of rest, swung back ~170 deg, settled |
| 15:47 | cw  | 2251 deg | crested, stopped cleanly |
| ~16:30 | cw | 553 deg | died uphill, swung back ~170 deg, settled (recovered via D after logger exit) |

## Model fit  A'' = -sgn(w)(c + b|w|) + G sin(A - phi), label frame
- 15:14 alone: G=0.574 phi=330.7 rest=150.7 c=0.158 b=0.036 rms 1.27 deg
- 15:47 alone: G=0.577 phi=333.2 rest=153.2 c=0.188 b=0.010 rms 0.75 deg
- joint (shared G,phi): G=0.574 rad/s^2, phi=330.7 deg, rest=150.7 deg; ccw c=0.159 b=0.035; cw c=0.185 b=0.011; rms 1.06 deg
- Board at rest reports angle 155.65 (wedge 5): matches the fitted rest angle.
- Residual 2x/rev after the fit: 0.7 deg (ccw) / 1.1 deg (cw), same raw-frame phase in both -> stationary in encoder frame (sensor or fixed coupling misalignment). Left at zero in firmware until a powered step-vs-raw sweep.

## Consequences
- G/c ~ 3.3 (was 3.3 before too, but both doubled: lighter wheel). Static hold cone asin(c/G) ~ 16 deg around rest.
- A coast that dies within the last uphill half-rev does NOT stop there: it swings back and settles within ~16 deg of the rest angle (150-155 deg = wedge 5 in the 12-wedge frame = a DARE). 2 of 3 spins today did exactly that.
- The gravity-aware predictor's "first zero crossing" must become bimodal: crest -> continue; no crest -> settle near rest. Takeover must always catch before the final crest and hold at the target with current on.
- rawZero=88 predates today's coupling work: verify pointer-vs-wedge before trusting wedge numbers.

Proposed G-line (INL zero): G0.574,330.7,0,0,0,0
