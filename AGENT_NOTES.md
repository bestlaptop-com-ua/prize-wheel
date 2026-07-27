# AGENT_NOTES — rolling resume state (branch claude/adaptive-v2)

Purpose: a restarted session can read this and continue. Newest at top.

## >>> OWNER ACTION REQUIRED — ONE ACTION UNBLOCKS EVERYTHING (re-confirmed Session 27, 09:27; blocked ~61 min) <<<
The encoder frame is out of sync with the physical wheel by ~156° and the mission cannot validly advance
until you re-align it. Sessions 4–10 have independently verified this; Session 10 re-derived it FROM SCRATCH
off the raw logs (see the decisive numbers below), so it is not an inherited assumption — it is sound.
Motor work is HALTED; board is still on the validated latch-fix firmware (429e2a5), NOT reflashed.

WHAT'S WRONG (short version): the wheel landed on ~wedge 10/11 at 08:25:56 and has not physically moved
since. But the firmware/encoder now reads wedge 4 (angle 134.65) and has held that rock-stable for 30+ min.
So the encoder is ~156° off from where the wheel actually is.
DECISIVE EVIDENCE (Session 10, first-hand): in the 08:26→08:30 gap the encoder angle moved 338.55°→134.65°
(Δ≈156°) while the CALIBRATED camera (scale 0.837, fit resid 0.29°) stayed within a 0.18° span across 3918
frames — i.e. the wheel physically did NOT move — and there was NO reboot banner and NO `z` re-zero in that
gap. A stable, FRESH/VALID absolute reading that is 156° wrong with the wheel still = the AS5600 magnet has
PHYSICALLY SLIPPED ON ITS HUB. This is a hardware slip, not a firmware glitch, so a board reboot will NOT
fix it and I cannot fix it from software. Because the encoder is the ONLY absolute position sense, every
wedge/dare identity is currently untrustworthy — a firmware-"safe" landing could be a physical DARE. That
is why I will not spin. IMPORTANT: a re-zero ALONE won't hold — please snug the magnet hub set screw first
(step 3), or it can slip again mid-party and silently break the dare guarantee during your reveal.

  >>> THE ONE ACTION THAT UNBLOCKS ME (≈60 s, same re-zero as 07:54 + a set-screw snug): <<<
  1. (5 s, helps confirm) Glance at the wheel and note which wedge number is physically under the red
     pointer. Expected: ~wedge 10/11 (that confirms the encoder slipped). Jot it here if you can.
  2. (Required — do this FIRST, before re-zeroing) The AS5600 magnet hub on the shaft has slipped ~156°.
     Snug its set screw (and nudge-check the sensor mount for looseness) so it can't slip again during the
     party. If it feels already tight, still re-seat it — the evidence says it moved.
  3. (Required) Rotate the wheel so the bright rim screw sits under the red pointer, then — with the spin
     loop idle — send `z` ONCE. Verify `s` then reads wedge-3 center ≈ 103.5° (as on the 07:54 re-zero).
     This physically RE-ESTABLISHES encoder==wheel by construction.

  After you leave an owner note OR a new `# wedge-0 boundary set` appears in the serial log (dated after
  08:46), the next session will re-validate encoder==camera with one tiny attended cam-cal move and resume
  the T3 baseline. Until then, sessions will keep safely no-op'ing — nothing is lost by the wait.

