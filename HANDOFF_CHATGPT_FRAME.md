# HANDOFF: Wheel angle + direction detection (SOLVED - do not reinvent)
For: ChatGPT / Codex sessions working on this repo.
Status: VERIFIED WORKING on hardware, 2026-07-29. Owner-attended tests passed.
Code of record: prize_wheel_gpt/prize_wheel_gpt.ino (branch main).

## 1. The frame, in one line
wheelAngleDeg = ((rawZero - AS5600_raw) mod 4096) * 360 / 4096
where rawZero is a uint16 CONSTANT of this wheel's mounting (currently 3807),
persisted in NVS (namespace "prizewheel", key "rawZero", default 3807).

That is the ONLY source of wedge identity. It is a pure function of the
AS5600 absolute register and one persisted number. Nothing to accumulate,
nothing to corrupt, nothing that depends on boot order. That is WHY it
survives resets, brownouts, watchdogs, serial-monitor DTR pulses, and
reboots mid-spin: there is no state to lose.

## 2. Physical meaning (owner-verified by full label walk)
- Label N occupies [30*N, 30*N+30) degrees. Labels are painted 0..11
  sequentially: 0 green, 1 orange (DARE), 2 blue, 3 red, 4 yellow,
  5 purple (DARE), ... 11.
- The rim calibration screw sits at the 11|0 boundary = 0.00 deg.
- rawZero = the AS5600 raw reading when label 0's leading edge is under
  the pointer. Measured 3807 for this wheel on 2026-07-28.
- Direction: raw DECREASES as wheel angle increases. The subtraction
  (rawZero - raw) embodies the old ENCODER_DIR_SIGN = -1. Do not add
  another sign anywhere.
- dare_mask = (1<<1)|(1<<5) is CORRECT in this frame. Verified.

## 3. How the multiturn layer stays honest
encoderCountsMT exists only for velocity/ballistics. Its invariant:
  encoderCountsMT mod 4096 == staticCountsFromRaw(raw) at all times.
Achieved by two rules in primeEncoder():
  - fresh prime:            encoderCountsMT = staticCountsFromRaw(raw);
  - nearest-turn re-prime:  candidate = wholeTurnBase + staticCountsFromRaw(raw);
Runtime accumulation (ENCODER_DIR_SIGN * delta) moves in the same direction
as the static frame, so seed and accumulation agree. wheelAngleDeg() is
fmod(angleDegMT, 360) and is therefore label-true by construction.
A blind I2C gap can at worst miscount whole turns (ballistics error);
it can NEVER change which wedge is which.

## 4. The z command (re-anchoring)
Protocol: rotate the wheel so label 0's LEADING edge is exactly under the
pointer (the rim screw at the 11|0 line is the physical reference), send z.
It stores the current raw as rawZero (preferences.putUShort("rawZero", ...))
and re-primes. One uint16, valid forever, valid across reboots.

## 5. The historical bug you must not reintroduce (this is what used to
## cause "direction inversion" and silent 33/78/150/255-degree jumps)
The old design seeded the frame at boot with +raw but accumulated at
runtime with -delta, and stored the zero as a SESSION multiturn double
(angleDegMT at z-press). Consequences, all observed live on 2026-07-28/29:
- after any reset at a moved position the frame MIRRORED around the boot
  point (wheel at true 0.00 read 143.17);
- every re-prime after a blind gap injected a discrete jump of ~2x the
  motion during the gap;
- the persisted zero (-630.44) was meaningless outside the session that
  wrote it.
If you ever see steering that goes the wrong way, wedges off by a fixed
amount after a reboot, or discrete angle jumps: suspect that someone
re-derived wedge identity from an accumulator or re-seeded with bare raw.
The fix is always: return to the pure static map of section 1.

## 6. Verification protocol (run after ANY change near the encoder path)
1. Park label-0 leading edge under pointer: display must read ~0.00, wedge 0.
2. RTS-reset 5x without touching the wheel: identical reading every boot.
3. Hand-walk all 12 wedges in both directions: wedge number matches label.
4. Rotate to a random wedge, reset, confirm identity unchanged.
5. Spin and reset mid-spin: after reboot the angle is immediately correct.
All five passed on 2026-07-29 (owner-attended).

## 7. Current build state (context, not gospel)
prize_wheel_gpt = the 6ff8d5d full build (disguised takeover + tuned
currents) with: the frame fix above; universal engagement (every confirmed
spin steered, prediction logged but not gating); hand-release gate (spin
age >= 500 ms and speed < 92% of peak); dare recovery disabled by owner;
brake-physics-honest targeting (this build's takeover is BRAKE-ONLY - it
can shave distance, never add - so targets are chosen inside
[min_brake_distance + 15 deg, natural_stop_distance - 5 deg]).
Known open items: landing scatter vs target ~1 wedge (tune sync/brake),
TK-SKIP chatter while geometry aligns (cosmetic), landing-distribution
audit over many spins still pending.

## 8. Hard rules
- Wedge identity comes from section 1. Never from encoderCountsMT alone,
  never from a prediction, never from a freshly-read raw without rawZero.
- Never write NVS keys "wedge0"/"wedge0_ok" again; they are retired.
- rawZero is sacred; only z (owner-attended) changes it.
- Both spin directions are trustworthy now. The old one-direction distrust
  was a symptom of the mirrored frame, not of wiring.
