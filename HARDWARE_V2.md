# Wheel v2 - hardware decisions and pin map

Status: build in progress (target: party build). This file records the v2
hardware that replaces the bench rig described in `S3_PORT.md`. Firmware in
`prize_wheel_gpt/` still carries the v1 bench pin map - see "Firmware deltas"
at the bottom for what must change before the v2 loom is powered.

## Mechanical

| Item | Choice | Notes |
|---|---|---|
| Disc | 36" Baltic birch, 1/2" (3/4" acceptable) | ~6 kg, I ~= 0.6 kg*m^2. 3/4" = ~9 kg, I ~= 0.9 |
| Stiffener | 12" x 3/4" ply hub plate, glued + screwed to the back | carries the hub studs |
| Hub | Go-kart sprocket hub, 3/4" bore, 3/16" keyway, 1" flange | 4 x 3/8" bolts on a 2-7/8" circle |
| Shaft | 3/4" keyed steel, ~12" | same 3/16" key as the hub |
| Bearings | 2 x UCFL204-12 (3/4" bore) | TWO supports required; the coupling is not a bearing |
| Coupling | Lovejoy L075: 3/4" keyed hub + 8 mm hub + NBR spider | 8 mm hub is scarce; drill a 1/4" hub out to 8 mm |
| Motor | NEMA23, 76 mm body, ~1.9 N*m, 3 A, DUAL SHAFT | rear stub carries the encoder magnet |
| Wedges | 12 (firmware unchanged); artwork also generated for 18 and 24 | 12 x 30 deg, 6.3" at the rim |
| Pegs | 12 on the wedge boundaries + flapper | mechanical clicks; this is why audio latency no longer matters |
| Face | printed adhesive vinyl (dry-erase laminate) on sealed/primed ply | trim flush after applying; register to the HUB HOLE, not the edge |

Torque budget: intercept at 0.55 rev/s with a 4 s decel needs ~0.6 N*m (1/2"
disc) or ~0.8 N*m (3/4"). Motor supplies 1.9 N*m => 2.4-3x margin.

Balance statically on the bearings AFTER pegs and vinyl are on. 20 g at the
rim is ~0.1 N*m, a real bite out of the budget.

## Electronics

| Block | Part | Rail |
|---|---|---|
| MCU | ESP32-S3-N16R8 (Lonely Binary) on a screw-terminal breakout | 5 V |
| Driver | BTT TMC5160T Pro stepstick, SPI mode | 24 V motor, 3V3 logic |
| Encoder | AS5600, I2C, on a bracket off the motor's rear endcap | 3V3 |
| LEDs | WS2815 halo ring, 40" dia, ~300 px, behind the disc | 12 V |
| Level shift | 74AHCT125 on a DIP-14 screw adapter | 5 V |
| Audio | S3 I2S -> PCM5102A DAC -> TPA3116D2 amp -> 8 ohm speaker | 5 V / 24 V |
| 24 V PSU | 200 W / 24 V constant-voltage LED driver, 8.33 A | - |
| 12 V PSU | Mean Well LRS-100-12 + 12->5 V buck (3 A) for logic | - |

No DFPlayer in v2. The peg flapper makes the clicks; effect samples live in
PSRAM and play over I2S. An ambient music bed, if wanted, comes from an
external source into the amp's input.

### Pin map (v2)

| Function | S3 GPIO | To |
|---|---|---|
| SPI SCK | 12 | TMC5160T CFG2 |
| SPI MOSI | 11 | TMC5160T CFG1 |
| SPI MISO | 13 | TMC5160T CFG0 |
| SPI CS | 10 | TMC5160T CFG3 |
| STEP | 5 | TMC5160T STP |
| DIR | 6 | TMC5160T DIR |
| EN (active low) | 7 | TMC5160T EN |
| I2C SDA | 38 | AS5600 |
| I2C SCL | 39 | AS5600 |
| I2S BCK | 15 | PCM5102A |
| I2S LRCK | 16 | PCM5102A |
| I2S DIN | 17 | PCM5102A |
| LED data | 40 | 74AHCT125 A1 -> Y1 -> WS2815 DI |
| (leave empty) | 19, 20 | native USB |
| (leave empty) | 43, 44 | UART0 console |

If STEP/DIR/EN terminals 5/6/7 are absent on the breakout, substitute
47/48/21 and change nothing else.

### Wiring rules that bite if skipped

- TMC5160T `CLK` -> GND (use the internal clock).
- TMC5160T `EN` is ACTIVE LOW: drive LOW to run. This is inverted vs the
  A4988-style `EN` on the v1 rig.
- The stepstick is seated in an A4988/DRV8825 DIP-switch breakout with the
  CFG0-CFG3 and CLK pins BENT OUT of the socket and jumpered directly, so the
  breakout's MS1/MS2/MS3 and RST/SLP wiring never touches the SPI lines.
  Set all three DIP switches OFF.
- 2200 uF / 35 V across VM-GND at the driver. The 24 V supply is an LED
  driver; it cannot sink the energy the motor returns while braking.
- PCM5102A: `SCK` -> GND and `XSMT` -> 3V3 are REQUIRED jumpers. FLT, DEMP
  and FMT stay open (internal pull-downs give I2S / no de-emphasis / normal).
- WS2815 `BI` (backup data) -> GND at the first pixel; inject 12 V at both
  ends of the ring; 330 ohm in series on DI.
- AS5600 on 24 AWG silicone, under 30 cm, SDA/SCL twisted with a ground
  return, 2.2k pull-ups to 3V3, 400 kHz. Check MD set / ML+MH clear / AGC
  mid-range at bring-up.
- Speaker goes ONLY to the amp's OUT+/OUT-. The TPA3116 output is bridged;
  neither terminal may touch ground.
- All grounds (24 V V-, 12 V V-, buck OUT, driver power GND, logic GND)
  meet at ONE star point.
- Fuses: 24 V motor 5 A, 24 V amp 3 A, 12 V LEDs 5 A.

See `docs/prize_wheel_v2_wiring.svg` for the drawing.

## Firmware deltas still to do

The sketch in `prize_wheel_gpt/` is still on the v1 bench map:

- `TMC_RX_PIN 16` / `TMC_TX_PIN 17` / `R_SENSE 0.11f` - UART TMC2209 setup.
  v2 is a TMC5160 over SPI: `TMC5160Stepper driver(CS, 0.075f, MOSI, MISO,
  SCK)`, SPI mode 3, `rms_current(2800)`, `en_spreadCycle(false)`,
  `pwm_autoscale(true)`.
- `PIN_EN 4` -> 7. Verify enable polarity against the 5160T (also active-low,
  so `setEnablePin(pin, true)` likely stands).
- `PIN_SDA 8` / `PIN_SCL 9` -> 38 / 39.
- LED data pin -> 40.
- GPIO 15/16/17 are now I2S, not UART. Serial1 and all DFPlayer code come
  out; the effect player becomes I2S from PSRAM.
- Drive is now DIRECT 1:1, not the 2:1 belt: 4096 encoder counts cover ONE
  wheel revolution. Re-derive anything that assumed two.
- Re-measure `rawZero` on the new disc; re-run the friction calibration with
  pegs fitted (the flapper changes the friction shape).
- Retune the StealthChop current staging around the 3 A motor rather than
  the 600/450 mA pair used on the v1 rig.
