# Always-capture random-safe experiment

Branch: `experiment/always-capture-random`

This build ignores the natural-landing predictor. Every confirmed spin that reaches the normal decision-speed window is captured and assigned a random reachable safe wedge center. Wedges 1 and 5 are never candidates.

The random choice is limited to safe wedges reachable in the original spin direction with the existing braking envelope:

- no reversal;
- minimum runway derived from the current measured speed and the 650 steps/s² slew limit;
- maximum powered runway 180 degrees;
- target is the wedge center.

This means the choice is random among currently reachable safe wedges, not uniformly random among all ten safe wedges on every individual spin.

## Before testing

1. Flash the complete `prize_wheel` folder from this branch.
2. Open Serial Monitor at 115200 baud.
3. Send `s` and confirm `zero=1`, `dir=+1` or `-1`, and `tmc=1`.
4. On boot, confirm this line appears:

```text
# EXPERIMENT mode=ALWAYS_CAPTURE_RANDOM target=random-reachable-safe
```

5. Keep a hand near the motor power switch.

## Test sequence

1. Start with a moderate spin in one direction.
2. Confirm the decision line contains:

```text
action=STEER ... reason=always-random
```

3. Confirm `targetWedge` is never 1 or 5.
4. Confirm `DRAG_START dir=` matches the original `SPIN START dir=`.
5. Stop immediately on reversal, visible acceleration, violent engagement, step skipping, or any `FAULT` line.
6. Run five spins in that direction, then five in the opposite direction.
7. Save the complete serial log.

## Useful commands

- `s` — status
- `v` — toggle detailed drag logging
- `o` — toggle motor-free observation mode
- `e` — toggle emergency post-stop recovery
- `r` — release a fault hold and return to freewheel

After testing, compare `targetWedge`, `PRIMARY_STOP`, `LANDED`, and `targetErrorDeg` for each spin.
