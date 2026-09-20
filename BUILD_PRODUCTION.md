# Build the plywood-wheel production integration candidate

**Not a validated production release.** This worktree is based on `d9b514a`
with local compatibility, learning and pulse-first capture changes. Its new
production pickup/braking integration has not been validated on the plywood
wheel; the diagnostic positive-direction trial still failed pickup. These instructions compile the
project only; they do not upload, reset a controller, clear NVS or run a motor.

Use the `prize_wheel_gpt` sketch directory. Older documents describe the former
classic ESP32 board and are not the build settings for this ESP32-S3 candidate.

## Pinned dependencies and board

| Component | Required version/settings |
| --- | --- |
| ESP32 Arduino core | `esp32:esp32@3.3.10` |
| FastAccelStepper | `1.2.7` |
| TMCStepper | `0.7.3` |
| FastLED | `3.10.5` (retained party LED dependency) |
| Board | ESP32-S3 Dev Module, 16 MB flash, OPI PSRAM |
| Partition | `app3M_fat9M_16MB` |
| USB CDC on boot | `default` (the verified existing FQBN value) |

Exact FQBN, copied from the existing MILL-PC build configuration:

```text
esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600
```

The STEP-clock helper depends on this core/FastAccelStepper combination and
the sole stepper using MCPWM group 0 / timer 0. Library, board, core or stepper
allocation changes require reviewing that helper again. Wire, Preferences,
SPI, WiFi and Networking are supplied by the pinned core.

## Install and verify dependencies

Run PowerShell from this worktree root. Arduino CLI must be installed and
available on `PATH`; alternatively set `$cli` to its absolute executable path.
These install commands change the selected Arduino environment, so use the
dedicated build environment intended for this project.

```powershell
$cli = 'arduino-cli'
$esp32Index = 'https://espressif.github.io/arduino-esp32/package_esp32_index.json'
& $cli core update-index --additional-urls $esp32Index
& $cli core install 'esp32:esp32@3.3.10' --additional-urls $esp32Index
& $cli lib update-index
& $cli lib install 'FastAccelStepper@1.2.7' 'TMCStepper@0.7.3' 'FastLED@3.10.5'
& $cli version
& $cli core list
& $cli lib list
```

Each install must succeed. Check the printed versions against the table;
duplicate sketchbook libraries can override the intended package. Retain the
CLI version, resolved library paths/versions and build log with any tested
binary. This recipe pins project dependencies; it does not claim identical
binary hashes across arbitrary CLI installations or source revisions.
Command syntax follows the Arduino CLI references for
[core installation](https://arduino.github.io/arduino-cli/1.3/commands/arduino-cli_core_install/)
and [versioned library installation](https://arduino.github.io/arduino-cli/1.3/commands/arduino-cli_lib_install/).

## Compile without uploading

```powershell
$fqbn = 'esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
$buildRoot = Join-Path $PWD 'build\production-s3'
$outputRoot = Join-Path $PWD 'build\production-firmware'
New-Item -ItemType Directory -Force $buildRoot, $outputRoot | Out-Null
& $cli compile --fqbn $fqbn --warnings all --verbose --build-path $buildRoot --output-dir $outputRoot '.\prize_wheel_gpt' *>&1 | Tee-Object -FilePath '.\build\production-compile.log'
if ($LASTEXITCODE -ne 0) { throw 'Production candidate compilation failed' }
Get-FileHash '.\build\production-firmware\prize_wheel_gpt.ino.bin' -Algorithm SHA256
```

The source defaults are takeover off, WiFi off, audio off, LED effects on. Do not add the
diagnostic `PW_SELFSPIN_MOTION_ENABLE` flag: this production sketch has no
selfspin command handler. Build artifacts stay under ignored `build/`.

Run the local policy tests separately on a Windows host with Visual Studio
2022 Community C++ tools installed:

```powershell
.\tests\run_host_tests.cmd
if ($LASTEXITCODE -ne 0) { throw 'Production host tests failed' }
```

Host tests cover the actual persistence, recovery-journal and fit-lifecycle
helpers. They do not validate ESP32 compilation, GPIO/SPI/NVS behavior, loaded
pickup, the party selector under the new dynamics, landing or retained hold.
The frozen source passed this target build on September 20; see
[validation evidence](VALIDATION_2026-09-20.md). It still needs hardware
investigation and final production wheel testing before it is ready to use.
