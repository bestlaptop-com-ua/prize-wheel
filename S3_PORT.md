# S3_PORT.md - Porting the prize wheel to ESP32-S3-N16R8

Audience: a fresh Claude session (or human) with this repo and no memory of the
2026-08-06 bench marathon. Read MISSION.md / CLAUDE.md for the project's purpose
and REDESIGN_REPORT.md for the control design before touching code.

## Source baseline
- Branch `claude/party-scatter` = newest working firmware: certified control
  core + per-edge landing-scatter margins + WiFi off + FX (DFPlayer, WS2812B).
- Tag `party-certified-v1` = the certified fallback without scatter.
- Do NOT base on `codex/party-v1` (its freewheel init change breaks motor
  coupling - see RISK_AUDIT history) except to cherry-pick its R5/R8 findings
  deliberately, with bench validation.

## Target board facts (ESP32-S3-N16R8 DevKitC-1 class)
- 16 MB flash, 8 MB octal PSRAM. RAM pressure that shaped the classic-ESP32
  build (diag-vs-WiFi-vs-LED tradeoffs) is gone; DIAG_CAPACITY can grow.
- TWO USB-C ports: one via CH343 UART bridge, one NATIVE USB (S3 silicon).
  Prefer the native port for flashing/serial - no external bridge chip to
  wedge. Arduino: enable "USB CDC On Boot".
- Onboard WS2812 RGB LED, usually GPIO48 - free boot/status indicator.
- RESERVED pins - never use: 26-32 (flash), 35/36/37 (octal PSRAM!),
  19/20 (native USB). Strapping - avoid or use with care: 0, 3, 45, 46.
- Safe general-purpose set used below: 1,2,4-18,21,38-42,47,48.

## Arduino IDE / CLI settings
- Board: "ESP32S3 Dev Module", esp32 core 3.x (match repo: 3.3.10+).
- Flash Size 16MB; PSRAM: "OPI PSRAM"; Partition: any 16MB app-heavy scheme
  (huge_app no longer needed but harmless equivalents exist); USB CDC On
  Boot: Enabled; upload via native USB port.

## Suggested pin map (edit the #defines at the top of prize_wheel_gpt.ino
## and in pw_party.h; nothing else hardcodes pins)
| Function            | classic pin | S3 pin (suggested) |
|---------------------|-------------|--------------------|
| AS5600 SDA          | 21          | 8                  |
| AS5600 SCL          | 22          | 9                  |
| TMC UART RX2 (ext 1k) | 16        | 16                 |
| TMC UART TX2 (int 1k on module) | 17 | 17  (Serial2 with explicit pins) |
| Motor EN            | 25          | 4                  |
| Motor STEP          | 26          | 5                  |
| Motor DIR           | 27          | 6                  |
| DFPlayer TX (1k)    | 32          | 15                 |
| DFPlayer RX         | 33          | 18                 |
| DFPlayer BUSY       | 34          | 7                  |
| WS2812B data        | 4           | 21 (through the 74AHCT125 shifter) |
Wiring doctrine unchanged: star/bus ground with per-load returns; thin
twisted signal grounds alongside UART/I2C/LED data; 1k resistors on TMC TX
and DFPlayer TX lines; 330-470R after the shifter; level shifter is
MANDATORY (S3 is still 3.3V logic - no ESP32 variant outputs 5V).

## Code changes required
1. Pin defines per table above.
2. REMOVE `#define FASTLED_ESP32_I2S 1` from pw_party.h - that driver is
   classic-ESP32 only and will not compile/work on S3. On S3, let FastLED
   use its default S3 backend (RMT5/LCD). VERIFY on hardware early: drive
   the strip with a minimal rainbow sketch before integrating.
3. VERIFY FastAccelStepper's pulse engine on S3 and its peripheral
   disjointness from FastLED's backend (classic build proved MCPWM vs RMT;
   S3 allocation differs). One bench spin with LEDs animating settles it.
4. Serial console: with USB CDC enabled, `Serial` is the native port -
   115200 still fine. TMC_SERIAL/DFPlayer Serial1/Serial2 begin() calls
   already pass explicit pins; just update the defines.
5. Optional S3 upgrades (post-port wishlist): software MP3 via helix +
   I2S DAC (PCM5102) replaces DFPlayer with zero start latency; WiFi per
   WIFI_TASK.md now fits comfortably; DIAG_CAPACITY back to 2944+.

## Fresh-chip calibration liturgy (NVS is empty; order is strict)
1. Flash; open serial; expect AS5600 primed and a TMC_UART boot latch if
   wires aren't on yet - `r` clears after S2 verifies.
2. Park the CALIBRATION SCREW under the pointer (the screw is the ONLY
   valid zero reference - painted lines have a 180-degree twin) -> `z`.
3. Rest pointer mid-wedge in 3 or 7-11 (probe refuses dare-adjacent
   wedges), 24V on, hands clear -> `p` -> expect PASS, takeover ENABLED.
4. Friction learns from seeds (0.30) toward reality (~0.55-0.68 on wheel
   v1): first 3-4 spins land loose - expected, not a fault.
5. Cross-check frame vs paint: wedge labeled "1" centered under pointer
   must read wedge=1 (~45 deg). `m` prints the dare mask (wedges 1, 5).

## Hard-won process rules (each one paid for in bench hours)
- Control core is frozen: FX/telemetry never edits control paths. The one
  "obviously safe" control fix attempted by an agent put a spin on a dare.
- One change, one flash, one validation batch. Tag before experiments
  (`git tag` = instant rollback).
- When replacing a shared constant, DELETE the old name so the compiler
  finds every use.
- Adjustable trim-pot bucks are banned: one drifted to 15V and killed a
  300-LED strip and likely a devkit. Fixed-output 5V modules only, and
  meter-verify any supply before it touches logic.
- After ANY mechanical work near the encoder: screw-park zero check.
  Shaft grub screw creeps ~2 deg/session under braking - threadlocker it,
  and re-check zero every ~20 spins until proven stable.
- Serial-port discipline: close IDE Serial Monitor before flashing; never
  kill esptool mid-handshake (wedges USB bridges); on S3 the native port
  makes most of this moot.
- Party boot ritual: power on -> pos=FRESH check -> warm-up spin -> `r`
  clears any S1-restored latch after S2 verifies -> one spin per guest,
  hands off until stopped.