Full three-way evidence (encoder jumped / camera flat / mic silent, no reboot/RTS/re-zero in the gap) is
in the Session 4 & 5 records below and in CLAUDE_VARIANT.md. This is a candidate hardware wall (invariant
#7); the re-zero above is the simplest fix and resolves it regardless of root cause.

## 2026-07-27 SUPERVISOR v2 SESSION 27 (~09:27) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep = lines 4110/4381/7461 → 07:24/07:46/07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:27:23.575 reply): sensor_pos=FRESH velocity=VALID age_us=65 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–26 — encoder frozen at 134.65
  (~61 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed. Did NOT
  re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Instruments live: bridge ALIVE (last cmd answered), no stop file; cam ALIVE, flat at −5423.66 (no physical
  move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in
  tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
- NOTE FOR HUMANS: this is the 18th consecutive identical no-op session (10→27), ~61 min blocked on a single
  ~60 s owner action. Relaunches add nothing until the owner re-zeros (or declines). Nothing is lost by the
  wait; nothing advances without it. If the owner is unavailable, the loop can safely be paused.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:27:23: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–27.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 26 (~09:25) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep = lines 4110/4381/7461 → 07:24/07:46/07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:25:55.523 reply): sensor_pos=FRESH velocity=VALID age_us=49 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–25 — encoder frozen at 134.65
  (~60 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed. Did NOT
  re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Instruments live: bridge ALIVE (last cmd answered), no stop file; cam ALIVE, flat at −5423.66 (no physical
  move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in
  tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
- NOTE FOR HUMANS: this is the 17th consecutive identical no-op session (10→26), ~60 min blocked on a single
  ~60 s owner action. Relaunches add nothing until the owner re-zeros (or declines). Nothing is lost by the
  wait; nothing advances without it. If the owner is unavailable, the loop can safely be paused.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:25:55: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–26.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 25 (~09:24) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep = lines 4110/4381/7461 → 07:24/07:46/07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:24:14.826 reply): sensor_pos=FRESH velocity=VALID age_us=65 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–24 — encoder frozen at 134.65
  (~58 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed. Did NOT
  re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Instruments live: bridge pid 30212 ALIVE, no stop file; cam ALIVE, flat at −5423.68 (no physical move,
  consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in tree.
  LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
- NOTE FOR HUMANS: this is the 16th consecutive identical no-op session (10→25), ~58 min blocked on a single
  ~60 s owner action. Relaunches add nothing until the owner re-zeros (or declines). Nothing is lost by the
  wait; nothing advances without it. If the owner is unavailable, the loop can safely be paused.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:24:14: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–25.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 24 (~09:22) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep = lines 4110/4381/7461 → 07:24/07:46/07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:22:36.666 reply): sensor_pos=FRESH velocity=VALID age_us=45 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–23 — encoder frozen at 134.65
  (~56 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed. Did NOT
  re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Instruments live: bridge pid 30212 ALIVE, no stop file; cam pid 19992 ALIVE, flat at −5423.70 (no physical
  move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in
  tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
- NOTE FOR HUMANS: this is the 15th consecutive identical no-op session (10→24), ~56 min blocked on a single
  ~60 s owner action. Relaunches add nothing until the owner re-zeros (or declines). Nothing is lost by the
  wait; nothing advances without it. If the owner is unavailable, the loop can safely be paused.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:22:36: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–24.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 23 (~09:20) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:20:36.933 reply): sensor_pos=FRESH velocity=VALID age_us=499 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–22 — encoder still frozen at
  134.65 (~54 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam ALIVE, flat at −5423.71 (no physical
  move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in
  tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:20:36: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–23.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 22 (~09:18) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:18:46.808 reply): age_us=46 angle=134.65 wedge=4 omega=0 predStop=134.6(w4) mode=10 DONE
  (coils floating). IDENTICAL to Sessions 10–21 — encoder still frozen at 134.65 (~53 min stable since 08:26).
  Leading `????` on the line = supervisor's queued `?` probe bytes; status fields intact. Board answered `s` →
  silence timer reset, no RTS needed. Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam pid 19992 ALIVE, flat at −5423.71 (no
  physical move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate
  fix in tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:18:46: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–22.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 21 (~09:17) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:17:18.080 reply): sensor_pos=FRESH velocity=VALID age_us=435 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–20 — encoder still frozen at
  134.65 (~51 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam ALIVE, flat at −5423.71 (no physical
  move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in
  tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:17:18: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–21.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 20 (~09:15) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:15:49.303 reply): age_us=500 angle=134.65 wedge=4 omega=0 predStop=134.6(w4) mode=10 DONE
  (coils floating). IDENTICAL to Sessions 10–19 — encoder still frozen at 134.65 (~49 min stable since 08:26).
  Leading `?` garble on the line = supervisor's queued probe bytes; status fields intact. Board answered `s` →
  silence timer reset, no RTS needed. Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam pid ALIVE, flat at −5423.75 (no physical
  move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in tree.
  LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:15:49: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–20.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 19 (~09:14) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:14:10.562 reply): sensor_pos=FRESH velocity=VALID age_us=47 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–18 — encoder still frozen at
  134.65 (~48 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam pid 19992 ALIVE, flat at −5423.71 (no
  physical move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate
  fix in tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:14:10: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–19.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 18 (~09:12) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:12:30.328 reply): sensor_pos=FRESH velocity=VALID age_us=393 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–17 — encoder still frozen at
  134.65 (~46 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam pid 19992 ALIVE, flat at −5423.71 (no
  physical move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate
  fix in tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:12:30: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–18.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 17 (~09:10) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:10:49.992 reply): sensor_pos=FRESH velocity=VALID age_us=47 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–16 — encoder still frozen at
  134.65 (~45 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam pid 19992 ALIVE, flat at −5423.79 (no
  physical move, consistent with the block). Working tree: only this file modified; NO g/G tooling candidate
  fix in tree. LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:10:49: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–17.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 16 (~09:08) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:08:55 reply): sensor_pos=FRESH velocity=VALID age_us=720 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–15 — encoder still frozen at
  134.65 (~43 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge answering serial; cam flat at −5423.82 (no physical move, consistent with
  the block). Working tree: only this file modified; NO g/G tooling candidate fix in tree. LEGACY LATCH item
  remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:08:55: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–16.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 15 (~09:07) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep lines 4110/4381/7461 = 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:07:08 reply): sensor_pos=FRESH velocity=VALID age_us=382 angle=134.65 wedge=4 omega=0
  predStop=134.6(w4) mode=10 DONE (coils floating). IDENTICAL to Sessions 10–14 — encoder still frozen at
  134.65 (~41 min stable since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed.
  Did NOT re-derive the slip analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs).
- Verified instruments live: bridge answering serial; cam flat at −5423.82 (no physical move, consistent with
  the block). Working tree: only this file modified; NO g/G tooling candidate fix in tree. LEGACY LATCH item
  remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:07:08: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–15.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 14 (~09:05) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep: only 07:24 / 07:46 / 07:54). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:05:30 reply): age_us=45 angle=134.65 wedge=4 omega=0 predStop=134.6(w4) mode=10 DONE
  (coils floating). IDENTICAL to Sessions 10–13 — encoder still frozen at 134.65 (~39 min stable since
  08:26). Board answered `s` → supervisor silence timer reset, no RTS needed. Did NOT re-derive the slip
  analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs). Block holds.
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam flat at −5423.80 (no physical move,
  consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in tree.
  LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:05:30: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–14.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 13 (~09:04) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep: only 07:24 / 07:46 / 07:54). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:03:45 reply): sensor_pos=FRESH velocity=VALID age_us=47 angle=134.65 wedge=4 omega=0
  mode=10 DONE (coils floating). IDENTICAL to Sessions 10–12 — encoder still frozen at 134.65 (~37 min stable
  since 08:26). Board answered `s` → supervisor silence timer reset, no RTS needed. Did NOT re-derive the slip
  analysis (first-hand verified Sessions 4–10; Session 10 re-derived from raw logs). Block holds.
- Verified instruments live: bridge pid 30212 ALIVE, no stop file; cam flat at −5423.80 (no physical move,
  consistent with the block). Working tree: only this file modified; NO g/G tooling candidate fix in tree.
  LEGACY LATCH item remains CLOSED (429e2a5). No owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:03:45: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–13.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 12 (~09:02) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Both unblock signals still absent: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grep: only 07:24 / 07:46 / 07:54). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:01:53 reply): DONE age_us=522 angle=134.65 wedge=4 omega=0 mode=10 (coils floating).
  IDENTICAL to Sessions 10–11 — encoder still frozen at 134.65 (~35 min stable since 08:26). Board answered
  `s` → supervisor silence timer reset, no RTS needed. Did NOT re-derive the slip analysis (first-hand
  verified Sessions 4–10; Session 10 re-derived from raw logs). Block holds.
- Checked working tree: only this file modified (Session 11's uncommitted edit); NO g/G tooling candidate fix
  present (the OWNER PRIORITY note's uncommitted candidate is not in the tree — committed/reverted earlier).
  LEGACY LATCH item remains CLOSED (429e2a5). So there is NO owner-independent code work pending → correct no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:01:53: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5 = HEAD after commit). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–12.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 11 (~09:00) — STILL BLOCKED on owner re-zero; 1 `s` re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + one status read + handoff, as prescribed)
- Checked both unblock signals: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05 (grep:
  only 07:24 / 07:46 / 07:54 exist). (2) No owner note atop this file. Block unchanged.
- One fresh `s` (09:00:00 reply): sensor_pos=FRESH velocity=VALID age_us=773 angle=134.65 wedge=4 omega=0
  mode=10 DONE (coils floating). IDENTICAL to Session 10's 08:57:48 reading — encoder still frozen at 134.65
  (~34 min stable). Board answered `s` → supervisor silence timer reset, no RTS. Did NOT re-derive the slip
  analysis; it is first-hand verified across Sessions 4–10 (Session 10 re-derived from raw logs). Block holds.
- LEGACY LATCH open item remains CLOSED (429e2a5 = HEAD = flashed). No owner-independent code work pending;
  working tree clean apart from this docs edit. So this session correctly stays a no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 09:00:00: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera, calibrated).
- NO wheel/motor work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor
  spins used. Firmware UNFLASHED (429e2a5). Repo: docs-only edit (this file) on claude/adaptive-v2.
- Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT re-derive the
   analysis — verified across Sessions 4–11.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 10 (~08:57) — BLOCKED; block re-derived FIRST-HAND from raw logs; 0 spins, NO flash
### What I did (NO motor, NO flash — independent re-verification + sharpened owner ask)
- Checked both unblock signals: (1) NO new `# wedge-0 boundary set` after 07:54:05 (grep: only 07:24/07:46/
  07:54). (2) No owner note atop this file. Block unchanged.
- KEY DIFFERENCE vs Sessions 5–9: I did NOT trust the inherited narrative — I re-derived the block from the
  raw serial + calibrated camera logs myself:
    * Serial: encoder angle 338.55 (08:26:13) → 134.65 (08:30:32), Δ≈156°, stable at 134.65 through 08:57:48
      (fresh `s`: FRESH/VALID age_us=402, omega=0, mode=10).
    * Camera (cam_cal.txt: scale 0.837, sign +1, fit resid_rms 0.29°): across the 08:26:14→08:30:35 gap,
      3918 frames spanned only 0.18 cam-deg (−5423.68..−5423.86); still −5423.74 at 08:56:39. A real 156°
      wheel move = ~131 cam-deg. → wheel physically did NOT move.
    * No reboot: no boot/`rst:` banner in serial during the gap (08:30:30 lines are the supervisor `?`
      help-echo, board answered → no RTS); no `z` in the gap.
  → CONCLUSION (root cause, first-hand): the AS5600 magnet hub PHYSICALLY SLIPPED ~156°. A stable FRESH/VALID
    absolute reading that is 156° wrong with the wheel still and no reboot cannot be a firmware glitch. So a
    remote RTS reset would NOT unblock (magnet is where it physically is) — confirmed no owner-independent fix.
- Sharpened the top owner ask: promoted "snug the magnet-hub set screw" to a REQUIRED first step (not
  conditional), added the decisive numbers and elapsed-time, so the re-zero durably holds through the reveal.
- LEGACY LATCH item remains CLOSED (429e2a5 = HEAD = flashed). No owner-independent code work pending; tree clean.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 08:57:48: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically ≈ wedge 10/11 (camera, calibrated).
- Instruments live (verified this session): serial bridge (answered `s`), cam (flat, −5423.74). NO wheel/motor
  work left running (all tool calls synchronous; started NO background task). 0 of <=4 motor spins used. UNFLASHED (429e2a5).
- Repo: docs-only edit (this file) on claude/adaptive-v2. Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm with ONE `s` and no-op. The block is now first-hand verified (Session 10) — do NOT
   re-derive again; a supervisor `?` probe keeps the board alive between sessions if you send nothing.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 9 (~08:53) — STILL BLOCKED on owner; LIVE 3-way re-confirm; 0 spins, NO flash
### What I did (NO motor, NO flash — unblock-signal check + live instrument integrity check + handoff)
- Checked the two unblock signals: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05
  (grepped: only 07:24 / 07:46 / 07:54 exist). (2) No owner note atop this file. Block unchanged.
- KEY DIFFERENCE vs Sessions 5-8: instead of re-reading stale logs, I verified all three instruments are
  producing FRESH data at the same wall-clock second, which is the one thing that could have undermined the
  "no physical motion" evidence (a frozen cam/mic process would echo stale flats forever):
    * Serial: sent `s` → fresh reply 08:52:44 (age_us=690, sensor_pos=FRESH) angle=134.65 wedge=4 omega=0 mode=10.
    * Camera: pw_cam.log timestamps advancing (…08:52:03), cam=-5423.81 d≈0 q=2.1 — live AND flat.
    * Mic: pw_mic.log advancing (08:53:29), rms≈-96 dBFS band≈-47 dB flag=0 — live AND silent (no motion sound).
  → All three LIVE and mutually agreeing NOW: encoder frozen at 134.65, wheel physically unmoved, room silent.
  Block is now sextuple-confirmed, this time with a live (not stale-log) three-way. Did NOT re-derive the slip math.
- Confirmed the LEGACY LATCH open item is already CLOSED: committed at 429e2a5 ("Latch fix: 3x-failed
  dare-recovery floats coils (was infinite grip) - VERIFIED"), which is HEAD and the flashed image. No
  owner-independent code work remains pending; working tree clean. So this session correctly stays a no-op.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 08:52:44: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera).
- Instruments live (verified fresh this session): bridge, cam (flat), mic (silent). NO wheel/motor work left
  running (all tool calls synchronous; started NO background task). 0 of <=4 motor spins used. Firmware UNFLASHED (429e2a5).
- Repo: docs-only edit (this file) on claude/adaptive-v2. Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm the block with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT
   re-derive the analysis — it is verified across Sessions 4-9 (Session 9 added a live 3-way instrument check).
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 8 (~08:50) — STILL BLOCKED on owner; 0 spins, NO flash (no-op, as prescribed)
### What I did (NO motor, NO flash — one status read + handoff only)
- Checked the two unblock signals: (1) NO new `# wedge-0 boundary set` in pw_serial.log after 07:54:05 —
  last real re-zero unchanged (grepped: 07:24 / 07:46 / 07:54 only). (2) No owner note atop this file.
- One fresh `s` (08:50:42): sensor_pos=FRESH age_us=256 angle=134.65 wedge=4 omega=0 mode=10 DONE (coils
  floating). Camera 08:50:45 cam=-5423.77 d≈0 q=2.1 — IDENTICAL to the 08:25:56 post-landing lock. State
  FROZEN, identical to Sessions 4/5/6/7 (encoder rock-stable ~24 min, wheel physically unmoved). Block is
  now QUINTUPLE-verified; did NOT re-derive. Board answered `s` → supervisor silence timer reset, no RTS.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 08:50:42: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera).
- Instruments live: bridge pid30212, cam pid19992 (flat), mic pid12492. NO wheel/motor work left running
  (all tool calls synchronous; started NO background task). 0 of <=4 motor spins used. Firmware UNFLASHED (429e2a5).
- Repo: docs-only edit (this file) on claude/adaptive-v2. Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 07:54, OR an owner note atop this file. If
   neither, re-confirm the block with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT
   re-derive the analysis — it is verified across Sessions 4/5/6/7/8.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 7 (~08:49) — STILL BLOCKED on owner; 0 spins, NO flash (no-op, as prescribed)
### What I did (NO motor, NO flash — one status read + handoff only)
- Checked the two unblock signals: (1) no NEW `# wedge-0 boundary set` in pw_serial.log after 08:46 — last
  real re-zero is STILL 07:54:05 (the 08:45:22 `=== PRIZE WHEEL ===` block is a `?` help-probe echo, board
  ANSWERED it → no reset). (2) No owner note at the top of this file answering the pointer question.
- One fresh `s` (08:49:04): angle=134.65 wedge=4 mode=10 DONE, coils FLOATING, FRESH (age_us=155). Camera
  08:49:06 cam≈-5423.75 d≈0 q=2.1 — IDENTICAL to the 08:25:56 post-landing lock. State FROZEN and identical
  to Sessions 4/5/6 (encoder rock-stable ~23 min, wheel physically unmoved). Did NOT re-derive the analysis
  (it is quadruple-verified now). The board answered `s` → supervisor silence timer reset, no RTS needed.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 08:49:04: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera).
