# Flash 12 (2026-09-30): wedge-uniform pick restored, every landing balances

Owner, 9/30 09:25: "It seems like it prefers some wedges and avoid other. I've never seen 1 and 10 for example."

## What the logs showed

| Build | Spins | Pass-1 (wedge-uniform) picks | Spread of landings |
|---|---|---|---|
| Flash 10 (g = 1.0) | 46 | 18 | 1–6 per safe label; chi-square p = 0.83, fair |
| Flash 11 (g = 1.4) | 30 | 0 | 26 of 30 on dare-adjacent labels (2/4/7/9/12/14/15/17); 6/10/11 never |

## Cause

Flash 11 divided both ends of the pass-1 window by brakeGain. With the fixed latency and shave terms, the window closed at every catch speed: −2 to −19°, cw and ccw.

Every spin then fell through to one of two paths:
- **The shadow firm tier.** It always took the first acceptable spot after the last dare.
- **Pass-3 hard brakes.** 7 of 30 spins. Spin 30 slipped 10.5° at 1.8× natural.

## Changes

1. **Pass-1 window.**
   - Top: the true natural coast again.
   - Floor: coupling at 1.05 × min(g, 1.3) × natural decel, and ≥ natural/min(g, 1.3). Pass 1 is never firmer than the firm tier.
   - Plan check: min(g, 1.3) × natural.
   - Exact-stop cap: min(min(g, 1.3) + 0.25, 1.6) × natural. Flash 10 was 1.25.
   - Resulting window widths:

     | Direction | 0.25 | 0.30 | 0.35 | 0.40 rev/s |
     |---|---|---|---|---|
     | cw | 21° | 32° | 43° | 56° |
     | ccw | 18° | 28° | 39° | 51° |

2. **Deficit balancing** counts every real landing (in closeSpin). It skips re-spins, faults and control locks, and still lives in RAM only.
3. **Shadow firm tier.** It now makes a deficit-weighted wedge choice among all acceptable landings, then a random 0.5° grid point inside that wedge. Before, it always took the firmest spot. The checks on each candidate are unchanged.

Passes 3/4/5 and all dare-safety tests are untouched. A selection simulation using the friction model (cw/ccw, catch speeds 0.25–0.40) gives these results:

| Build | Share per safe label | How targets are chosen |
|---|---|---|
| Flash 11 | 1, 5, 6, 10, 11, 18 at about 1% each; dare-adjacent labels at 8–13% | — |
| Flash 12 | 6.9–7.9% each | 91% pass 1, 7% firm tier |

## Rollback without a flash

Press '−' at rest until brakeGain reads 1.0. That restores flash-10 behaviour: floor 1.05, check 1.0, firm tier off, cap 1.25.

## Review

An independent agent reviewed this change and gave GO with changes. All four findings were applied: the pass-1 firmness floor, the exact-stop cap, counting real landings only, and the n = 0 guard.
