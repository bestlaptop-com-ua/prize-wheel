# RISK_AUDIT — party firmware, branch `claude/party-v1`

Audit target: `prize_wheel_gpt/prize_wheel_gpt.ino` at `f5419be` (frozen control
core) plus the party additions on this branch. Line references are to this
branch's copy of the sketch. Every claim below was verified by reading the code,
not inherited from the docs; where hardware evidence would be required, that is
said explicitly.

Severity scale: **PARTY-RUINING** (dare rest, latched dead wheel, wrong wedge
identity), **VISIBLE** (guests can notice something unnatural), **COSMETIC**
(log/telemetry only).

---

## 1. Weak spins: assist passes are visible; a dying spin can still reach a dare
**Severity: PARTY-RUINING (dare case) / VISIBLE (assist case) · Likelihood: moderate for timid guests**

Verified in `chooseSafeTarget()`: when the wedge-uniform window never opens
(weak release), Pass 2 "shadow capture" reserves `naturalDeg - 2°` of runway
(line 1142) — the motor is engaged for essentially the whole remaining coast
at near-natural deceleration. Pass 3 (line 1176) brakes at up to
`ASSIST_DECEL_MAX_SPS2 = 1100` — measurably steeper than natural coast.
Symptoms the owner would observe: a slow spin that "finds another gear" of
smoothness (Pass 2, faint hum/steps for seconds), or a visibly firm slow-down
(Pass 3).

The floor case is documented and real (REDESIGN_REPORT §6.1): a release at
≲0.13 rev/s whose assist band *and* natural stop both lie in/near a dare wedge
closes honestly as `NO_REACHABLE_SAFE` and can physically rest on a dare
(`LANDED-DARE`, line 1390). The owner rejected honest-stop gating, so **no code
change is possible**.

**Procedural mitigation (party runbook):**
- Announce a house rule up front: *"limp spins get a respin"* — guests accept
  this as normal wheel etiquette, and it converts the dangerous corner into a
  fun rule instead of a tell.
- The operator (owner) watches the wheel, not the log: any spin that dies
  within ~1 revolution should be laughingly re-spun before it settles.
- FX helps here: wedge ticks continue through assist braking (masking), and the
  ratchet→tick crossover hides motor engagement noise.

## 2. Fault latch was RAM-only → **fixed (S1)**
**Severity was: PARTY-RUINING · Likelihood: high (observed tonight)**

Power-cycling after `LANDED-DARE` silently cleared `FC_LANDING_UNSAFE` and
re-armed automatic control with the hardware condition that caused the failure
still present. **S1** persists the latch (`prizewheel/fltLatch`, written inside
`enterFault()`), restores it at boot (`pwPartyBegin()`), and clears it only
where the RAM latch cleared before: the `r` command, and the attended probe
start that recovers `FC_DIR_CAL_INVALID` (mirroring existing semantics
exactly). Residuals, stated honestly:
- A newer fault overwrites the stored code (same as the RAM latch).
- The NVS write adds ~1–10 ms inside `enterFault()`; control is already ending
  there, and the coils/pulse ramp-down runs in FastAccelStepper hardware, so
  the delay has no mechanical effect.
- If NVS itself is unwritable the persist fails silently — the bench script
  includes an S1 power-cycle test to prove it works on the real board.

## 3. TMC2209 UART is electrically marginal → **mitigated (S2)**
**Severity: VISIBLE→PARTY-RUINING · Likelihood: high (3 dropouts tonight)**

The failure mode that matters is *config loss with comms alive*: a driver
brown-out/reset returns it to default registers (MRES=0 → 256 µsteps, default
current) while `test_connection()` (line 881) still passes. Result: rattle,
weak coupling, degraded landings — observed on the bench. The frozen core only
health-checks at boot, at `r`, and between spins, and never re-applies config.

**S2** adds, while `ST_IDLE_STOPPED` with the wheel quiet, a 5 s CHOPCONF
readback (`driver.microsteps() != 16`), with a second immediate read to
tolerate a single line glitch, latching through the existing `FC_TMC_UART`
path. The `r` handler now also **re-applies `driverConfig()` and verifies by
readback** before clearing — without this, clearing the fault would resume on
a defaults-running driver, which is exactly the party-night trap.

Residuals: a dropout *during* a spin is not detected until the wheel is back at
rest — one degraded-feeling spin can slip through (readback during control is
forbidden: a blocking UART read would corrupt the 1 kHz encoder cadence).
**Procedural:** reflow the driver-side UART joint before the party
(LED_HANDOFF flags it as a known intermittent) and strain-relieve that harness.