- Instruments live: bridge pid30212, cam pid19992 (flat), mic pid12492. NO wheel/motor work left running
  (all tool calls synchronous; started NO background task). 0 of <=4 motor spins used. Firmware UNFLASHED (429e2a5).
- Repo: docs-only edit (this file) on claude/adaptive-v2. Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (unchanged critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` after 08:49, OR an owner note in this file. If
   neither, re-confirm the block with ONE `s` (keeps serial alive, avoids RTS reset) and no-op. Do NOT
   re-derive the analysis — it is verified across Sessions 4/5/6/7.
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move BEFORE resuming
   T3 baseline (currently ~4 g / 3 G valid, all pre-anomaly). Then T4 anomaly items, T5, T6 ATTENDED
   acceptance (>=30 owner hand spins), REPORT.md, checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 6 (~08:46) — STILL BLOCKED on owner; consolidated the ask; 0 spins, NO flash
### What I did (NO motor, NO flash — verification + handoff clean-up only)
- Re-confirmed the block is genuinely owner-only and unchanged: last real re-zero is still 07:54:05
  (`# wedge-0 boundary set`); the `z ...` lines at 08:30/08:35/08:39 are HELP-MENU echoes from the
  supervisor's `?` silence-probes, NOT zero events. No owner note answering the pointer question.
- Fresh readings 08:46: encoder angle=134.65 wedge=4 (rock-stable now 20+ min, 08:26→08:46, zero drift);
  camera cam=-5423.84 d=0 q=2.1 (identical to post-landing lock) → wheel physically unmoved since 08:25:56.
  State is FROZEN and identical to Sessions 4/5 — encoder ~156° off physical. Did NOT re-derive a 4th time.
- CONSOLIDATED the owner ask: replaced the two stacked owner-action blocks with ONE crisp, low-friction
  action at the very top — the attended rim-screw re-zero (same as 07:54), which fixes BOTH candidate
  causes (encoder slip OR wheel roll) by re-establishing encoder==wheel by construction, so the owner no
  longer needs a look-then-report-back round-trip to unblock. Kept the optional wedge-number glance for
  diagnosis and the magnet-hub/sensor-mount check for the slip case. Evidence trail (Sessions 4/5) preserved.
- Did NOT flash the staged AS5600 magnet-health readout (Session 5's reasoning still holds: it can't detect
  a hub slip, and flashing now would move the board off the validated 429e2a5 image right before the owner
  must re-zero against known-good firmware).
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 08:46:20: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera).
- Instruments live: bridge pid30212, cam pid19992 (flat), mic pid12492. NO wheel/motor work left running
  (all tool calls synchronous; started NO background task). 0 of <=4 motor spins used. Firmware UNFLASHED.
