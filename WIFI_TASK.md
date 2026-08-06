# WIFI_TASK — SoftAP Telemetry + Command Channel

**Base:** branch from `fix/landing-distribution` (commit `59d022c` or later).
Create branch `claude/wifi-v1`.
**Sketch:** `prize_wheel_gpt/prize_wheel_gpt.ino`. Read FX_TASK.md first — the
same event/print discipline and loop-budget rules apply.

## Purpose
Remove the USB tether. A cable running from a "free-spinning" party wheel to a
laptop is the most visible tell in the build. With WiFi the wheel stands alone:
telemetry and commands go to a phone. Serial over USB stays fully functional
for bench work — WiFi is additive, never a replacement.

## Non-negotiable constraints
1. **Control tick untouchable.** All network work is non-blocking and fits the
   same ≤2 ms per-loop budget as FX. No socket call may ever wait.
2. **Fail-silent.** No WiFi, no client, RF interference, or stack error must
   never fault the wheel or alter a spin. Worst case: telemetry stops.
3. **No OTA in v1.** A failed OTA bricks until USB rescue. Flashing stays USB.
4. **No new I2C traffic; no control-logic changes.**
5. **Flash + bench-test after every commit** per HANDOFF_README workflow.

## Design
- **SoftAP, not venue WiFi.** SSID `PW-####` (last 2 MAC bytes), WPA2 password
  in a `#define`, fixed channel, `max_connection=2`. No dependency on any
  router or internet. Optional: hidden SSID via `#define`.
- **TCP server on port 23** (plain telnet). Up to 2 simultaneous clients.
- **Outbound mirror:** route the existing log-print path through one function;
  it writes to Serial AND appends to a 4 KB RAM ring buffer. Each loop pass,
  for each connected client, send pending ring bytes only up to
  `client.availableForWrite()` — drop-oldest on overflow, never block.
- **Catch-up on connect:** new client receives a banner plus the most recent
  ~2 KB of the ring so a phone joining mid-session sees recent context.
- **Inbound commands:** bytes read from clients feed the SAME single-char
  command parser the serial console uses (refactor to `handleCommandChar(c)`
  shared by both inputs if needed). No new command logic. Existing guards
  (e.g. `z` only while IDLE) automatically apply.
- **Net heartbeat:** every 10 s emit `# net: clients=N rssi=...` through the
  normal log path so link health is visible in the same stream.

## Power / RF cautions
- WiFi TX bursts draw sharp current spikes. Verify the 5 V rail feeding the
  ESP32 is solid under radio load; log a boot line if the brownout detector
  fired. Bench test: stream telemetry during motor capture at 600 mA.
- Radio runs on core 0; verify no added jitter to the 25 ms tick with the
  loop max-time tracker (serial cmd) while a client is streaming.

## Acceptance
1. Seven consecutive controlled spins, both directions, with a phone client
   streaming — results and loop timing identical to no-WiFi baseline.
2. Kill the link mid-spin (disconnect client / walk out of range): spin
   completes normally; ring retains the lines; reconnect replays context.
3. `s f r z d` all work from telnet with identical output to serial.
4. AS5600 13-move accumulation test shows no regression with radio up.
5. Append findings to REDESIGN_REPORT.md appendix; do not rewrite the report.

## Explicitly out of scope for v1
OTA flashing, web dashboard/HTTP UI, mDNS, venue-WiFi station mode, and any
cloud relay. Each can be a later phase once the party build is frozen.
