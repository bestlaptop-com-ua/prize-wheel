# Flash 13 (2026-09-30): WiFi — lab network + hotspot, phone web page, telnet, firmware over WiFi

Owner, 9/30 10:00: "Enable wifi part of the project. Connectivity, log reading, changing settings."

Chosen options:
- Lab WiFi with a hotspot fallback.
- A web page plus telnet.
- Firmware upload over WiFi.

## Using it
- **First time.** Join the hotspot `PW-xxxx` with the default wheel password `spinthewheel`. Open http://192.168.4.1.
  1. Enter the wheel password in Controls and tap Remember.
  2. Under Network, save the lab WiFi name and password.
  3. Change the wheel password.
- **Lab WiFi.** Open http://prizewheel.local, or the IP that `w` prints over USB. If the lab WiFi can't be found for 20 s at boot, or is lost for 30 s, the wheel starts its hotspot. It retries the lab WiFi every 2 minutes, but only while no phone is on the hotspot.
- **Open to anyone on the network:** status, the landing-count chart, and the live log (`/status`, `/log`, and telnet output on port 23).
- **Needs the wheel password:** commands, WiFi settings, password change and firmware upload. For telnet, type the password and press Enter to enable commands.
  - Remote commands run only when the wheel is at rest, one character per loop pass, like the USB console. Mid-spin they wait in a queue. USB remains the mid-spin path.
  - WiFi settings, password and firmware changes are refused unless the wheel is at rest.
- **Firmware upload.** Use the `.bin` produced by the build (`build/prize_wheel_gpt.ino.bin`).
  - Steering and sound pause during the upload, and are restored if it fails.
  - Only images that contain the prize-wheel network marker are accepted.
  - A new image must keep the control loop alive for 30 s before it is confirmed. If it crashes first, or the loop stalls for 20 s, the bootloader goes back to the previous firmware.

## Design
- **One network task on core 0 (`pw_net.h`).** It does all WiFi, HTTP, telnet and mDNS work. The control loop on core 1 never calls a network API. Per loop pass it only does this:
  - drains a log-line queue (at most 4 lines);
  - runs at most 1 remote command character;
  - applies upload maintenance requests;
  - publishes a 5 Hz status snapshot (seqlock).
- **Log mirror.** Serial, telnet and the web page share one parser: `dispatchCommandChar`, split out of `handleSerial`. The log ring is written only by core 1; the net task reads it and trims any bytes overwritten while copying.
- **Diagnostics buffer.** `DIAG_CAPACITY` drops from 2304 to 1664 while WiFi is compiled in. This is the flash-2 switch.
- **Settings storage.** Stored in NVS namespace `pwnet`: `ssid`, `pass` and `wpw` (the wheel password, which is also the hotspot WPA2 key).
- **TX power.** 17 dBm, to soften RF current peaks.

## Review
An independent agent reviewed the change and gave GO with changes. All blocker and should-fix items were applied:
- remote commands gated (at rest, 1 per pass, UART room);
- queue NULL guard;
- restart on abort-after-end;
- firmware marker check;
- non-blocking telnet send;
- 12 KB stack;
- rollback spam fix;
- maintenance handshake and audio mute;
- ring in-flight byte and `X-Boot` header;
- telnet IAC parser;
- SSID escaping;
- reconnect timestamp.

The re-review gave GO.

## Bench checks after the flash
1. Send `?` from the web page.
2. Run `w` and read `stackFree`.
3. Do one spin while `/status`, `/log` and telnet are all streaming, then check `t` and look for BROWNOUT.
4. Do one firmware upload of the same build and confirm it is confirmed after 30 s.