- Repo: docs-only edit (this file) on claude/adaptive-v2. Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (critical path — STILL BLOCKED on the ONE owner action at the very top)
1. Check pw_serial.log for a NEW `# wedge-0 boundary set` dated after 08:46, OR an owner note in this file.
   If neither, the mission still cannot validly advance — re-confirm the block briefly and no-op (do NOT
   re-derive the analysis again; it is triple-verified). Keep serial from going silent (a stray `s` resets
   the supervisor's 4-min silence timer and avoids an unnecessary RTS reset).
2. Once the owner re-zeros: RE-VALIDATE encoder==cam with ONE tiny attended k/K cam-cal move (net enc vs
   cam within a few deg) BEFORE resuming T3. Then continue T3 baseline toward >=10 g + >=10 G (currently
   ~4 g / 3 G valid, all pre-anomaly), batches <=4/session, armed d + mic.
3. Then T4 anomaly items (slow-engage click, ccw brake rattle, uphill recovery slip), T5, then T6
   ACCEPTANCE = ATTENDED >=30 owner HAND spins via the relay protocol, then REPORT.md + checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION 5 (~08:42) — INDEPENDENTLY RE-VERIFIED the halt; still BLOCKED on owner; 0 spins, NO flash
### What I did (NO motor, NO flash — verification + handoff only)
- Did NOT take the prior halt on faith: pulled the RAW serial around the jump. Confirmed the transition
  from angle=338.55 (wedge 11, last reading 08:26:13) to angle=134.65 (wedge 4) occurred during a ~4-min
  serial-SILENT gap (08:26:13→08:30:30) with NO boot banner, NO RTS reset, NO motor, NO `z` — the 08:30:30
  and 08:35/08:39 "=== PRIZE WHEEL ===" lines are `?` help-probe echoes (board answered = no reset), not reboots.
  AS5600 is a live single-turn absolute read (s shows sensor_pos=FRESH, small age_us), so a stable 134.65
  means the magnet is physically presenting that angle to the sensor NOW. Camera (independent) = no motion.
  => prior attribution "cam short + enc full = disc slipped" (magnet/hub decoupled ~156°) is SOUND. Not overturned.
- Fresh `s` 08:41:08: angle=134.65 wedge=4 omega=0 mode=10 DONE — IDENTICAL to 08:30:52 (~11 min prior).
  NEW EVIDENCE: encoder is rock-stable (no drift/noise over 15+ min) → consistent with a single discrete
  mechanical decoupling (or a stuck/persistent fault), NOT an active intermittent AS5600 electrical glitch.
- Camera 08:42:16 still cam≈-5423.82 d≈0 q=2.1 (= post-landing lock) → wheel physically unmoved since 08:25:56.
  Told the owner (top block) that the wheel-state NOW == anomaly-time state, so a single look is decisive.
### Why NO firmware this session (deliberate)
- Considered adding an AS5600 magnitude/AGC/STATUS health readout. Rejected FOR NOW: the encoder IS the
  only position sense, so NO in-firmware guard can detect a magnet-HUB slip (magnet still centered over the
  sensor, just co-rotated wrong → STATUS/magnitude read normal). It would only catch magnet loss/weakening.
  Low value for THIS fault, and flashing during the halt would move the board off the validated 429e2a5
  image right before the owner must re-zero against known-good firmware. Left as a staged idea (below), not flashed.
### Board / instrument state at end (SAFE — coils floating — ENCODER FRAME STILL SUSPECT)
- Board `s` 08:41:08: angle=134.65 wedge=4(firmware, NOT trustworthy) omega=0 mode=10 DONE, coils FLOATING,
  accel=900, cal unchanged cw(0.300/0.150) ccw(0.352/0.119). Physically believed still ≈ wedge 10/11 (camera).
- Instruments live: bridge pid30212 (verified ALIVE), cam pid19992 (flat), mic pid12492. NO wheel/motor work
  left running (all Bash/PowerShell calls synchronous; started NO background task). 0 of <=4 motor spins used.
- Repo: docs-only edit (this file) on claude/adaptive-v2. Firmware UNCHANGED + UNFLASHED (429e2a5 running).
  Mission NOT complete — stay on branch, no REPORT.md.
### NEXT SESSION (critical path — STILL BLOCKED on the owner binary question at the very top)
1. Check for an owner note / a NEW `wedge-0 boundary set` in pw_serial.log after 08:31 answering the physical
   pointer position. If none, the mission still cannot validly advance — re-confirm the block, do NOT do motor work.
2. If owner reports wedge 10/11 → after they secure the magnet hub + attended re-zero, RE-VALIDATE encoder==cam
   with ONE tiny attended k/K cam-cal move (net enc vs cam within a few deg) BEFORE resuming T3.
3. If owner reports wedge 4 → the encoder is fine; investigate why the wheel rolled 156° while floating (a
   naturalness/creep concern for acceptance) and re-check cam geometry; then resume T3 on the trusted mask.
4. STAGED (only after frame re-validated, evidence-first): optional AS5600 magnitude/AGC/STATUS readout added
   to `s`/diag as a continuous magnet-health signal for the reveal — will NOT catch a hub slip, but flags
   magnet loss/weakening. Do not flash blind; verify it does not touch the proven TMC init or motor path.

## 2026-07-27 SUPERVISOR v2 SESSION 4 (~08:31) — ANOMALY #1 = ENCODER-FRAME SLIP (three-way proven); HALTED, 0 spins
### What I did (NO motor, NO flash — investigation only)
- Resumed the critical-path task #1 (investigate Anomaly #1 post-landing creep) using the exact
  post-LANDED polling method the prior session prescribed — but the wheel was ALREADY sitting in a
  post-landing DONE state (landed w10 @08:25:56), so I got the creep curve for FREE (no motor budget).
- Polled `s` x12 @~2 s: wheel was at angle=134.65 wedge=4 and DEAD STILL (zero change across 23 s).
  But the PRIOR reading (08:26:13) was angle=338.55 wedge=11 -> a ~156° shift had occurred in between.
- Cross-checked the camera over 08:26:14–08:30:31 (3844 samples): cam-angle range 0.18 units (~0.2°
  physical via scale 0.837491), sum d=-0.18, max|d|=0.020, quality steady ~2.1 (never <1.0). => the
  physical wheel did NOT move. Confirmed the wheel settle-oscillated ~40° then locked flat at 08:26:02.
- Cross-checked the mic (08:26:02–08:30:32): band max -47.2 dB, zero anomaly flags => no acoustic event.
- Ruled out reboot/RTS/re-zero: no boot banner / no `z` execution in serial for the window; the 08:30:30
  burst was only the supervisor's `?` help-probe (board ANSWERED it -> no RTS reset). Encoder single-turn
  absolute -> a 156° firmware-angle change REQUIRES the magnet to have moved ~156° relative to the sensor;
  camera proves the wheel rim did not -> magnet/sensor decoupling ("disc slipped"), not physical creep.
### Reinterpretation of prior sessions
- The prior "post-landing creep +22.5°" (316->338.55 @08:26:13) is now split: the immediate post-landing
  part is a REAL physical settle-oscillation (camera saw it, ~8 s, damped). The LATER large drift is the
  ENCODER-frame slip, not gravity creep. The prior candidate fixes (extend DRIFT_WATCH / add creep-margin
  buffer) would NOT help — they assume physical creep. DO NOT implement them; this is an encoder fault.
- The historic 33/78/150/255° face-vs-encoder divergences were attributed to grind/stall/latch events.
  THIS one has NO motor event at all (static float) — a new/worse class if it's a mechanical hub slip.
### Board / instrument state at end (motor SAFE — coils floating — but ENCODER FRAME SUSPECT)
- Board `s` 08:30:52: angle=134.65 wedge=4(firmware) omega=0 mode=10 DONE, coils FLOATING, accel=900,
  cal unchanged cw(0.300/0.150) ccw(0.352/0.119). Physically the wheel is believed to still be at the
  08:25:56 landing (~wedge 10/11 per camera) — firmware wedge is NOT trustworthy (see owner block).
- Instruments live: bridge pid30212, cam pid19992 (stable), mic pid12492 (ambient). NO wheel/motor work
  left running (all Bash calls synchronous; started no background task). 0 of <=4 motor spins used.
- Repo: docs-only edits (this file + CLAUDE_VARIANT.md) on claude/adaptive-v2. Mission NOT complete
  (stay on branch; no REPORT.md). Firmware unchanged (429e2a5 latch fix still flashed + running).
### NEXT SESSION (critical path — BLOCKED on the owner check above)
1. Read the OWNER ACTION block at the top; check the serial log / this file for an owner note confirming
   the physical pointer position and whether the encoder slipped. Do NOT do motor work until encoder
   frame is re-validated (attended re-zero + magnet-hub/sensor-mount secured if slip confirmed).
2. If the owner re-secures + re-zeros: RE-VERIFY the encoder/camera agree with a single tiny attended
   `k`/`K` cam-cal move (net enc vs cam within a few deg) BEFORE resuming — this event means we can no
   longer assume the encoder is trustworthy just because `s` reads FRESH/VALID. Then resume T3 baseline.
3. Consider a firmware guard: a cheap camera-independent encoder sanity check is hard, but at minimum a
   startup/periodic AS5600 magnitude/AGC (`d` diag) read could flag a weak/failing magnet. Evidence-first.

## 2026-07-27 SUPERVISOR v2 SESSION 3 (~08:22) — T3 BASELINE batch (latch fix DONE last session)
### Entry state (verified this session)
- Firmware = committed latch fix 429e2a5 (board runs it; no flash needed). Working tree clean.
- Frame zero VALID (owner re-zero 07:54, wedge-3 center=103.5). Board `s` 08:22:15: angle=106.35
  wedge=3 (SAFE) omega=0 mode=10 DONE, coils floating, accel=900, cal cw(0.300/0.150) ccw(0.352/0.119).
- Instruments live+fresh: bridge pid30212, cam pid19992 (stable d=0 q=2.4), mic pid12492 (ambient
  floor RMS ~-97 dBFS band -48 dB). Cam is UNCAL for absolute wedge but net-rotation scale=0.837491
  in pw_cam_cal.txt (T2); use for cam-enc net agreement.
- Prior VALID (post-07:54) baseline points: g->w6, G->w11 (~08:05), g->w3 (08:19 SPIN#1 steered dare1).
  Counts so far g~2 / G~1; need >=10 each. This batch adds toward that (G-weighted, scarcer dir).

### MOTOR RUN log (this session) — T3 baseline batch, <=4 spins
- MOTOR RUN: T3 baseline batch of 4 alternating G,g,G,g through v4. Arm d on spin 1 only (dense enc
  reference for acoustic cross-attribution; d auto-dumps after stop, WDT-safe Fix A), spins 2-4 without
  d to keep landing/acoustic data clean. Per spin capture: SPIN-GEN release omega (engage speed), accel,
  fight-aborts, LANDED wedge/isDare/steered, cam net vs enc net, post-release drift, mic flags in window.
  Expect each: SPIN-GEN START->RELEASE reason=target -> SPIN#n V4 -> LANDED isDare=0. Ceiling 900, frame
  zero VALID, bench, wheel clear. Firmware 429e2a5.

### RESULT — 4-spin T3 batch, ALL SAFE, zero aborts/latch/mic-anomaly (batch budget spent)
Fired G,g,G,g (2 per FAS dir). Release speeds spanned 0.365-0.442 rev/s (full T1 range). Every spin:
SPIN-GEN START->RELEASE reason=target -> V4 STEER -> LANDED isDare=0 steered=1. No fight-abort, no
RECOVERY-FAIL (the only FAILED lines in log remain the historic 07:46 pre-fix ones), no WDT/panic.
  - SPIN#2 G/FAS- dir+1: rel omega=0.365 -> LANDED wedge=2 (pred w3). Dense d trace CLEAN
    (n=3072 accepted=3072 errors=0 gaps=0 alias=0 flips=0 maxAbsDelta=1) = good acoustic/jitter ref.
  - SPIN#3 g/FAS+ dir-1: rel omega=-0.412 -> pred wedge=5 (DARE!) -> STEER -> LANDED wedge=0, err -1.4.
  - SPIN#4 G/FAS- dir+1: rel omega=0.413 -> pred wedge=1 (DARE!) -> STEER -> LANDED wedge=8 (+1 rev).
  - SPIN#5 g/FAS+ dir-1: rel omega=-0.442 -> LANDED wedge=10 (pred w2 safe, steered to distribute).
Acoustics: ZERO mic flag=1 across 08:22:30-08:26 window; mic stayed at ambient floor (RMS ~-96 dBFS,
band ~-48 dB) THROUGHOUT ramps/engages/brakes. Engage+takeover acoustically inaudible (T4/T6 criterion
met for this batch). Cam SAW every spin (d peaked -3.41 rev/s-ish at release, returned to d~0 stable) -
qualitative three-way agreement (motion+stop); precise net-agreement fit deferred to pw_baseline tool.
VALID (post-07:54) baseline tally now ~ g: w6,w3,w0,w10  |  G: w11,w2,w8  (=4 g / 3 G). Need >=10 each.

### >>> T3-EXPOSED ANOMALY #1 (catalog; investigate FIRST next session, NOT owner-blocking) <<<
POST-LANDING CREEP IN DONE. SPIN#5 LANDED wedge=10 angle=316.0 @08:25:56 (dir=-1 spin); board `s`
@08:26:13 (17 s later) read angle=338.55 wedge=11 omega=0 mode=10 DONE. => wheel crept +22.5deg while
in DONE, crossing the w10->w11 boundary, with NO DRIFT_WATCH/CARRY marker in the serial log. Note the
creep was +deg = OPPOSITE the dir=-1 spin (consistent with passive gravity/imbalance pull, not a motor
move; coils were floating). STAYED SAFE (w10 and w11 both safe; dares w1/w5 far) so NO invariant-1/2/3
violation THIS time. BUT: a ~22deg post-landing creep near a dare boundary could carry a steered-safe
landing ONTO a dare (e.g. land w0@~40deg -> creep to ~62 = w1 DARE; or w4/w6 toward w5). This is the
known "DONE never recovers a static rest" gap (CLAUDE.md) meeting real imbalance creep -> it is the
core-guarantee risk T3 is meant to surface.
CAVEATS (do not overclaim from ONE reading): (a) only spin5 had a follow-up `s`; spins 2-4 had the next
spin fired ~immediately so their settle drift is UNMEASURED - unknown if systematic. (b) owner is at the
bench; a hand nudge in those 17 s cannot be excluded from a single sample. Must REPRODUCE before fixing.
REPRODUCE (no extra motor budget beyond the spins - method is post-landing polling, not new moves):
run 2-3 spins; after EACH LANDED, send `s` every ~2 s for ~25 s and log angle(t) to get the creep curve
(magnitude, rate, whether it settles). If systematic and >~half a wedge: candidate minimal fixes to weigh
next session (do NOT implement blind) - (1) extend DRIFT_WATCH arming to persist a few seconds into DONE
for landings whose creep envelope reaches a dare boundary; (2) add a drift-margin buffer so the planner
only accepts targets whose [center +/- creepEnvelope] stays off both dare wedges. Evidence first.

### Board / instrument state at end (SAFE)
- Board `s` 08:26:13: angle=338.55 wedge=11 (SAFE) omega=0 mode=10 DONE, coils FLOATING, accel=900,
  cal unchanged cw(0.300/0.150) ccw(0.352/0.119). No wheel/motor work left running (all my Bash calls
  were synchronous; started no background task). Instruments live: bridge pid30212, cam pid19992,
  mic pid12492. 4 of <=4 motor spins used - motor budget SPENT for this session.
- Repo: docs-only edit (this file) on claude/adaptive-v2; mission NOT complete (stay on branch, no
  REPORT.md yet). Firmware unchanged (429e2a5 latch fix still flashed + running).

### NEXT SESSION (critical path)
1. INVESTIGATE ANOMALY #1 (post-landing creep) via the post-LANDED `s`-polling method above - FIRST,
   before adding more baseline points. It touches the core dare guarantee. Evidence, then minimal fix.
2. Continue T3 baseline toward >=10 g + >=10 G (currently ~4 g / 3 G valid). Batches <=4/session, mic +
   occasional dense d. Keep cataloging engage speed / aborts / enc-vs-cam / drift / acoustics.
3. T4 remaining anomaly items (slow-engage click, ccw brake rattle, uphill recovery slip) - evidence-first.
4. Then T5, then T6 ACCEPTANCE = ATTENDED >=30 OWNER HAND spins via the relay protocol (write request at
   TOP of this file + exit), then REPORT.md + checkout clean main.

## 2026-07-27 SUPERVISOR v2 SESSION (~08:10) — LEGACY LATCH FIX applied + IN-SESSION verified (wheel already on dare)
### Entry state
- Frame zero VALID (owner re-zero 07:54, persisted NVS; wheel physically moving does not change it).
- Board `s` at 08:15:37: angle=156.71 wedge=5 (DARE) omega=0 mode=10 DONE, coils floating, accel=900,
  cal cw(0.300/0.150) ccw(0.352/0.119). The wheel is passively RESTING ON DARE WEDGE 5 (drifted/placed
  there while in DONE; DONE never recovers a static rest — a known benign gap, coils floating = safe).
- Working tree at entry: clean (last session committed SPIN_GEN 40->8). Bridge pid21132 LIVE.

### Firmware change (prize_wheel.ino only) — reviewed staged fix, FOUND A BUG in it, corrected
- Applied the LEGACY LATCH fix (recoveryAttempts>=3 branch of RECOVERY_HOLD, ~line 2669). The staged
  patch in CLAUDE_VARIANT.md used `mode = DRIFT_WATCH` — REJECTED: DRIFT_WATCH re-detects the dare rest
  and re-calls startDareRecovery(), which has NO attempt cap (only tryCreepCarry checks >=4). That would
  loop-re-energize (twitch) forever — WORSE than the static grip. Corrected terminal = `mode = DONE`
  (fully passive; only a genuine new spin re-arms; recoveryAttempts clears then via startSpinEvent).
- New branch body: driverFreewheel() + print "# DARE RECOVERY FAILED: floating coils; hardware/cal fault
  - re-spin to clear" + sawSpinThisCycle=false + settleT0=0 + mode=DONE. NO LANDED line emitted (fault,
  not a safe landing => LANDED-isDare==0 invariant stays intact). No cal pollution.
- Added guarded self-test cmd `L`: refuses unless wheel ready + on a dare wedge; else forces
  recoveryAttempts=3, driverActive(RECOVERY_HOLD_CURRENT_MA), mode=RECOVERY_HOLD -> the real state
  machine then hits the fixed branch. Faithful test of the fix (skips only the 3 prior failed moves).
  Added to help(). Compiles clean (32% flash, unchanged).

### MOTOR RUN log (this session)
- MOTOR RUN: LEGACY LATCH fix verification. Wheel already parked on dare wedge 5, still, coils floating.
  Flash new fw (fix + L cmd), then fire `L` (1 motor action: brief 500mA energize then FLOAT). Expect:
  "# L latch-test: dare wedge=5 ... entering RECOVERY_HOLD" then within RECOVERY_HOLD_MS
  "# DARE RECOVERY FAILED: floating coils ..." then mode->DONE (coils float). Then `s` confirms floating.
  <=4 motor actions; owner at bench; wheel clear. Ceiling 900, frame zero VALID.
- MOTOR RUN: post-fix regression spin — one `g` (FAS+) to confirm the RECOVERY_HOLD edit did not
  break the normal spin/steer pipeline AND to move the wheel OFF dare wedge 5 to a safe rest. Expect
  SPIN-GEN START->RELEASE->SPIN#n V4 -> LANDED isDare=0. (2nd of <=4 motor actions.)

### RESULT — LATCH FIX VERIFIED + regression PASS, committed (LEGACY LATCH open item CLOSED)
- Flashed COM3 (Hash of data verified + Hard resetting). Frame zero survived RTS reset (wedge 5 pre/post).
- `L` test (wheel on dare wedge 5): 08:18:19 "entering RECOVERY_HOLD" -> 08:18:20 "# DARE RECOVERY
  FAILED: floating coils; hardware/cal fault - re-spin to clear" -> 08:18:21 status mode=10 DONE, coils
  FLOATING, still wedge 5 (rotor snapped ~3.5deg on 500mA energize, expected). NO repeat, NO twitch loop,
  NO panic/WDT. Contrast: OLD fw printed "held; do not use" x5 at 07:46 (the infinite grip) — now GONE.
- Regression `g`: RELEASE omega=-0.386 -> v4 predicted wedge 1 (DARE) -> STEER -> LANDED wedge=3 isDare=0
  targetErrorDeg=1.9. Normal pipeline + dare-avoidance intact; wheel moved OFF the dare to safe wedge 3.
- End state: board angle=106.35 wedge=3 (SAFE) omega=0 mode=10 DONE, coils floating, accel=900,
  cal cw(0.300/0.150) ccw(0.352/0.119). Bridge pid30212 LIVE. No wheel work left running. 2 of <=4 motor
  actions used. COMMITTED firmware + docs on claude/adaptive-v2. Board runs the committed fix.

### NEXT SESSION (critical path; frame zero VALID; last firmware SAFETY item now DONE)
1. LATCH FIX IS DONE + VERIFIED + FLASHED + COMMITTED. `L` self-test command remains in fw (guarded,
   harmless test tooling; can stay for the reveal or be stripped before final if desired).
2. Resume T3 baseline: >=10 g + >=10 G through v4, supervised batches <=4/session, armed d + mic; log
   engage speed/aborts/enc-vs-cam wedge/drift/acoustics. Valid baseline points so far: 08:18 g (steered
   dare1->wedge3), plus the 2 from the ~08:05 session. (g/G = iteration tooling ONLY.)
3. Then T4 anomaly fixes (slow-engage click, ccw brake rattle, uphill recovery slip — evidence-first),
   T5, then T6 ACCEPTANCE = ATTENDED: >=30 OWNER HAND spins via the relay protocol (write the request at
   the TOP of this file and exit; motor spins are for iteration only, never final acceptance), then REPORT.md.


## 2026-07-27 SUPERVISOR v2 (new loop) SESSION 1 (~08:05) — FLASHED bounded 8-rev spin-gen; verifying g/G
### State at entry
- Frame zero VALID (owner re-zero 07:54:05, wedge-3 center = 103.5 deg, persisted NVS).
- Uncommitted .ino change reviewed: SPIN_GEN_MOVE_REVS 40 -> 8. VERIFIED SAFE against code:
  serviceSpinGenerator moveEnded path (line ~1400) calls driverFreewheel()+mode=DONE with
  reason="moveEnd"; reach-to-speed at ceiling 900 (accel 450) ~= 1.44 rev << 8 rev bound.
- Compiled clean (32% flash). FLASHED COM3 (Hash of data verified + Hard resetting).
- Bridge stopped(pid31968)->flash->waited 7s->restarted(pid21132), confirmed LIVE (s/c replied).
- Post-flash s: angle~8.7 wedge=0 mode=0 IDLE, accel=900, seeds c=0.300 b=0.150 / c=0.352 b=0.119.
  Wheel physically drifted to ~wedge0 boundary after 07:56 (floating coils, unbalanced) — wedge0
  OFFSET persisted (Fix B, same RTS reset that was round-trip-verified) so mask STILL VALID.

### MOTOR RUN log (this session)
- MOTOR RUN: verify bounded 8-rev spin-gen. Fire g (FAS+), wait LANDED; then G (FAS-), wait
  LANDED. <=4 spins (using 2). Expect each: SPIN-GEN START(release into free coast) -> RELEASE
  reason=target -> SPIN#n V4-ENGAGE/DECISION -> LANDED isDare=0, NO hang, NO loop-WDT reset.
  Firmware = bb6d211 + SPIN_GEN_MOVE_REVS 8. Ceiling 900, frame zero VALID. Bench, wheel clear.
  Instruments: bridge pid21132, cam pid19992, mic pid12492 live. Landings now PHYSICALLY VALID.

### RESULT — both spins PASS, bounded ramp verified, committed
- SPIN#1 (g/FAS+, enc dir=-1): SPIN-GEN RELEASE reason=target omega=-0.382; pred stop
  wedge=1 (DARE) -> action=STEER -> LANDED wedge=6 isDare=0 steered=1. durS~8s, no hang.
- SPIN#2 (G/FAS-, enc dir=+1): SPIN-GEN START accel=450 targetHz=2400 -> RELEASE reason=target
  omega=0.375; pred stop wedge=5 (DARE) -> STEER -> LANDED wedge=11 isDare=0 steered=1.
- ZERO hangs, ZERO loop-WDT/panic/Reboot markers, ZERO "DARE RECOVERY FAILED" (latch untouched;
  the only FAILED lines in log are the old 07:46 ones). Bounded 8-rev move released on target
  both dirs — the 40-rev "never decelerate" hang mode is gone. targetErrorDeg (402.8/355.0)
  is the known-cosmetic gated-move artifact; wedge numbers authoritative.
- End state: board mode=10 DONE, coils floating (safe), angle=345.7 wedge=11, accel=900.
- COMMITTED SPIN_GEN_MOVE_REVS 40->8 on claude/adaptive-v2. Working tree now clean except notes.

### NEXT SESSION (critical path, frame zero VALID)
1. LEGACY LATCH fix: staged verbatim in CLAUDE_VARIANT.md ("LEGACY LATCH located"). Apply
   (float coils + fault + DRIFT_WATCH in the recoveryAttempts>=3 branch ~line 2662), flash,
   then FORCE a fail-recovery test (deliberately land a dare + fail recovery 3x) to prove the
   coils now float instead of gripping. This is the last firmware safety item.
2. Resume T3 baseline: >=10 g + >=10 G through v4 (batches <=4/session), armed d + mic, log
   engage speed/aborts/enc-vs-cam wedge/drift/acoustics. (g/G = iteration tooling ONLY.)
3. First g/G spins THIS session already gave 2 valid baseline data points (both steered dares).
4. T6 ACCEPTANCE IS ATTENDED (owner directive, commit 906cc0f / MISSION.md T6): >=30 OWNER
   HAND spins via the AGENT_NOTES relay protocol, NOT motor spins. When acceptance-ready
   (after latch fix + T3-T5), write the exact request at the TOP of this file and exit so the
   owner can perform them. Motor spins are for iteration only, never for final acceptance.


## 2026-07-27 SUPERVISOR v2 SESSION 4 (~07:38) — g/G rebuilt on bounded k/K-style ramp (owner tooling directive)
### MOTOR RUN log (session 4)
- MOTOR RUN: verify bounded g/G ramp — fire g (FAS+), G (FAS-), g+armed-d (dense enc
  trace), G. <=4 spins. Expect each: SPIN-GEN START(release into free coast) ->
  RELEASE reason=target -> SPIN#n V4-ENGAGE -> LANDED, NO hang, NO loop-WDT reset.
  Firmware = SPIN_GEN_MOVE_REVS 40->8 (bounded) atop WDT+FixA/B. Ceiling 900, coils
  floating at start, mode IDLE, angle~345 wedge11. NOTE: frame zero is still the
  ARBITRARY PLACEHOLDER (no attended z since 07:24:32) — landing wedges are NOT
  physically valid; this run only verifies RAMP ROBUSTNESS, not landings. Bench,
  wheel clear. Instruments: bridge pid31968, cam pid19992, mic pid12492 all live.

## 2026-07-27 SUPERVISOR v2 SESSION 3 (~07:31) — BLOCKED on frame zero; latch located; 0 spins

### >>> STILL THE #1 BLOCKER: FRAME ZERO IS AN ARBITRARY PLACEHOLDER <<<
- Verified this session: the ONLY wedge-0 event in pw_serial.log is the placeholder at
  07:24:32 (`# wedge-0 boundary set at current encoder angle (persisted)`). No true
  attended `z` has happened since. The dare mask is therefore misaligned to the physical
  wheel, so EVERY LANDED/isDare/landing-wedge reading is physically meaningless.
- Consequence: T3 baseline, T6 acceptance, and any latch-fix verification are ALL blocked.
  A "30-spin acceptance run" on this mask would be a FALSE guarantee (a physically-dare
  wedge can read safe). I refuse to produce acceptance data on a wrong mask.
- UNBLOCK (physical, owner/attended — I cannot rotate the wheel): rotate so the bright rim
  screw is under the red pointer (physical wedge-0 boundary), send `z` ONCE. It now
  persists across reboots/RTS resets (Fix B, session 1). Then `s` should read a sane wedge.
- LIKELY-PRESENT-OWNER SIGNAL: at 07:31:27 and 07:31:42 someone HAND-SPUN the wheel
  (SPIN#1 omegaPeak 0.220 dir+1; SPIN#2 0.257 dir-1; NO SPIN-GEN markers; no campaign
  process alive). A human was at the bench this morning — that is exactly when the one
  attended re-zero should be done. Bench idle since 07:31:59.

### What I did (frame-independent, no motor, no flash)
- LOCATED the long-lost LEGACY LATCH ("source never located" open item is now CLOSED).
  It is the `recoveryAttempts >= 3` branch of RECOVERY_HOLD, prize_wheel.ino ~2662-2671:
  it prints "DARE RECOVERY FAILED: held" and re-holds forever WITHOUT ever calling
  driverFreewheel(), so the coils stay energized at RECOVERY_HOLD_CURRENT_MA=500 mA in an
  infinite hold loop = the grip the owner felt. Forcing the wheel only resets the timer,
  never floats the coils. Full mechanism + a READY minimal fix (float coils + fault +
  DRIFT_WATCH) written in CLAUDE_VARIANT.md ("LEGACY LATCH located" entry).
- Deliberately did NOT commit/flash the latch fix: exercising that branch requires
  deliberately landing a dare and failing recovery 3x, which is only meaningful with a
  VALID frame zero. Committing a blind change to core dare-recovery would break the
  evidence-first / >=4-verification-spin discipline. Patch is staged verbatim in the log.

### Board / instrument state at end of session 3
- No firmware change, no motor spins (0 of <=4 used). Board last known: mode 0 IDLE from
  the 07:31:59 LANDED; coils floating after the external hand-spins. accel=900, seeds
  c=0.300 b=0.150 (drifted to c=0.352 b=0.119 by the 07:31:59 FRICTION fit).
- Instruments alive (confirmed via Win32_Process): serial bridge pid 15624; cam tracker2
  pid 19992; mic logger pw_mic.py pid 12492; supervisor pw_supervisor2.ps1 pid 11088.
  (Cleaned up an orphaned `tail -f | grep` serial-watcher pipe left from a prior session.)
- git: only doc edits (CLAUDE_VARIANT.md, AGENT_NOTES.md) this session; committing on
  claude/adaptive-v2. NO code change. Repo stays on claude/adaptive-v2 (mission NOT
  complete — do not checkout main / do not write REPORT.md until a valid acceptance run
  exists on a correct frame zero).

### NEXT SESSION (unchanged critical path)
1. Confirm whether an attended `z` at the true rim-screw position has happened (grep
   pw_serial.log for a NEW `wedge-0 boundary set` after 07:24:32). If not, the mission
   cannot validly advance — surface the physical re-zero need again.
2. Once frame zero is valid: (a) apply+flash+verify the staged LEGACY LATCH fix with a
   forced fail-recovery test; (b) resume T3 baseline (>=10 g + >=10 G, batches <=4/session,
   armed d + mic); then T4 fixes, T6 acceptance, REPORT.md.

## 2026-07-27 SUPERVISOR v2 SESSION 1 OUTCOMES — 2 firmware robustness fixes VERIFIED

### >>> OWNER ACTION REQUIRED BEFORE ANY VALID CAMPAIGN <<<
- FRAME ZERO IS CURRENTLY WRONG (arbitrary). The WDT reboot at 07:15:20 wiped the
  old in-RAM zero; I then persisted a PLACEHOLDER zero at whatever position the wheel
  sat at 07:24:32 (to prove the persistence path). Wedge numbers since are NOT the
  physical wedge map, so the dare-mask is misaligned. Owner/next session must:
  rotate wheel so the bright rim screw is under the red pointer (physical wedge-0
  boundary), send `z` ONCE. It now PERSISTS across reboots/RTS resets — do it once.
- Until re-zeroed, do NOT treat any LANDED wedge / isDare as physically valid.

### Fix A (committed): d-dump no longer self-resets the board
- Root cause of the 07:15:20 WDT reboot: dumpDiagnostics() prints ~3072 lines; the
  serial TX buffer fills and Serial.printf busy-waits, blocking loop() ~16 s >> the
  4000 ms loop WDT -> panic+reset (reboot wiped frame zero). NOT the spin ramp.
- Fix: feed esp_task_wdt_reset()+yield() every 32 lines inside the dump loop
  (prize_wheel.ino dumpDiagnostics). VERIFIED: armed d + g spin -> full 3069-line
  dump completed, ZERO task_wdt/Rebooting markers, board responsive after. Same op
  rebooted at ~6.5 s before the fix. Encoder trace clean (accepted=3072 errors=0
  gaps=0 alias=0 flips=0 maxAbsDelta=1).

### Fix B (committed): frame zero survives resets (NVS-persisted)
- Was: wedge0OffsetDeg reset to 0.0 on every boot (only sign/cal/accel were in NVS),
  so every WDT reboot AND every supervisor RTS reset wiped frame zero (the standing
  hazard the mission rules warn about).
- Fix: `z` now preferences.putDouble("wedge0",...); boot getDouble restores it (same
  proven pattern as "accel"). VERIFIED via RTS-reset round-trip: z at raw~54 -> angle
  reads 0.00 -> RTS reboot (mode 10->0) -> angle still 0.00 (offset restored).

### WDT behaviour confirmed
- The loop task WDT (4000 ms panic+reset) WORKS: it caught the dump stall and
  self-recovered. Boot banner: "# loop watchdog armed: 4000 ms, panic+reset on stall".
  Fix A removes the only stall trigger seen this session; WDT remains the safety net.

### Firmware / board state at end of session 1
- Firmware = spin-gen(16e1e14)+loopWDT(6ab587a)+FixA+FixB. COMPILED clean (32% flash),
  FLASHED COM3 (Hash verified + Hard resetting), then RTS-reboot-tested. To COMMIT
  this session on claude/adaptive-v2. Board: mode=0 IDLE, coils floating, accel=900,
  seeds c=0.300 b=0.150, wedge0 PLACEHOLDER persisted (see OWNER ACTION above).
- Instruments live: serial bridge pid 15624; cam tracker v2 (assumed, not re-checked);
  mic logger pw_mic.py pid 12492 (running, ambient floor RMS ~-97 dBFS / band ~-48 dB).
  No motor/wheel task left running. pyserial + sounddevice pip-installed this session.
- Motor spins used: 2 of <=4 (both `g`). Both clean START->RELEASE->SPIN#->LANDED,
  no aborts, no rattle markers, no WDT reset (2nd verified Fix A).

### Acoustic first data point (T2b)
- The clean `g` spin was ACOUSTICALLY SILENT at the mic: mean 1-6kHz band == ambient
  (-48 dB), crest 7.1 vs 6 dB ambient, zero anomaly clips. At current mic placement
  the peg-clack rhythm (~12x omega, ~4-5 impulses/s at 0.4 rev/s) is below the floor
  -> mic cannot yet serve as the natural-sound reference the addendum wants. NEXT:
  either move the mic closer or lower the anomaly threshold; and the "strange noises"
  the owner hears are likely intermittent engage-click/rattle events (not this clean
  run) -> catch them with clips during a real campaign AFTER re-zero.

### NEXT SESSION (in priority order)
1. Owner (or attended) re-`z` at true physical wedge-0 (screw under pointer). Verify
   `s` reads a sane wedge and that a `g` spin lands on a plausible physical wedge.
2. Resume T3 baseline: >=10 g + >=10 G through v4, batches <=4/session, WITH d dense
   capture (now safe) + mic. Log engage speed, aborts, enc-vs-cam wedge, drift, acoustics.
3. Improve mic sensitivity/threshold so peg rhythm is resolvable (natural-sound ref).

---

## 2026-07-27 SUPERVISOR v2 SESSION 1 (~07:10-) — T2b mic logger built; peg-clack cal + T3
- Board health CONFIRMED at start: s -> sensor FRESH, velocity VALID, angle=108.72
  wedge=3 omega=0 mode=10(DONE) ceiling=900 seeds c=0.300 b=0.150. No RTS reset in
  events log (supervisor v2 started 07:07). Frame zero intact from prior session.
- Firmware = commit 6ab587a (spin-gen g/G @16e1e14 + loop TWDT 4000ms). help shows
  g/G/k/K present. Working tree clean except this notes file.
- T2b DONE (logger): built %TEMP%\pw_mic.py -> pw_mic.log, ~50ms/line: rms_dbfs,
  band_db(1-6kHz), crest_db, peak_dbfs, floor_db(EWMA ambient), flag. Anomaly=band
  >floor+12dB x3 blocks -> 4s WAV clip to %TEMP%\pw_clips (last 20), debounce 3s.
  pid %TEMP%\pw_mic.pid, stop file %TEMP%\pw_mic_stop. Uses default input (Intel mic
  array dev1, NOT NVIDIA Broadcast). Installed sounddevice 0.5.5 via pip. RUNNING
  detached pythonw pid 12492. Ambient floor: RMS ~-97 dBFS, band ~-48 dB (= the 10s
  ambient calibration; wheel silent). Same wall clock as serial/cam logs.

## MOTOR RUN log (session 1)
- MOTOR RUN: peg-clack acoustic cal + T3 batch — arm d (dense enc), fire g (FAS+)
  spin-gen, wait LANDED+dump; then G (FAS-). 2 spins total (<=4 rule). Instruments:
  serial bridge, cam tracker v2, mic logger all live. Bench, wheel clear. Firmware
  6ab587a. Purpose: re-confirm g/G+WDT in fresh loop, first 3-frame+acoustic T3 data,
  predict peg rhythm ~12x omega for the mic natural-sound reference.

## 2026-07-27 relaunch (~07:00) — T1 HANG FIXED; RESUME AT T3

### FIRST-PRIORITY RESOLVED
- The 00:49:50 `G` spin hang is FIXED. Root cause class: `loop()` blocked inside a
  library/peripheral call during the spin-gen motor ramp (12 s software timeout
  provably never fired; no software infinite loop; I2C already bounded by
  Wire.setTimeOut(3)), and there was NO hardware watchdog to recover. Fix: loop task
  now subscribed to ESP32 Task WDT, 4000 ms, panic+reset; fed once per loop().
  Full postmortem in CLAUDE_VARIANT.md ("T1 hang postmortem + loop-watchdog fix").
- Firmware change: prize_wheel.ino only — added `#include "esp_task_wdt.h"`,
  `LOOP_WDT_TIMEOUT_MS 4000`, WDT init/reconfigure+add at end of setup(),
  `esp_task_wdt_reset()` as first line of loop(). TMC init block untouched.
- COMPILED clean (32% flash) + FLASHED to COM3 (Hash of data verified + Hard
  resetting). Not yet committed at time of this note -> commit right after (this
  session commits it on claude/adaptive-v2).
- VERIFIED: 4 supervised spins (g,G,g,G) all clean START->RELEASE->SPIN#n->LANDED,
  no hang / no WDT reset / no refusals. Landed wedges 6/11/10/3, all isDare=0.
  Spin #4 target 0.442 rev/s (> the 0.436 that hung old fw) completed fine.

### BOARD/INSTRUMENT STATE AT END OF SESSION
- Board healthy, mode=IDLE, coils floating (safe). Boot banner shows
  `# loop watchdog armed: 4000 ms`.
- Serial bridge RESTARTED and live, new pid in %TEMP%\pw_bridge.pid (was dead-handle
  once after flash; fixed by full stop+restart — remember the >=5 s wait rule, and
  if pid alive but log stays empty, stop+restart the bridge). Cam tracker v2 assumed
  still running (not re-checked this session). No wheel work left running.
- Firmware now = commit 16e1e14 (spin-gen) + this WDT commit. Ceiling 900, cal seed.

### NEXT: resume T3 baseline campaign (was interrupted by the hang)
- Run >=10 g + >=10 G spins through v4 in supervised batches of <=4 per session.
  Per spin capture: engage speed, ramp accel, aborts, landing wedge (enc) vs
  cam-implied wedge, post-release drift. Host tool: %TEMP%\pw_baseline.py.
- Acoustic addendum (T2b) is still PENDING and is owner-flagged do-FIRST for the
  broader mission: build %TEMP%\pw_mic.log logger before/alongside T3 so T3 spins
  also get acoustic data. (Not started.)
- Watch for any watchdog reset in the serial log during campaigns; if one fires,
  grab the panic backtrace to pin the true stall cause.

## 2026-07-27 session

### State
- Instruments confirmed alive at session start: serial bridge (COM3) + cam
  tracker v2. Cam calibration file `%TEMP%\pw_cam_cal.txt` did NOT exist ->
  T2 still pending.
- Firmware baseline at session start: v4.4 (commit 47eebd1).
- Persisted accel ceiling reported by `c`: 900 sps2. Friction seeds still at
  c=0.300 b=0.150 both dirs (calibration is fresh/seed).

### T1 — spin generator `g`/`G`  (IN PROGRESS)
Added to prize_wheel.ino:
- Tunables SPIN_GEN_* (target 0.35-0.45 rev/s, 600 mA, accel = ceiling/2,
  40-rev move, 12 s timeout).
- Mode `SPIN_GEN` appended to enum (after DRIFT_WATCH).
- State vars spinGen*.
- startSpinGenerator(fasDir) / serviceSpinGenerator(): ramp up at half the
  persisted ceiling, RELEASE (driverFreewheel) when encoder omega reaches the
  random target, hand off to FREE_SPIN via mode=DONE. `g`=FAS+, `G`=FAS-.
- Wired into loop switch + handleSerial + help().
Rationale for release-by-encoder-omega: guarantees the wheel actually reached
speed (not just the commanded profile), and target < GUEST_OVERRIDE (0.80) so
no override re-latch. Direction invariant safe: motion never reverses.

NEXT STEP: compile-only, then flash (stop bridge first!), then verify `g`/`G`
produce SPIN-GEN START/RELEASE then a normal SPIN#n START/V4-ENGAGE.

### Flash reminder (from CLAUDE.md)
Stop bridge -> esptool -> wait >=5 s -> restart bridge. cli + --libraries
mandatory. Judge upload by 'Hash of data verified' + 'Hard resetting'.

## MOTOR RUN log
- 2026-07-27 ~00:17 flashed T1 (commit 16e1e14). Verified help shows g/G.
- MOTOR RUN: first `g` (FAS+) spin-generator test — expect SPIN-GEN START -> RELEASE -> SPIN#n V4-ENGAGE coast. Bench, wheel clear.

- MOTOR RUN: `G` (FAS-) spin-generator test, opposite direction.

- MOTOR RUN: T2 cal capture — arm d (dense encoder), fire g, dump, fit cam vs enc.

- MOTOR RUN: T2 cal — arm d, fire k (bounded 108 deg cal move), dump, fit.

- MOTOR RUN: T2 cal series — 6 alternating k/K bounded moves for net-rotation scale fit.

- MOTOR RUN: T2 verify — 3 fresh k/K moves, check vs stored scale.

## T2 DONE (00:38): scale=0.837491 sign=+1, resid 0.29deg RMS (7 moves), check 0.76/0.86deg PASS.
Host tools in %TEMP%: pw_cam_cal_net.py (fit/check net-rotation), pw_cam_cal.py (streaming, deprecated), pw_baseline.py.
Cal file %TEMP%\pw_cam_cal.txt written.
NEXT: T3 baseline campaign - >=10 g-spins + >=10 G-spins through v4, catalog wheel-speed-up aborts, landings, cam-enc net agreement.

- MOTOR RUN: T3 baseline campaign — 20 spins alternating g/G (10 each) through v4 pipeline.
- MOTOR RUN: T3 baseline campaign (resumed) — driving 20 fresh spins alternating G/g via bridge, waiting for each LANDED. Build=T2 (c442323), ceiling=900, cal seed. Bench, wheel clear.

## 2026-07-27 session (relaunch, ~07:00) — T1 hang fix
- MOTOR RUN: watchdog-fix verification — one `g` (FAS+) then one `G` (FAS-) spin generator run, expect SPIN-GEN START -> RELEASE -> SPIN#n V4-ENGAGE -> LANDED, no hang. Bench, wheel clear. Firmware = spin-gen + loop TWDT (4000ms panic).

## SUPERVISOR HANDOFF 07:0x (session killed by owner re-steer, no fault of yours)
Hang diagnosis so far: Wire.setTimeOut(3) already set, so I2C lockup RULED OUT; remaining suspect = FastAccelStepper ISR/queue block during high-rate spin-gen ramp. Per new OWNER PRIORITY in prompt: do NOT root-cause further - rebuild g/G on the k/K bounded-move ramp style + enable ESP32 task WDT, verify, move on to T3.

## ZERO STATUS WARNING (owner-side, 07:5x)
The z at 07:46:19 is INVALID - it fired while the wheel rested at the 07:44 spin-gen landing, not at the rim screw (the agent's spins moved the wheel after the owner parked it). Owner-side z and s were also injected mid-P1-dump around 07:46 - account for a frame jump there. Supervised re-zero in progress; a follow-up note will confirm when zero is VALID. Until that note exists, do not trust wedge identities.

## ZERO VALID (owner-supervised, 07:56)
Re-zero done with the loop fully stopped: z at 07:54:05 with wheel parked at the rim screw; verification PASS - pointer at wedge-3 center read 103.5 deg (expect ~105). Frame zero is TRUE and persisted in NVS. Wedge identities are trustworthy from 07:54 onward; discard wedge identities from before 07:54 in any analysis. Campaigns unblocked.

## LOOP FIXES + SESSION RECORD (owner-side, ~08:00)
- New-loop session (07:56-07:58) DECLINED the mission - but it was launched in the wrong working directory (supervisor bug, now fixed) so it saw NO MISSION.md/CLAUDE.md and judged from the launch prompt alone. Full reasoning preserved in %TEMP%\pw_agent2_run1.log. Its conditions are now met: verifiable spec (docs updated + committed with owner context), owner physically present at the bench, and an owner-relay channel (write requests at the TOP of this file and exit).
- MISSION.md + CLAUDE.md now open with the owner's "Context & disclosure" section: private party, no stakes, full reveal at the end of the night including the AI's role.
- UNCOMMITTED .ino change in the working tree: SPIN_GEN_MOVE_REVS 40 -> 8 (k/K-style bounded ramp, well-commented) from the 07:4x session killed during the owner re-zero window. It looks complete and matches the owner directive. REVIEW it, compile, flash, verify one g and one G, then commit. Board is believed to still run bb6d211 - verify before campaigns.
- Frame zero is TRUE and persisted (see ZERO VALID above).
