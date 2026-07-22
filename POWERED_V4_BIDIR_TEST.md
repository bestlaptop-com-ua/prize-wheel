# Powered random capture v4: bidirectional test

This revision follows the first powered test log. Counterclockwise capture landed within about 0.1-1.6 degrees, while clockwise runs reversed under load. V4 adds a continuous-run direction probe, a falling-speed takeover gate, and a conservative clockwise torque profile.

## Changes under test

- `p` now tests both FastAccelStepper continuous commands: `runForward()` and `runBackward()`.
- Powered control remains locked until both directions move the encoder in opposite directions and the result is persisted.
- Takeover requires a confirmed falling-speed trend and at least a 0.045 rev/s drop from the observed peak. This prevents engagement while a guest spin is still accelerating.
- Counterclockwise (`dir=-1`) retains the successful profile: 350 mA entry, 700 mA run, 0.22 rev/s cruise.
- Clockwise (`dir=+1`) uses 450 mA entry, 950 mA run, 0.16 rev/s cruise.
- `POWER_START` now logs the actual FastAccelStepper command sign as `fasDir`.

## 1. Flash and verify lockout

Open Serial Monitor at 115200 baud and reset the ESP32.

Expected startup includes:

```text
# EXPERIMENT mode=POWERED_RANDOM_V4 ...
# V4 CONTROL LOCKED: run bidirectional p probe before powered spins
```

The old one-direction calibration is deliberately invalid for this build.

## 2. Run the bidirectional probe

Move the pointer to the center of safe wedge 3 or 7-11. Keep hands clear and send:

```text
p
```

Expected sequence:

```text
# BIDIR_PROBE START ...
# BIDIR_PROBE FWD_START command=runForward
# BIDIR_PROBE FWD_RESULT movedDeg=... encoderSign=...
# BIDIR_PROBE REV_START command=runBackward
# BIDIR_PROBE REV_RESULT movedDeg=... encoderSign=...
# BIDIR_PROBE PASS runForwardEncoder=... runBackwardEncoder=... returnErrorDeg=...
```

The two encoder signs must be opposite. Stop if either movement is violent, exceeds roughly one-third of a wedge, or the belt/motor skips.

Send `s` and confirm:

```text
zero=1 dir=1 bidir=1 tmc=1 observeOnly=0
```

`dir=-1` is also valid.

## 3. First clockwise spin

Send `v` to enable detailed control output. Make one moderate clockwise spin.

Check:

- no `DECISION` while speed is still rising;
- `POWER_START dir=+1`;
- `fasDir` agrees with the probe mapping;
- `entryMa=450 runMa=950 cruiseRevS=0.160`;
- no physical reversal;
- final `targetWedge` is not 1 or 5;
- `recovery=0`.

Stop immediately on `FAULT`, reversal, grinding, belt jump, or skipped steps. Send `r` to release a fault hold.

## 4. One counterclockwise confirmation

Only after the clockwise test completes safely, make one moderate counterclockwise spin.

Expected profile:

```text
POWER_START dir=-1 ... entryMa=350 runMa=700 cruiseRevS=0.220
```

This should preserve the previous accurate counterclockwise behavior.

## 5. Upload

Save the complete session from boot through both spins as:

```text
powered_v4_test_01.txt
```

Commit it to the same experiment branch. Do not perform a larger run until the probe and both first spins are reviewed.
