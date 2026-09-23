# led_test_20260923 - standalone LED strip test

Timur 2026-09-23 14:05: "Led does not work. Flash led test".  The old led48* sketches on the desktop are
from the 8/09 bench (GPIO48, stepper on 4/5/6) and do not match the v2 board, so this is a fresh one.

- Same wiring as the wheel firmware: GPIO40 -> 74AHCT125 -> WS2815 DIN, 300 LEDs, GRB, FastLED WS2812B protocol.
- Motor OFF for the whole test: TMC5160 EN (GPIO7) held high, STEP low, SPI CS high.  The wheel is NOT
  steered while this runs - flash the wheel firmware back afterwards (work/party_20260923 or party3).
- Auto cycle, 3 s each: red, green, blue, white, a travelling orange block, ends (first 3 red, middle 3
  green, last 3 blue).  Brightness 25 %.
- Serial 115200: a auto, r g b w solid, c chase, e ends, 0 off, + / - brightness, ? status.

Reading the result:
- Nothing lights in any step: no 12 V at the strip, no 5 V at the 74AHCT125, the data wire, or the first
  pixel is dead (it would block the whole strip).  Check the strip supply with a meter first.
- Lights, but stops part-way / wrong colours: data or power along the strip (a dead pixel blocks
  everything after it).
- Everything correct here but dark in the wheel firmware: firmware side.
