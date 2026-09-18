# Wheel v2 - hardware decisions and pin map

Status: direct-drive v2 bring-up completed in part; reliability is not yet
validated. The committed main sketch has the S3/TMC5160 SPI pin map below.
See [bring-up observations](HARDWARE_EVAL_2026-09-17.md) and the
[2026-09-18 review](HARDWARE_REVIEW_2026-09-18.md) for outstanding work.
The NEMA34/DM860T and belt options are not installed configurations here.

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

Torque estimate: at 0.55 rev/s, a uniform 4 s stop with inertia
0.6-0.9 kg*m^2 needs 0.52-0.78 N*m net braking torque. Gravity and friction
alter motor demand; capture transients may be larger. The 1.9 N*m motor
rating is holding torque, so it does not establish a 2.4-3x running margin.
Neither does this estimate establish that the motor is inadequate.

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
- TMC5160T `EN` is ACTIVE LOW: drive LOW to run. Verify the actual carrier wiring;
  A4988 enable is also active low.
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

## Firmware status and remaining work

The committed sketch already uses `TMC5160Stepper`, R_SENSE=0.075 ohm,
SPI GPIO 10/11/13/12, enable GPIO 7, encoder GPIO 38/39, LED GPIO 40,
and direct-drive `GEAR_RATIO=1.0`. It selects SpreadCycle with
`en_pwm_mode(false)`; TMC2209-only calls are not valid replacements.

Remaining work includes preserving learned friction values across restart,
reconciling production-only changes, capture diagnostics and tuning, and
integrating I2S audio (DFPlayer effects are disabled). Recalibrate `rawZero`
and friction after mechanical changes, with pegs fitted. Do not increase
current solely from the nominal 3 A motor rating without checking its
rating convention. See the review for belt encoder constraints and the
separate DM860T driver implementation required if that hardware is adopted.
