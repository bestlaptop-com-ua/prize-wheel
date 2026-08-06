# Codex party-v1 delivery

Branch: `codex/party-v1`

Control baseline: `f5419be` on `fix/landing-distribution`. The task file is
the following commit (`dcb804c`). No device was flashed, no serial port was
opened, and no mechanical assumption was changed.

`PARTY_TASK.md` references `MISSION.md` and `CLAUDE.md`, but neither file exists
on the supplied branch. Mission context was taken from `BRIEF.md`,
`HANDOFF_README.md`, `REDESIGN_REPORT.md`, `FX_TASK.md`, and `WIFI_TASK.md`.

## What was built

- Sanctioned S1: fault code persisted in the existing read-write
  `prizewheel` NVS namespace, restored into `FAULT_LATCHED` at boot, and
  cleared only by a successful attended `r` path.
- Sanctioned S2: `CHOPCONF`/`GCONF` readback at boot and every five seconds
  while truly idle. A mismatch uses existing `FC_TMC_UART`; `r` reapplies the
  accepted configuration and proves it before clearing.
- Sanctioned S3: every spin summary includes cumulative `fitRej` and current
  `fitCW`/`fitCCW` counts.
- SoftAP `PW-####`, WPA2, two-client telnet server on port 23, 4 KB
  drop-oldest log mirror, about 2 KB reconnect catch-up, bounded non-blocking
  socket reads/writes, ten-second network heartbeat, and the same command
  parser as USB serial. No OTA or Internet dependency exists.
- Fixed-size FX event ring, raw fire-and-forget DFPlayer protocol, 120 ms
  global command limiter, ratchet/tick hysteresis, delayed win fanfare, guest
  jingle, and fault stop.
- 36-pixel stationary-rim WS2812B animation: idle breathe, real-angle comet,
  target collapse, landed segment, and dim steady fault state. LED zero/direction
  calibration persists through `[`, `]`, and `\`.
- Combined WiFi/FX `micros()` maximum and over-budget counter, printed by `t`
  and reset by `T`.
- Six generated, original MP3s plus a deterministic Python generator. The
  checked-in set is 44.1 kHz mono, 96 kbps, and 164,256 bytes total.
- Full risk audit and updated owner wiring/operation notes.

The FX task's GPIO16/17 DFPlayer request is electrically incompatible with
the accepted TMC2209 UART already on those pins. Party v1 preserves the motor
link and uses isolated UART1 GPIO32/33. Follow `README.md`, not the stale pin
sentence in `FX_TASK.md`.

## Compile proof

Command run from the repository root:

```powershell
& 'C:\Program Files\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' compile --fqbn esp32:esp32:esp32 --warnings all prize_wheel_gpt
```

Result on 2026-08-06:

```text
Sketch uses 1080978 bytes (82%) of program storage space. Maximum is 1310720 bytes.
Global variables use 57416 bytes (17%) of dynamic memory, leaving 270264 bytes for local variables. Maximum is 327680 bytes.
```

The compile exited 0. There were no warnings from the sketch or party add-on
code. Nine warnings came from FastAccelStepper 1.2.7's ESP-IDF platform files:
missing initializers in MCPWM/I2S configuration structs and one deprecated
`gpio_iomux_in` call. They are the same library class documented for the
baseline and were not patched locally.

Toolchain and libraries actually selected by `arduino-cli`:

| Component | Version |
|---|---:|
| Arduino CLI | 1.5.1 |
| ESP32 Arduino core / board platform | 3.3.10 |
| Wire | 3.3.10 |
| Preferences | 3.3.10 |
| WiFi | 3.3.10 |
| Network | 3.3.10 |
| TMCStepper | 0.7.3 |
| FastAccelStepper | 1.2.7 |
| Adafruit NeoPixel | 1.15.5 |

The optional 2.9-second diagnostic buffer is 82,432 bytes and is allocated
only when `d` is armed, then freed after the dump. If internal heap cannot
supply it, the command reports the allocation failure and control continues.

## Compile-time settings

### Sanctioned fixes

| Define | Default | Effect |
|---|---:|---|
| `PARTY_FIX_PERSIST_FAULT` | 1 | S1 NVS fault persistence and `r`-only clear |
| `PARTY_FIX_TMC_VERIFY` | 1 | S2 boot/idle register readback and reset recovery |
| `PARTY_FIX_FIT_SUMMARY` | 1 | S3 rejection/count fields in spin summaries |

These exist for controlled regression comparison. Party delivery requires all
three to remain 1.

### WiFi and telnet

| Define | Default | Owner action / effect |
|---|---:|---|
| `PARTY_WIFI_ENABLED` | 1 | Set 0 only for an isolation build |
| `PARTY_WIFI_PASSWORD` | `change-me-2479` | **Change before guest use**; minimum eight characters |
| `PARTY_WIFI_CHANNEL` | 6 | Fixed SoftAP channel |
| `PARTY_WIFI_HIDDEN` | 0 | Set 1 to hide SSID; not a security substitute |
| `PARTY_TELNET_PORT` | 23 | Plain telnet port |
| `PARTY_TELNET_MAX_CLIENTS` | 2 | Also supplied to SoftAP max-connections |

SSID is generated as `PW-####` from the final two SoftAP MAC bytes.

