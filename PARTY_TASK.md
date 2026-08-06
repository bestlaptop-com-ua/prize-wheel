# PARTY_TASK — Complete Party Firmware (competitive delivery)

Two agents receive this task independently. Each delivers a complete,
compile-verified firmware plus documentation on its OWN branch. The owner
compares and flashes the winner. Party night is TOMORROW: bias every decision
toward robustness over ambition.

**Mission context:** read MISSION.md / CLAUDE.md first. This is a stage-magic
party prop with a reveal at the end; the "dare" wedges are jokes among
consenting friends. Naturalness of the wheel is a hard requirement equal to
the never-land-on-dare guarantee.

**Base:** branch from `fix/landing-distribution` at `f5419be`.
**Branch name:** `<agent>/party-v1` (e.g. `codex/party-v1`, `claude/party-v1`).
**Sketch:** `prize_wheel_gpt/prize_wheel_gpt.ino`.
Read REDESIGN_REPORT.md, FX_TASK.md, WIFI_TASK.md, HANDOFF_README before code.

## Hard rules
1. **Control logic is frozen.** The 12-state machine, spin classification,
   reservation, capture, braking profile, guest detection, fault handling and
   their constants are owner-accepted. No changes except the three Sanctioned
   Fixes below. Any other control-path edit disqualifies the delivery.
2. **Do NOT flash, open COM3, or touch the device.** Deliverable is source +
   docs, compile-verified. The owner flashes and bench-tests the winner.
3. Mechanics will NOT change before the party. Firmware must behave across
   the measured friction band fricC 0.22–0.55 (it drifted that much in one
   evening; treat drag as environmental, not fixable).
4. Loop budget: FX + WiFi combined ≤ 2 ms per loop pass worst case, measured
   by a micros() max-tracker exposed via a serial/telnet command. The 25 ms
   control tick and AS5600 I2C timing are untouchable.
5. FX and WiFi fail silent: their errors must never fault the wheel.

## Part 1 — Firmware risk audit (RISK_AUDIT.md)
Analyze the CURRENT firmware for errors and failure points. For each item:
severity (party-ruining / visible / cosmetic), likelihood, symptom the owner
would observe, and mitigation (code fix if sanctioned, else procedure).
Verify rather than trust; read the code. Seed list from tonight's bench:
- Weak spins (natStop < ~400°) fall to Pass 2/3: motor drives ~98% of coast
  (visible) and a feeble spin can still reach a dare. Owner rejected
  honest-stop gating. Audit + procedural mitigation only.
- Fault latch is RAM-only: a power cycle after LANDED-DARE silently clears
  it (observed tonight). See Sanctioned Fix S1.
- TMC2209 UART is electrically marginal: three dropouts tonight; symptom is
  rattle (driver loses config) and degraded landings while test_connection
  can still pass. See Sanctioned Fix S2.
- Friction estimator instability: 1-in-6 fits rejected with negative fitB;
  fricC wandered 0.22→0.50 in one session. Rejection is harmless (keeps old
  values) but staleness under shifting drag is a risk. See S3.
- Capture-surge residual: first fast spin after boot showed rise=0.068
  (spin#3, 01:47) while later identical spins were 0.000. Investigate
  initialization (lastAppliedHz, fitter reload, aFas application order).
  Audit; fix only if the cause is provable and outside the frozen core.
- NVS trap: read-only begin() silently returns defaults (documented in repo).
- Historical AS5600 frame jumps (33–255°, non-accumulating) — transport
  suspect; audit what a mid-party jump would do in each state.
- Anything else you find. Depth of this audit is a judging criterion.

## Sanctioned Fixes (each behind its own #define, default ON)
- **S1** Persist the fault latch in NVS (`prizewheel` namespace, open
  read-write); boot restores a latched fault; only the `r` command clears it.
- **S2** Periodic TMC config verify (e.g. every 5 s while IDLE: read back a
  written register); mismatch → existing TMC_UART fault path. No new fault
  types.
- **S3** Append `fitRej=<n>` and current fit counts to the SPIN SUMMARY line.

## Part 2 — WiFi (per WIFI_TASK.md, unchanged)
SoftAP + telnet mirror + command channel, fail-silent, no OTA.

## Part 3 — FX (per FX_TASK.md, with these updates)
- DFPlayer Phase 1 AND WS2812B Phase 2 are both in scope for this delivery.
- **Generate the MP3 files yourself** — the owner has no time to source
  audio. Synthesize with a committed script (Python/ffmpeg/sox):
  `media/generate_mp3.*` producing `media/mp3/0001..0006.mp3`, 44.1 kHz
  mono, total < 2 MB: 0001 dry tick ~80 ms; 0002 seamless ratchet loop ~3 s;
  0003 drumroll loop; 0004 win fanfare 2–3 s; 0005 idle ambience (optional);
  0006 guest-stopped jingle (optional). Owner copies /mp3 to the microSD.
- RMT-vs-FastAccelStepper conflict must be resolved and the resolution
  documented (pin channels or NeoPixelBus I2S), with reasoning, since it
  cannot be hardware-proven before flash.

## Deliverables on your branch
1. Complete firmware (single .ino or tabs) — compiles clean.
2. RISK_AUDIT.md (Part 1).
3. media/ (MP3s + generator script).
4. Updated README section: wiring for DFPlayer (UART2 16/17, 1 kΩ), WS2812B
   (data pin, stationary rim), WiFi credentials define.
5. DELIVERY.md: what was built, compile proof (exact arduino-cli command,
   core esp32 3.3.10, library list + versions, flash/RAM byte sizes), every
   #define an owner might toggle, known limitations, and a 15-minute
   bench-test script for the owner (ordered checks with expected output).
If your environment lacks the ESP32 toolchain, say so explicitly in
DELIVERY.md and deliver anyway — but a verified compile scores higher.

## Judging criteria, in order
1. Compiles for esp32:esp32:esp32 (core 3.3.10).
2. Zero unauthorized control-path diffs (we diff against f5419be).
3. Risk-audit depth and honesty.
4. FX/WiFi spec compliance incl. budgets and fail-silent proofs.
5. Code clarity and the quality of the owner bench-test script.
