# LED_HANDOFF - addressable LED bezel for the prize wheel

Status 2026-08-06: hardware fully verified on the bench, firmware written and
proven on the strip, integration NOT yet present in the working firmware.
Read the "Integration state" section before touching anything - the code was
integrated into the wrong sketch and must be re-ported.

## Hardware (verified, do not re-derive)

- Strip: WS2812B, **300 LEDs** (sold/labeled as 144 - measured 300 by meter
  marks + last-6 test), 5 V, colour order **GRB**, ~60/m, 5 m.
- Mounting: wrapped as a **helix around the pole**, top to bottom. Travel along
  strip index reads as rotation; effects are written with that assumption.
- Connector: 3-pin JST (red = 5 V, **green = DATA**, white = GND) plus a loose
  red/white pigtail = **far-end power injection**. The pigtail goes back to the
  buck output terminals; without it the far end browns at high load.
- Data: **GPIO4** -> green. Direct 3.3 V drive verified working across all 300
  at full red - no level shifter fitted. If far-field flicker ever appears, the
  fix is a 1N4001 in series with strip 5 V (drops VDD so 3.3 V logic is in spec).
- Free pins that remain after this claim: 13, 14, 18, 19, 23, 32, 33 out;
  35, 36, 39 input-only (reserved: Hall sensor). DFPlayer plan owns 32/33/34.

## Power & grounding (this topology fixed a real fault - keep it)

- Supply: 24 V PSU -> 24->5 V 5 A buck. Buck stayed **cold** at full-red load.
- Software cap: `FX_MAX_MA 3000` in integrated firmware (bench sketch ran 4000;
  3000 leaves room for TMC logic + DFPlayer on the same buck).
- Star ground, exactly this:
  - Strip GND (connector AND injection pigtail) -> **buck output negative**.
  - ESP32 GND -> buck output negative, **its own separate wire**.
  - TMC2209 **power** GND (pin beside VM) -> **24 V PSU negative** (heavy wire).
  - TMC **logic** GND -> ESP32 GND (thin wire, signal reference, no load current).
  - AS5600 GND stays on ESP32 GND. Never bundle strip power with encoder wiring.
- History: during the ground rework the TMC UART went down
  (`test_connection: 2`) -> driver ran at default current -> rattling, weak,
  late takeover. It recovered after reseating/power-cycle. **The driver-side
  UART joint is a known intermittent** - reflow before the party, and consider
  a boot guard: if test_connection != 0, latch FAULT loudly instead of running.

## Firmware architecture (the part that is non-negotiable)

`FastLED.show()` at 300 LEDs **blocks for 10.6 ms** (measured, n=50, RMT5).
The control loop polls the AS5600 every iteration; a 10.6 ms stall = ~9.5 deg
of unsampled travel at launch speed. Therefore:

- All rendering runs in a **FreeRTOS task pinned to core 0**
  (`xTaskCreatePinnedToCore(fxTask, "fx", 4096, nullptr, 1, nullptr, 0)`).
  The control loop keeps core 1. There is no WiFi/BT, so core 0 is free.
- Cross-core traffic is **volatile scalars only** (`fxMode`, `fxOmega`,
  `fxCelebrateAt`, `fxEnabled`) written by `fxNotify()` - plain stores, no
  locks, no allocation, nothing the control loop can block on.
- 50 fps via `vTaskDelayUntil`. FastLED uses the RMT5 driver on ESP32 core
  3.x - do NOT switch to bit-bang paths, they disable interrupts per frame.

## Effects (as written in fx_leds.h)

- **Standby**: two slow counter-drifting sine hue waves, brightness 55.
  Ambient, low contrast.
- **Spin**: 5 colour bands travelling along the helix; speed AND direction come
  from the live encoder `omega`, so the lights track the real wheel - including
  through takeover. This is a naturalness feature: the lights can never
  contradict the wheel. Band-speed constant (1.6) is a guess, untuned at speed.
- **Celebrate**: on landing, 3 s of full-strip colour slams alternating with
  white sparkle, decaying back to standby. Brightness 210. Triggered on the
  transition into the DONE-equivalent state.
- **Fault**: slow red pulse.
- Strip behaviour note: WS2812 pixels **latch their last frame** while powered.
  ESP32 reset without cutting buck power leaves the strip lit/frozen - this is
  normal, not evidence code is running. Floating GPIO4 can light random pixels.

## Integration state - READ THIS FIRST

The integration was done into `prize_wheel/prize_wheel.ino` (branch
`feat/led-fx`, now in stash "led-fx WIP") - **that is the wrong sketch**. The
firmware actually running on the wheel is `prize_wheel_gpt/prize_wheel_gpt.ino`
(1450 mA RMS, 12-state controller). The prize_wheel branch head has the 180 mA
soft-capture redesign and drives the wheel badly - do not use it as the base.

To integrate into prize_wheel_gpt:

1. Copy `fx_leds.h` (recover from stash `led-fx WIP` on `feat/led-fx`, or
   rewrite from this doc) into `prize_wheel_gpt/`.
2. `#include "fx_leds.h"` at top; `fxBegin();` at end of setup();
   `fxNotify(<fx mode>, <omega>);` once at the end of loop().
3. **The FX_* enum in fx_leds.h mirrors the OLD sketch's 9-value Mode enum.**
   prize_wheel_gpt has a different (12-state) controller - the numeric mirror
   is INVALID there. Write an explicit mapping function from gpt states to
   {standby | spinning | celebrate | fault} instead of casting the enum. The
   celebrate trigger = transition into the gpt state that means "landed,
   result being read" (it has a post-stop hold phase - fire at its start).
4. Confirm the gpt sketch's omega variable name/units (rev/s signed) before
   passing it.
5. First-flash checks: FastLED RMT5 init lines appear in boot log; standby
   waves at idle; bands reverse with spin direction; celebration on landing;
   and a hand-spin log confirming takeover behaviour is unchanged.

## Interaction with the open encoder investigation

The strip is a multi-amp load switching with fast edges sharing a ground system
with the AS5600 - a plausible aggravator (or red herring) for the discrete
silent I2C frame jumps. Rules:

- Take the encoder forensics baseline **before** enabling LEDs in a session
  when possible; compare with strip on/off if jumps change character.
- If jumps worsen after LED enable, unplug the strip before touching firmware.
- Keep strip power leads twisted and away from the encoder cable; cross at 90 deg.

## Bench tools

- `C:\Users\4urka\Desktop\pw_led_test\pw_led_test.ino` - standalone test/effect
  sketch. Commands over serial 115200: o off, w walk, c count(first/mid/last),
  m meter marks, e last-6, 1/2/3 fills, x full white, r/k/t/p/s effects,
  d demo cycle, z measures show() blocking time, i power estimate, +/- bright.
- Verified with it: 300 count, GRB order, uniform red at full length, cold buck,
  10.6 ms show() cost.