## 4. Friction estimator staleness under drifting drag → **instrumented (S3)**
**Severity: VISIBLE · Likelihood: high (fricC 0.22→0.50 in one session)**

Verified: rejected fits (negative/out-of-bounds `fitB`, contact) keep the old
model — harmless per event. The risk is *sustained* rejection or simply too few
coasts while the mechanical drag drifts: reachability windows are then sized
with a stale model, producing more Pass 2/3 spins (visible, see §1) or
early/late-feeling stops. The 1-in-6 bounds rejections observed tonight are the
expected signature of fitting a drifting plant with a 2-parameter model.

**S3** appends `fitRej=<n> fitsCW=<n> fitsCCW=<n>` to every SPIN SUMMARY, so
staleness is visible per spin from a phone over telnet.

**Procedural:** 5–10 vigorous warm-up spins per direction right before guests
arrive (feeds fresh fits at party-night drag); prefer warm-up over `F` (the
seeds c=0.30/b=0.15 are as wrong for a drifted plant as a stale fit is).

## 5. Capture-surge residual (spin#3 rise=0.068) — audited, no provable code fix
**Severity: VISIBLE · Likelihood: low (one occurrence tonight)**

PARTY_TASK asks specifically about `lastAppliedHz`, fitter reload, and aFas
application order. Findings from the code:
- `lastAppliedHz` is freshly assigned in `launchCapture()` on every capture
  before any use in `serviceDecelTick()` — no stale path, boot or otherwise.
- aFas ordering is per-capture and identical every spin: `setSpeedInHz` →
  `setAcceleration` → `setJumpStart` → `setCurrentPosition` → run; FAS latches
  speed/accel at the run call. No first-spin asymmetry in the sketch.
- Two genuine boot-only differences exist:
  1. **Friction model reload**: c/b arrive from NVS reflecting the *end* of
     the previous session under drag that demonstrably wanders 0.22→0.50.
     A stale-low `c` overestimates `naturalStop`, admits a longer runway, and
     makes the capture entry (`0.95 × trailing-min` at the ~0.68 cap) sit
     closer to the wheel than the same spin later in the evening.
  2. **First-ever `runForward()` after power-up initializes the MCPWM/PCNT
     machinery**; if the first pulses start a few ms late, the wheel decays
     below the seeded field and the ramp tail can carry it back up — the
     exact `rise` signature. This is inside FastAccelStepper, not the sketch.
- The surge-fix (zero coupling slack in `SPEED_MATCH_CAPTURE`) is correctly
  present (line ~1953); rise=0.068 also sits above `SPEEDUP_NOISE_REV_S=0.050`,
  so the trim path engaged and the event was recorded honestly.

Neither hypothesis is provable without the bench, and both sit inside the
frozen core / library — **no fix shipped**, per the task's rule.
**Procedural:** after every boot, run one throwaway strong spin per direction
before guest use (also warms the friction fit, §4). Watch `rise=` in SUMMARY.

## 6. NVS read-only trap — verified not present
The sketch opens `preferences.begin("prizewheel", false)` (line 2281) —
read-write — so the documented trap (read-only begin silently returning
defaults) does not apply. S1/S2/S3 reuse the same handle. Residual: a full or
corrupt NVS partition fails writes silently; the bench script's S1 power-cycle
test doubles as an NVS write proof. **COSMETIC · very low.**

## 7. AS5600 frame jumps (33–255°, non-accumulating): state-by-state audit
**Severity: mostly benign / VISIBLE worst case · Likelihood: low, transport-suspect**

The P1 pipeline turns a *single-frame* jump into a rejected sample, in every
state: a 33° jump in one 1 ms frame is ~375 counts against a plausibility bound
of ~49 (`maxAllowed`, line 782) → `DIAG_RATE` reject: position untouched,
velocity invalidated, next genuine sample compared over the true longer
interval. Per state:

| State | Effect of a transient jump |
|---|---|
| IDLE/MANUAL/CANDIDATE | one dropped sample; ~30 ms velocity re-prime; no motion misclassification (confirm logic needs sustained travel) |
| SPIN_PUSH / RELEASED | same; release/reserve decisions stall ≤ tens of ms; friction sampler skips (guarded by `encoderVelocityValid`) |
| TARGET_RESERVED | velocity-wait; bounded by `RESERVED_VEL_TIMEOUT_MS=500` → honest fault if sustained |
| CAPTURE / DECEL | command holds last value; >300 ms sustained loss → `FC_ENCODER_VELOCITY` (line 1783), honest ramp-down + latch |
| LANDING_SETTLE | stillness window restarts; bounded by settle timeout |
| FAULT_LATCHED | bounded close of any open spin record (5 s path) |

