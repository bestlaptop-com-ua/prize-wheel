# DELIVERY — Claude Code party firmware (`claude/party-v1`)

Delivered against PARTY_TASK.md from base `fix/landing-distribution`
(`dcb804c`, control core frozen at `f5419be`). Everything below was built and
compile-verified on the owner's own toolchain (the bench laptop's arduino-cli,
esp32 core 3.3.10) — no device contact of any kind: COM3 was never opened, the
serial bridge and Codex's checkout were left untouched.

## What was built

| Part | Delivered as |
|---|---|
| 1. Risk audit | `RISK_AUDIT.md` — 16 findings incl. the seeded list, each with severity/likelihood/symptom/mitigation and line refs; two findings (stale DFPlayer pinout §8, telnet `z`/`e` sabotage §9) are new |
| 2. WiFi | SoftAP `PW-XXXX` + telnet mirror of every serial line (4 KB ring, 2 KB catch-up, drop-oldest, never blocks) + same single-char command parser with all state guards; fail-silent; no OTA |
| 3a. Audio | DFPlayer Mini on **UART1 GPIO32/33** (FX_TASK's "UART2 16/17" is the TMC UART on this board — corrected, see README + RISK_AUDIT §8); fire-and-forget 10-byte frames, ≥120 ms command gap, ratchet↔tick crossover at 0.55/0.45 rev/s with ticks continuing through capture; fanfare 400 ms after a safe rest; guest-stop jingle; fault = silence |
| 3b. LEDs | WS2812B ×300 helix on GPIO4 per LED_HANDOFF.md: standby waves / omega-tracking bands / 3 s celebrate synced to the fanfare / dim-steady fault (FX_TASK override), 50 fps FreeRTOS task pinned to core 0, 3000 mA software cap |
| S1 | Fault latch persisted in NVS (`prizewheel/fltLatch`), restored at boot, cleared only by `r` (and the attended probe for DIR_CAL, mirroring RAM latch semantics) |
| S2 | 5 s CHOPCONF readback while IDLE (double-read glitch tolerance) → existing `FC_TMC_UART` path; `r` re-applies `driverConfig()` and verifies by readback before clearing |
| S3 | `fitRej=<n> fitsCW=<n> fitsCCW=<n>` appended to every SPIN SUMMARY line |
| Budget proof | `micros()` max-tracker over the FX+WiFi work per loop pass (S2 and full-loop tracked separately), serial/telnet command `t` |
| Media | `media/generate_mp3.py` (pure-stdlib synthesis, deterministic; encodes via ffmpeg or `pip install lameenc`) + committed `media/mp3/0001..0006.mp3`, 44.1 kHz mono 96 kbps, 205 KiB total |

### Files
- `prize_wheel_gpt/pw_party.h` — config switches, declarations, serial mirror.
- `prize_wheel_gpt/pw_party_impl.h` — all implementations; included at the very
  bottom of the sketch so it observes control globals without forward-decl
  surgery.
- `prize_wheel_gpt/prize_wheel_gpt.ino` — minimal edits, itemized below.

## Frozen-core diff statement (judging criterion 2)

Every edit to `prize_wheel_gpt.ino`, and why it is authorized:

| Site | Edit | Authorization |
|---|---|---|
| includes | `#include "pw_party.h"` (top), `#include "pw_party_impl.h"` (bottom) | integration, no control effect |
| `enterFault()` | `PW_S1_PERSIST(code);` after the latch is set | S1 |
| `startDirectionProbe()` | `PW_S1_CLEAR();` beside the existing `faultCode = FC_NONE` | S1 (mirrors RAM latch lifecycle) |
| `r` handler | `PW_S1_CLEAR()`; `pwS2ReconfigVerify()` gate before clearing | S1, S2 |
| `closeSpin()` | S3 suffix on the SUMMARY printf | S3 |
| `finishFrictionCapture()` | `PW_S3_COUNT_REJECT();` after each existing REJECT print | S3 |
| `DIAG_CAPACITY` | 2944 → 1664 while `PW_WIFI_ENABLE` (else 2944) | instrumentation, not control: the WiFi stack's static DRAM (~31 KB measured link overflow) cannot coexist with the full bench buffer; see "RAM" below |
| `handleSerial()` | split into `handleSerial()` + `handleCommandChar(char)`; party commands intercepted first | WIFI_TASK requires the shared parser ("refactor to handleCommandChar(c)") |
| `help()` | `pwPartyHelpLines();` appended | UI only |
| `setup()` | `pwPartyBegin();` as the last line | integration |
| `loop()` | `micros()` stamp first line, `pwPartyService(...)` last line | PARTY_TASK rule 4 (budget tracker) + integration |

No state-machine, classification, reservation, capture, braking, guest
detection or fault-handling code or constants were changed. FX events are
derived by observing `state`/`spin`/`spinOpen` once per loop pass — zero event
emission calls inside control code.

## Compile proof

Exact command (the owner's toolchain, sketchbook config):

```
C:\Scripts\prize_wheel\tools\arduino-cli.exe compile
  --config-file C:\Scripts\prize_wheel\arduino-cli.yaml
  --fqbn esp32:esp32:esp32 --warnings all
  C:\Users\4urka\Desktop\pw_claude_party\prize_wheel_gpt
```

- Platform: `esp32:esp32 3.3.10`
- Libraries resolved by the build: TMCStepper **0.7.3**, FastAccelStepper
  **1.2.7** (both the sketchbook copies already proven on this wheel), FastLED
  **3.10.5** (copied verbatim from the OneDrive Arduino libraries — the exact
  version the owner bench-verified on this strip), Wire / Preferences / SPI /
  WiFi / Networking from core 3.3.10.
- Result:

```
«COMPILE_RESULT»
```

- Warnings: «WARNINGS_SUMMARY»

## RAM note (the one real trade-off)

The WiFi/Network stack adds ~31 KB of static DRAM. The frozen sketch's 80 KB
diagnostics ring was deliberately sized to ~2 KB under the segment limit, so
the first party build failed to link (`dram0_0_seg overflowed by 31192 bytes`
— recorded honestly). Resolution: `DIAG_CAPACITY` 2944 → **1664** (~1.66 s of
1 kHz capture) while WiFi is compiled in; a bench build with
`PW_WIFI_ENABLE 0` restores the original 2944. Landing-phase captures still
fit; full-spin captures need the no-WiFi bench build.

## Every #define an owner might toggle (`pw_party.h`)

| Define | Default | Meaning |
|---|---|---|
| `PW_S1_ENABLE` / `PW_S2_ENABLE` / `PW_S3_ENABLE` | 1 | sanctioned fixes |
| `PW_WIFI_ENABLE` | 1 | SoftAP+telnet; 0 also restores full diag buffer |
| `PW_FX_AUDIO_ENABLE` / `PW_FX_LED_ENABLE` | 1 | FX subsystems |
| `PW_WIFI_PASSWORD` | `"spinthewheel"` | **change before flashing** |
| `PW_WIFI_CHANNEL` / `PW_WIFI_HIDDEN` | 6 / 0 | AP radio settings |
| `PW_TELNET_COMMANDS` | 1 | 0 = telnet telemetry-only (RISK_AUDIT §9) |
| `PW_DFP_VOLUME` | 20 | boot volume 0–30 (live: `V<n>`) |
| `PW_FX_IDLE_AMBIENCE` | 0 | loop track 5 while idle |
| `PW_DFP_TX_PIN` / `PW_DFP_RX_PIN` | 32 / 33 | DFPlayer UART1 pins |
| `PW_NUM_LEDS` | 300 | strip length (measured) |
| `PW_FX_MAX_MA` | 3000 | FastLED power cap |
| `PW_LED_FPS` | 50 | halve first if budget issues (FX_TASK rule) |
| `PW_FX_RATCHET_ON/OFF_REV_S` | 0.55 / 0.45 | audio crossover hysteresis |

## Known limitations (honest list)

1. **Weak-spin physics floor unchanged** — owner rejected gating; procedural
   mitigation only (RISK_AUDIT §1: the respin house rule).
2. **S2 is blind during motion** — a TMC dropout mid-spin latches only when
   back at IDLE; readback mid-control would corrupt the 1 kHz cadence.
3. **Diag buffer 1664 samples with WiFi** (above).
4. **DFPlayer clone variance**: if bench check 8 shows the ratchet playing
   once instead of looping, the module ignores `0x19`; fallback = timed
   re-trigger (the loop files are exactly periodic) — two-line change in
   `pwFxService` marked by the `PW_DFP_CMD_LOOP_CUR` queue calls.
5. **LED effects rewritten from LED_HANDOFF.md**, not recovered from the
   `led-fx WIP` stash (which lives only in the other checkout and targets the
   wrong sketch); band-speed constant 1.6 is the doc's untuned guess.
6. **FX_TASK Phase-2 wedge-segment mapping is N/A on this hardware**: the
   strip is a helix around the pole (LED_HANDOFF), not a rim ring, so
   angle→LED wedge mapping has no physical meaning; the "winner flash" is the
   full-strip celebrate synced to the fanfare.
7. **WiFi and LEDs share core 0**: heavy telemetry can drop LED frames
   (cosmetic; control on core 1 unaffected — `t` proves it).
8. **Telnet `d` dumps** wrap the 4 KB ring; use USB for diag dumps.
9. **Compile warnings are library-internal only** (FastAccelStepper MCPWM
   structs — same set REDESIGN_REPORT documented — plus FastLED unused
   platform helpers); the sketch and party modules compile clean.
10. **Nothing here was hardware-tested** — compile-verified only, per the
    task's no-device rule. The bench script below is the acceptance gate.

## 15-minute owner bench script

Prereqs: winner flashed over USB, serial console at 115200, wheel free, phone
nearby. Keep the serial bridge stopped while a terminal owns COM3.

1. **(1 min) Boot.** Expect, in order: `# AS5600 primed`, `# frame:
   label-true ... dirCal=VALID`, friction line, help text with the new
   `--- party additions ---` block, `# fx: DFPlayer on UART1 tx=32 rx=33`,
   `# fx: WS2812B x300 on GPIO4 ... (FAS=MCPWM, LEDs=RMT: no channel
   conflict)`, `# net: SoftAP PW-XXXX up, telnet 192.168.4.1:23`,
   `# party build: S1=1 S2=1 S3=1 wifi=1 audio=1 leds=1`. **No** `BROWNOUT`
   warning. LEDs: standby waves within ~1 s.
2. **(30 s)** `s` → `state=IDLE_STOPPED fault=NONE ... tmc=1 takeover=1`.
   `t` → `# budget: fx+wifi max=...` with max well under 2000 µs.
3. **(2 min) Phone.** Join `PW-XXXX`, `telnet 192.168.4.1` → banner + recent
   log replay. Send `s` from the phone: status appears on phone AND USB.
   `w` → `clients=1`.
4. **(2 min) Audio regimes.** `V25` + Enter → `# audio volume -> 25`. Rotate
   the wheel slowly by hand: one dry tick per wedge line. Strong hand-spin
   (no control needed yet — `e` off if desired): ratchet loop engages at
   speed, hands back to discrete ticks as it slows; the crossover should not
   read as a "mode switch" at 1 m. Confirm the ratchet still sounds after
   >3 s at speed (loop works; if it stops → limitation 4).
5. **(3 min) Controlled spins.** Two per direction, vigorous. Expect the full
   `START → RELEASE → RESERVE → CAPTURE → SUMMARY` chain; ticks continue
   through capture; ~0.4 s after rest → fanfare + LED celebrate; SUMMARY now
   ends `... fitRej=N fitsCW=N fitsCCW=N`. Landing error and feel identical
   to the pre-party build.
6. **(1 min) Budget under load.** With the phone streaming during a spin:
   `t` → fx+wifi max < 2000 µs, `over2ms=0`.
7. **(2 min) S1 proof.** Spin the wheel gently and unplug the AS5600
   connector mid-motion → `FAULT code=ENCODER_STALE` latches. Reconnect.
   Power-cycle the ESP32. Boot must print `# S1: latched fault ENCODER_STALE
   restored from NVS; takeover locked until r`. A hand spin now closes as
   `CONTROL_LOCKED` (observed, not controlled). `r` → `# S2: driver config
   re-applied and verified`, `# fault cleared`. One controlled spin to
   confirm recovery.
8. **(2 min) S2 proof** (skip if the driver can't be power-cycled alone):
   while IDLE, briefly cut the TMC's supply so it reboots to defaults.
   Within ~10 s: `# S2: TMC config verify FAILED (microsteps=256,256 expected
   16)` + `FAULT code=TMC_UART`. `r` recovers as above.
9. **(1 min) Fail-silent WiFi.** Start a controlled spin, kill the phone's
   WiFi mid-spin: the spin completes normally; reconnect: catch-up replays the
   missed lines.
10. **(30 s) Wrap.** `f` (fit counts grew), `w` (heap stable vs step 3 — no
    leak), `t` once more. Done.

Pass = every expected line seen, no unexpected FAULT, budget under 2 ms,
S1/S2 behave. Any deviation: note the exact serial line and compare against
RISK_AUDIT.md before touching anything.
