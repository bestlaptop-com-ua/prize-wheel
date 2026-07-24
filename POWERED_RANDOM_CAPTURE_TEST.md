# Powered random-capture bench test

This experiment is intentionally different from the brake-only build. After a
confirmed hand spin slows through about 0.28 rev/s, the motor takes control and
may actively drive the wheel forward through its imbalance to a random safe
wedge center.

## Control limits

- original spin direction only; no commanded reversal;
- random target excludes wedges 1 and 5;
- target is 130-240 degrees ahead at the highest takeover speed;
- 50 ms at 100 mA precharge;
- 350 mA entry current, rising to 700 mA after 160 ms;
- 0.22 rev/s powered cruise ceiling;
- 800 microsteps/s^2 command slew;
- encoder-distance deceleration and 3.5 degree stop lead;
- stall, opposite-motion, encoder-stale, and 8-second timeout faults hold the wheel until `r` is sent.

## Before the first spin

1. Flash the complete sketch folder from `experiment/powered-random-capture`.
2. Open Serial Monitor at 115200 baud.
3. Confirm the startup line contains `POWERED_ALWAYS_CAPTURE_RANDOM`.
4. Send `s` and confirm `zero=1`, `dir=+1` or `-1`, `tmc=1`, and `observeOnly=0`.
5. Send `v` to enable `# POWER` telemetry.
6. Keep a hand on the power switch. Keep hands and loose objects away from the wheel.

## First attended test

Make one moderate spin in the same direction used for the previous screenshot.
Do not test a very strong throw first.

Expected sequence:

```text
DECISION ... action=STEER ... reason=always-random
POWER_PRECHARGE ... targetWedge=<not 1 or 5>
POWER_START ...
# POWER ...
POWER_STOP reason=target-approach ...
PRIMARY_STOP source=DRAG ... isDare=0
LANDED ... isDare=0 ... recovery=0
```

A short period where commanded speed is greater than measured wheel speed is
expected: that is the new powered behavior required to carry the heavy side of
the wheel uphill.

## Stop immediately if

- the wheel reverses;
- takeover produces a violent jerk;
- the belt jumps or the motor audibly skips;
- speed rises uncontrollably;
- any `FAULT` line appears;
- the motor or driver becomes unusually hot;
- the wheel reaches a dare and recovery is needed on either of the first two tests.

Send `r` after a fault to release holding torque. Send `o` only while idle to
return to motor-free observation mode.

## Initial acceptance

Do only two spins in one direction, then review the complete serial log. Do not
continue to the opposite direction until target approach, stopping accuracy,
and motor behavior are checked.