### Audio and LEDs

| Define | Default | Owner action / effect |
|---|---:|---|
| `PARTY_FX_ENABLED` | 1 | Master FX service switch |
| `PARTY_AUDIO_ENABLED` | 1 | DFPlayer output switch |
| `PARTY_DFPLAYER_TX_PIN` | 32 | ESP32 UART1 TX through 1 kOhm to DFPlayer RX |
| `PARTY_DFPLAYER_RX_PIN` | 33 | Optional DFPlayer TX; firmware does not wait/read |
| `PARTY_DFPLAYER_VOLUME` | 20 | Boot volume, range 0-30 |
| `PARTY_DFPLAYER_MIN_COMMAND_MS` | 120 | Global command spacing; do not reduce for party |
| `PARTY_LED_ENABLED` | 1 | WS2812B output switch |
| `PARTY_LED_DATA_PIN` | 13 | Stationary-rim data pin |
| `PARTY_NUM_LEDS` | 36 | Installed pixel count; re-run timing acceptance if changed |
| `PARTY_LED_BRIGHTNESS` | 72 | Global 0-255 brightness/power limiter |
| `PARTY_LED_ZERO_INDEX` | 0 | Factory default; runtime calibrated value is in NVS |
| `PARTY_LED_DIRECTION_SIGN` | 1 | Factory index direction; runtime value is in NVS |
| `PARTY_LOOP_BUDGET_US` | 2000 | `t` timing threshold and overrun counter |

### Hardware geometry defines (do not tune on party day)

| Defines | Current values | Meaning |
|---|---|---|
| `TMC_SERIAL`, `TMC_RX_PIN`, `TMC_TX_PIN`, `TMC_ADDR`, `R_SENSE` | Serial2, 16, 17, 0, 0.11 | Accepted single-wire TMC link |
| `PIN_EN`, `PIN_STEP`, `PIN_DIR` | 25, 26, 27 | Motor control pins |
| `PIN_SDA`, `PIN_SCL`, `AS5600_ADDR`, `AS5600_RAW` | 21, 22, 0x36, 0x0C | Accepted encoder bus/register |
| `MOTOR_FULLSTEPS`, `MICROSTEPS`, `GEAR_RATIO` | 200, 16, 2.0 | 6,400 microsteps per wheel revolution |
| `NUM_WEDGES` | 12 | Physical wheel geometry |
| `RMS_CURRENT_MA` | 1450 | Proven driver setup ceiling, not a party tuning knob |

`Serial` is internally redirected through `PartyLog` only to mirror output; it
is not an owner setting. All accepted timing/current/reachability constants
remain ordinary frozen constants and are intentionally absent from the party
toggle table.

## Commands added or shared

USB serial and telnet both accept the existing `z p s d v e f F x r m ?`
commands with their existing guards.

