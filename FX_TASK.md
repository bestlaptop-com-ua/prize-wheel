# FX_TASK â€” MP3 Playback + WS2812B Illumination

**Base:** branch from `fix/landing-distribution` (commit `e2d8b29`). Create branch `claude/fx-v1`.
**Sketch:** `prize_wheel_gpt/prize_wheel_gpt.ino` â€” the owner-accepted production baseline.

## Mission context
This is a party prop (see MISSION.md / CLAUDE.md): a concealed steering prize wheel,
revealed as a trick at the end of the night. FX exists to make the wheel feel like a
normal, lively prize wheel and to mask motor engagement. Naturalness is a hard
requirement equal to the never-land-on-dare guarantee.

## Non-negotiable constraints
1. **Do not modify control logic.** The 12-state machine, spin classification,
   reservation, capture, braking, fault handling, and all timing constants are
   owner-accepted and frozen. FX is a pure consumer.
2. **Never block the control tick.** The 25 ms command tick and the encoder
   sampling loop must be unaffected. No `delay()`, no waiting for DFPlayer
   ACKs, no long LED renders in the control path. Budget: FX work â‰¤ 2 ms per
   loop pass, measured (add a `micros()` max-tracker printed by a serial cmd).
3. **No new I2C traffic.** AS5600 bus timing stays untouched (open transport-
   corruption investigation).
4. **RMT conflict check (critical).** FastAccelStepper on ESP32 core 3.3.10 may
   use RMT; WS2812B libraries (FastLED/NeoPixel) default to RMT too. Before any
   LED code: determine FAS driver engine actually in use, then either pin the
   LED library to a free RMT channel or use the I2S output method
   (NeoPixelBus `Neo800KbpsMethod` I2S variant). Prove no step-pulse
   interference with a controlled-spin regression before proceeding.
5. **Flash + bench-test after every commit** per HANDOFF_README workflow
   (arduino-cli, COM3, stop serial bridge first, â‰¥5 s reopen delay).

## Architecture: event bus
Control code gains ONLY minimal event emission â€” one-line calls at existing
state transitions, appending to a small ring buffer (no allocation, no I/O):

```
enum FxEvent { FX_BOOT, FX_IDLE_ENTER, FX_SPIN_CONFIRMED, FX_WEDGE_CROSS,
               FX_RELEASE_DETECTED, FX_CAPTURE_START, FX_DECEL_PHASE,
               FX_LANDED_SAFE, FX_GUEST_STOPPED, FX_FAULT };
```
`FX_WEDGE_CROSS` carries wedge index + signed direction; `FX_LANDED_SAFE`
carries winning wedge index. A new module `fx.cpp/h` (or `.ino` tab) drains the
buffer in `loop()` and drives audio + LEDs. FX failures must never latch a
control fault â€” wrap in its own error state, degrade silently.

## Phase 1 â€” DFPlayer Mini (audio)
**Wiring (document in README):** DFPlayer on UART2 â€” ESP32 GPIO17 (TX2) â†’
1 kÎ© series resistor â†’ DFPlayer RX; DFPlayer TX â†’ GPIO16 (RX2, optional, may be
left unread); VCC 5 V, GND common with ESP32; speaker on SPK_1/SPK_2.
Do NOT touch the TMC2209 UART.

**Driver rules:**
- Raw 10-byte serial frames or DFRobotDFPlayerMini in fire-and-forget mode.
  Never `waitAvailable()`; never read ACKs synchronously.
- Global command rate limit: â‰¥120 ms between commands.
- Volume set once at boot (start ~20/30), adjustable via serial cmd `VOL n`.

**SD layout `/mp3/`:**
| Track | File | Content |
|---|---|---|
| 1 | 0001.mp3 | wedge tick, ~80 ms dry click |
| 2 | 0002.mp3 | continuous ratchet loop, ~3 s, seamless |
| 3 | 0003.mp3 | drumroll loop |
| 4 | 0004.mp3 | win fanfare, 2â€“3 s |
| 5 | 0005.mp3 | idle ambience loop (optional) |
| 6 | 0006.mp3 | sad-trombone / guest-stopped jingle (optional) |

**Sound logic (the concealment core):**
- Speed â‰¥ ~0.5 rev/s: loop track 2 (ratchet) â€” discrete ticks can't keep up.
- Speed < ~0.5 rev/s: one tick (track 1) per FX_WEDGE_CROSS, rate-limited;
  this band overlaps motor engagement and masks residual hum/steps.
- FX_CAPTURE_START â†’ no audible change (silence would be a tell; ticks continue).
- FX_LANDED_SAFE â†’ short pause (~400 ms) then fanfare.
- FX_GUEST_STOPPED â†’ track 6 if present, else nothing.
- FX_FAULT â†’ audio stops (no scary noises).
Hysteresis on the 0.5 rev/s crossover (Â±0.05) so it doesn't chatter.

**Hardware on hand:** DFPlayer Mini and WS2812B strip both available. **Owner action:** source/record the six MP3s (44.1 kHz, mono OK). Firmware work
can proceed with placeholders; leave TODO in README.

## Phase 2 â€” WS2812B (after Phase 1 accepted)
- Strip mounts on the STATIONARY rim (no slip ring). LED count set by a
  `#define NUM_LEDS`; owner will confirm count after mounting.
- Angleâ†’LED mapping calibrated by two constants (LED index at wedge-0
  boundary, direction sign); add serial cmd to nudge and persist in NVS
  (`prizewheel` namespace, new key â€” open read-write, never read-only).
- Behaviors: idle attract breathe; spin = comet chase tracking real encoder
  angle and direction; decel = chase collapses toward landing region;
  landed = winner-wedge segment flash synced to fanfare; fault = dim steady
  (never blinking red â€” no alarm looks).
- Frame rate â‰¤ 60 fps, render budget within the 2 ms FX allowance; if the
  strip is long enough to exceed budget, halve fps before anything else.

## Acceptance
1. Regression: seven consecutive controlled spins, both directions, all
   `CONTROLLED_SAFE`, landing error < 2Â° â€” identical to bench-working-v2
   behavior with FX active.
2. Serial-logged max loop time with FX active vs baseline: no control-tick
   deadline misses.
3. Audio: ratchetâ†”tick crossover inaudible as a "mode switch"; ticks continue
   seamlessly through capture (verified by ear at 1 m).
4. No AS5600 frame-jump regression (run existing 13-move accumulation test).
5. Report deltas in REDESIGN_REPORT.md appendix; do not rewrite the report.

