# FINDINGS 2026-07-28 - Motor-off frame forensics (owner + assistant bench session)

## Instrument
pw_diag/pw_diag.ino (v3, in this commit): standalone motor-off sketch, AS5600 raw
read every 5 ms, STATIC absolute map (no accumulator, no NVS dependence for the frame).
Anchor: RAW_SCREW = 3807. NOTE: the board currently runs THIS sketch, not the main fw.

## Verified facts
1. AS5600 + magnet + mounting are GOOD. Owner-attended: all 12 wedges correct in both
   directions, stop-on-random-wedge + reset -> correct, reboot mid-spin -> correct.
   Absolute readout trustworthy across the full circle. (Motor off; EMI not exercised.)
2. Label-true physical frame (the reference from now on):
   - rawZero = 3807 counts = leading edge of label 0 = 0.00 deg. Rim screw sits at the
     11|0 boundary = 0 deg. Photo-era parking raw 3450 = 31.4 deg (the 0|1 boundary).
   - Angle increases as raw DECREASES: wheelAngleDeg = norm((3807 - raw) * 360/4096).
     Consistent with ENCODER_DIR_SIGN = -1 in main fw.
   - Labels are sequential 0..11, label N spans [30N, 30N+30):
     0 green, 1 orange, 2 blue, 3 red, 4 yellow, 5 purple, ...
   - dare_mask = (1<<1)|(1<<5) is CORRECTLY aligned with physical labels 1 and 5.
     Verified via full label-mapping walk. (A mid-session off-by-one alarm was a false
     alarm caused by mistaking a rim boundary peg for the calibration screw.)
3. The MAIN FIRMWARE frame does NOT survive resets, even with a valid NVS zero:
   - Observed live: wheel parked at true zero; fw-style frame displayed 143.17 deg.
   - Mechanism A (code, definite): prime seeds encoderCountsMT = +raw (~line 586) while
     runtime accumulates ENCODER_DIR_SIGN*delta = -delta (~line 897). Any reboot at a
     moved position MIRRORS the frame around the boot point; every re-prime after a
     blind I2C gap (preserveNearestTurn snaps to wholeTurnBase + raw) injects a discrete
     jump of ~2x the raw motion during the gap. This predicts exactly the silent-jump
     phenomenology (discrete, non-accumulating, variable magnitude: 33/78/150/255 class)
     that FRZ-EVT was built to capture.
   - Mechanism B (data, definite): the z command stores wedge0 = angleDegMT, a SESSION
     multiturn double (currently -630.44 in NVS). It is only meaningful inside the boot
     session where z was pressed; it cannot reproduce the calibrated frame after any
     reset even when it loads intact.
   - CAVEAT to AGENT_NOTES 2026-07-27 "ZERO VALID (07:56)": that zero is true only
     within an unbroken session. Under current fw, do NOT trust wedge identities after
     any reset (WDT, brownout, RTS, monitor attach) until re-zeroed.
4. NVS load anomaly (unresolved, keep monitoring): diag v1 used READ-ONLY
   Preferences.begin and produced effective zero == 327.04 on one boot and default-0 on
   another, same untouched key. v2/v3 use read-write begin (like main fw) and read
   -630.44 consistently across many boots. Suspect the read-only open path or a
   corrupted first read. Main fw loads read-write, so likely unaffected - unproven.

## Recommended fix (single commit, NOT yet implemented)
- Persist the anchor as uint16 rawZero (=3807) in NVS instead of the session double.
- wheelAngleDeg = norm((rawZero - raw) * 360/4096): static, absolute, reboot-proof,
  label-true. Wedge identity can never be corrupted by boot order or blind gaps.
- Keep a multiturn layer ONLY for spin ballistics, derived by counting wraps of the
  static angle; a blind gap can then at worst miscount turns, never wedge identity.
- z command becomes: park label-0 leading edge at pointer, z stores current raw.
- dare_mask unchanged.

## Ops notes (autonomous sessions)
- Board currently runs pw_diag v3, NOT the main firmware. Reflash before any campaign.
- Arduino IDE Serial Monitor resets the ESP32 (DTR/RTS) on every open.
- MCP-detached processes: Start-Process children die when the MCP call returns (job
  object). Use Invoke-CimMethod Win32_Process Create. Build pattern in
  pw_diag build usage: full-path IDE-bundled arduino-cli, *> logfile, EXIT=$LASTEXITCODE
  marker line, poll the log with short calls.
- pw_serial stop-latch (%TEMP%\pw_serial_stop) was used tonight to keep the bridge off
  COM3 while the owner used the IDE; removed again at session end.