| Command | Action |
|---|---|
| `t` | Print add-on max microseconds, 2,000 us budget, overrun count, link/FX configuration |
| `T` | Reset add-on timing maximum and overrun count |
| `VOL n` | Set DFPlayer volume 0-30; terminate with Enter |
| `[` / `]` | Nudge and persist LED wedge-0 index |
| `\` | Reverse and persist LED index direction |

## Known limitations

- Hardware acceptance is not claimed. No flash, COM3 access, physical wheel,
  phone, LED strip, or DFPlayer test occurred in this delivery.
- Ultra-weak spins remain physically/visibly risky as required by the frozen
  control policy. The host must enforce a confident-spin rule.
- The first-fast-spin capture surge has no provable source-code cause; perform
  warm-up spins and reject the build if `rise` repeats outside accepted behavior.
- S2 cannot read TMC registers during powered motion without violating encoder
  timing. A dropout can affect one spin before the next idle readback.
- Adafruit NeoPixel's RMT `show()` is synchronous for the frame duration.
  Thirty-six pixels are designed to fit the 2 ms budget, but only the hardware
  `t` result is acceptance evidence.
- DFPlayer has no reliable ACK/error path in this design. Player/card absence
  and clone-specific loop gaps degrade audio only.
- Telnet is plaintext inside the WPA2 SoftAP and exposes powerful existing
  commands. Change and protect the WPA2 password.
- A telnet client that falls behind loses old log bytes by design. Use USB for
  a complete high-rate diagnostic dump.
- Arming `d` temporarily consumes about 82 KB internal heap. Allocation failure
  is explicit and does not affect control.
- The generated loops are periodic at the source boundary, but some DFPlayer
  clones insert a decoder pause that firmware cannot remove.

See `RISK_AUDIT.md` for the full source-path analysis and procedural controls.

## Fifteen-minute owner bench script

Stop immediately on any dare landing, reversal, rattle, brownout, NVS failure,
restored/unexpected fault, or add-on timing overrun. Do not clear a fault just
to finish the checklist.

1. **0:00-1:30 - power-off wiring check.** Confirm TMC remains on GPIO16/17.
   Confirm DFPlayer RX is on GPIO32 through 1 kOhm (optional TX to GPIO33), not
   16/17. Confirm strip DIN GPIO13, stationary mounting, common ground, level
   shifting, and adequate 5 V bulk capacitance. Expected: no shared UART wire.
2. **1:30-2:30 - flash and boot.** Flash the compile-verified sketch by USB,
   then open 115200 serial. Expected: `AS5600 primed`, `step pulse
   backend=MCPWM_PCNT`, `tmc=1`, `party addons: wifi=1 audio=1 leds=1`, and
   `brownout=0`. There must be no `NVS ... FAILED`, `RESTORED PERSISTED FAULT`,
   or `TMC CONFIG MISMATCH` unless intentionally investigating an old fault.
3. **2:30-3:30 - frame and direction.** Put the pointer on the 11|0 boundary
   and send `z`. Center it in safe wedge 3 or 7-11 and send `p`. Expected:
   angle approximately 0 after `z`; two opposite probe legs; `DIR PROBE PASS`;
   `s` shows `dirCal=1`, `tmc=1`, `takeover=1`, `fault=NONE`.
4. **3:30-4:30 - LED and audio setup.** Use `[`/`]` and, if needed, `\` until
   the real pointer maps to the rim correctly. Send `VOL 20`. Slowly cross two
   wedges. Expected: persisted index lines, dry ticks, correct comet direction,
   no motor motion caused by FX.
5. **4:30-5:30 - phone link.** Join `PW-####`, telnet to `192.168.4.1:23`, and
   send `s`, `f`, `t`, then `T`. Expected: recent-context banner; output matches
   USB; add-on timing resets to zero; status remains takeover-ready.
6. **5:30-11:30 - seven-spin regression.** With the phone continuously
   streaming, make seven confident spins covering both directions, releases
   from 0.43-1.82 rev/s where practical, and starts near both dare regions.
   Disconnect/reconnect the phone during one middle spin. Expected for every
   controlled attempt: no reversal, no visible speed-up, no capture-time audio
   gap, ratchet-to-tick crossover without chatter, safe landed wedge, winner
   rim/fanfare, and one honest summary. Acceptance target from the baseline is
   `CONTROLLED_SAFE` with absolute landing error under 2 degrees. Link loss
   must not change the spin; reconnect must show recent context.
7. **11:30-12:30 - idle TMC proof.** Leave the wheel still for at least six
   seconds, then send `s`. Expected: no rattle/motion, no config-mismatch line,
   `tmc=1`, `fault=NONE`.
8. **12:30-13:30 - timing gate.** Send `t` from the phone while USB is also
   connected. Expected: `max_us<=2000` and `overruns=0`. Any overrun is a
   no-go; first disable LEDs and repeat rather than touching the control tick.
9. **13:30-14:30 - friction/accounting review.** Send `f` and inspect all seven
   summaries. Expected: plausible `fricC/fricB`, growing fit counts from real
   coasts, explicit `fitRej`, `rise` inside accepted behavior, no hidden dare
   line, and no `CONTROL_LOCKED`, `NO_REACHABLE_SAFE`, or fault result.
10. **14:30-15:00 - final gate.** Send `s` and `t`; disconnect the USB cable
    only if the independent 5 V supply is already stable. Expected final state:
    `IDLE_STOPPED`, `fault=NONE`, `dirCal=1`, `tmc=1`, `takeover=1`, WiFi up,
    zero timing overruns. If a fault occurred during any step, power cycling
    must restore that code; inspect the cause, and use `r` only after repair.