The dangerous variant is a *persisting* raw shift after >20 ms of blindness:
`primeEncoder(..., preserveNearestTurn)` (line 764) re-anchors to the nearest
whole turn using the **shifted** raw, which would move wedge identity by the
shift amount — the frame would be silently wrong until re-zeroed. Historical
jumps were single-frame and non-accumulating, so this has never been observed;
it remains the one encoder scenario with no in-code defense (a defense would
require touching the frozen pipeline).

**Procedural:** keep the encoder cable away from the LED strip power run,
crossing at 90° (the strip is a multi-amp fast-edge load sharing a ground
system — LED_HANDOFF flags it as a plausible aggravator); take the bench
baseline `d` capture with the strip on vs off; if jumps change character,
unplug the strip for the party. After any grind/stall event, sanity-check a
known wedge against the pointer before continuing.

## 8. DFPlayer wiring in the task docs is wrong for this firmware ⚠
**Severity: PARTY-RUINING if wired as written · Likelihood: high without this audit**

FX_TASK.md and PARTY_TASK.md say "DFPlayer on UART2 (16/17)". In this firmware
**UART2 on GPIO16/17 is the TMC2209 UART** (`TMC_SERIAL`/`TMC_RX_PIN`/
`TMC_TX_PIN`, sketch top). Wiring a DFPlayer there puts 9600-baud traffic on
the driver's PDN_UART line: config corruption, rattle, spurious `FC_TMC_UART`
latches — the exact §3 failure, permanently.

The delivered firmware puts the DFPlayer on **UART1, TX=GPIO32 → 1 kΩ → RX,
DFPlayer TX → GPIO33** (the pins LED_HANDOFF reserved for the DFPlayer plan;
both verified free in this sketch). **Wire per this branch's README section,
not per FX_TASK.md.**

## 9. Telnet command channel can subvert the trick if the WPA2 password leaks
**Severity: PARTY-RUINING · Likelihood: low (requires password + intent)**

By design (WIFI_TASK) telnet feeds the same parser as serial. That includes
`z` (re-anchors the label frame — silent wrong wedge identities), `e` (disables
takeover), `F`/`x` (clears calibration). A guest who knows the AP password and
port 23 could sabotage the wheel from a phone, invisibly. Mitigations shipped:
WPA2 with `PW_WIFI_PASSWORD` (change it before flashing!), `max_connection=2`,
optional hidden SSID, and a **`PW_TELNET_COMMANDS 0`** compile switch that
makes telnet telemetry-only if the password may have leaked. The existing state
guards (`z` only while idle/stopped) all apply unchanged.

## 10. RMT vs FastAccelStepper — resolved by peripheral disjointness (documented proof)
**Severity: would be PARTY-RUINING · resolved by code analysis**

- FAS 1.2.7 on classic ESP32/IDF5 compiles both engines but
  `stepperConnectToPin(pin)` with the default `DONT_CARE` **tries MCPWM_PCNT
  first** and only falls to RMT when all 6 MCPWM queues are taken
  (`src/pd_esp32/esp32_queue.cpp`, `DONT_CARE` block; `QUEUES_MCPWM_PCNT 6` in
  `src/pd_esp32/pd_config_idf5.h`). This sketch creates exactly one stepper →
  deterministically MCPWM/PCNT. Corroborated twice: the sketch's own comment
  about "FastAccelStepper's MCPWM machinery" (diag buffer sizing), and
  REDESIGN_REPORT's compile warnings located in FAS's MCPWM config structs.
- FastLED 3.10.5 on core 3.3.10 uses the RMT5 (`rmt_tx`) driver — a different
  peripheral block entirely; the owner already bench-measured it on this exact
  strip (10.6 ms `show()`, LED_HANDOFF).

So there is no channel contention *by construction*, not by luck. Residual:
RMT5 buffer-refill ISRs share core 0 with WiFi; under heavy WiFi traffic LED
glitches (not step-pulse interference) are possible — cosmetic, and `l` kills
the strip instantly if it misbehaves. Control (core 1 + MCPWM hardware) is
unaffected. Hardware-proof regression is in the bench script (it cannot be
proven from a container, and PARTY_TASK acknowledges that).

## 11. Core-0 contention: WiFi stack + LED task
**Severity: COSMETIC · Likelihood: certain under load**

LED_HANDOFF's "no WiFi, core 0 is free" assumption no longer holds: the radio
stack and the 50 fps LED task (10.6 ms/frame ≈ 53% of core 0) now share it.
The LED task runs at priority 1 and simply drops frames when preempted; the
control loop owns core 1 and never blocks on either. The `t` max-tracker is the
proof instrument: stream telemetry during a spin on the bench and confirm
FX+WiFi ≤ 2 ms per pass and no control-tick anomalies.

## 12. Power: WiFi TX bursts + 300 LEDs + DFPlayer on one buck
**Severity: VISIBLE→PARTY-RUINING (brownout reboot) · Likelihood: low-moderate**

Worst case stack-up: LED white burst (capped in software at
`PW_FX_MAX_MA 3000`), WiFi TX spike (~400 mA on 3V3), DFPlayer playing, TMC
logic — all from the 24→5 V buck. Mitigations: the 3000 mA FastLED cap
(bench sketch ran 4000 on a cold buck, so ~1 A headroom is real), the far-end
power-injection pigtail **must** be connected (LED_HANDOFF), star ground kept
exactly as documented, and the firmware now prints a `BROWNOUT` warning at
boot if the previous reset was a brownout — so a marginal rail is visible in
the log instead of a mystery reboot. A brownout mid-spin reboots into a safe
state: frame is static NVS (`rawZero`), S1 restores any latch, control locked
until still.

## 13. Serial print latency inside control transitions (pre-existing, unchanged)
**Severity: COSMETIC · Likelihood: rare**

`RESERVE`/`CAPTURE` printfs (~150–180 chars) block ~13–16 ms at 115200 once the
128-byte UART FIFO fills — inside the control path, between encoder samples.
The 1 kHz sampler tolerates gaps ≤ 20 ms (`ENCODER_MAX_GOOD_GAP_US`), so a
print plus an unlucky I2C stretch can occasionally force a long-gap re-prime at
capture start. This is frozen-core behavior, bench-proven through all prior
campaigns; noted so nobody "fixes" it party night. The telnet mirror adds only
a RAM ring copy (µs) to each print — it does not lengthen UART blocking. Keep
`v` (debug logging) OFF during guest use.

## 14. `d` diagnostic dump floods the loop and the telnet ring
**Severity: COSMETIC · Likelihood: only if misused**

`dumpDiagnostics()` prints ~180 KB; at 115200 that stalls the loop ~16 s at
stillness (pre-existing) and wraps the 4 KB telnet ring ~45× (clients see only
the tail). Use `d` on USB bench sessions, not from a phone mid-party.

## 15. DFPlayer clone variance on the loop command
**Severity: COSMETIC-VISIBLE · Likelihood: moderate (clone modules)**

The ratchet loop uses `0x12` (play /mp3 by number — immune to FAT copy order)
followed by `0x19 0` (repeat current). Some DFPlayer clones ignore `0x19`.
Symptom: ratchet plays 3 s then goes silent at speed. Bench check 12 verifies
it; if it fails, the loop tracks are exactly periodic so the fallback is a
timed re-trigger (a two-line change in `pwFxService`, documented in DELIVERY).
Audio is fully fail-silent either way: a dead/absent DFPlayer or missing SD
changes nothing about the wheel.

## 16. Smaller findings
- **micros()/millis() wraparound**: all comparisons are unsigned-subtraction
  form; 71-minute micros wrap is safe. Verified across the party additions too.
- **Spin numbering resets each boot** (frozen, deliberate): correlate logs by
  timestamp, not spin number, across reboots.
- **Deficit-weighted wedge balancing resets at boot** (RAM-only, frozen): after
  a mid-party reboot the anti-clustering restarts cold — cosmetic distribution
  effect only.
- **`GUEST_RESPUN` + audio**: a re-grab mid-control closes the spin silently
  and the new spin re-enters the ratchet/tick regime; quick audio transitions
  are possible but read as natural (wheel was grabbed).
- **AP channel congestion**: a room full of phones can lag telemetry on the
  fixed channel; fail-silent by design, telemetry-only impact.
- **Static RAM headroom**: the diag buffer deliberately sits ~2 KB under the
  DRAM segment limit; the party additions were kept small and the delivered
  build's RAM figure is recorded in DELIVERY.md as the proof.

---

## Priority actions for the owner (before guests)
1. Wire the DFPlayer per README **(UART1 32/33 — not FX_TASK's 16/17!)** (§8).
2. Change `PW_WIFI_PASSWORD`; consider `PW_WIFI_HIDDEN 1` (§9).
3. Reflow the TMC UART joint; strain-relieve (§3).
4. Connect the LED power-injection pigtail; keep the star ground (§12).
5. Run the 15-minute bench script in DELIVERY.md, including the S1 power-cycle
   and S2 config-loss checks.
6. Party runbook: announce the weak-spin respin rule (§1); warm-up spins after
   every boot (§4, §5); keep `v` off (§13); `d` only on USB (§14).
