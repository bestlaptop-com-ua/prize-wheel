# party7_20260928 - flash 7: boot volume 15 (gain 0.50)

Base: work/party6_20260925 (flash 6, on the wheel since 2026-09-25 15:47, restored 2026-09-28 12:46).
Source = flash-6 source + `apply_flash7.py` (exact-once anchors); `stage7.py` stages it on MILL-PC.

## Why (Timur, 2026-09-28)
13:3x "Decrease gain. It is too loud. Make it 50% from current volume" -> `V15` at runtime (gain 0.50),
lost on every reboot.  17:17 "Do 15" -> make it the boot default.

## What changed
1. `pw_party.h`: `PW_DFP_VOLUME 30` -> `15` (boot volume; `V<n>` still adjusts live, 0-30, gain = n/30).
2. Banner `party7-20260928 (flash 7: boot volume 15 = gain 0.50) on party6-20260925 ...`.
Nothing else: control logic, currents, hold, audio samples unchanged.

## Result (on the wheel since 2026-09-28 17:35)
Arm approved ~17:28 (Telegram, reason stated the upload).  MILL-PC compile exit 0, no sketch warnings,
1,701,379 bytes (54%); upload exit 0, hash of data verified.  `?` party7 banner; `s` IDLE_STOPPED fault=NONE
tmc=1 takeover=1 dirCal=1 rawZero=1925; `f` friction kept (cw 0.2483/0.0931 fits 26, ccw 0.3225/0.2020 fits 40);
`P` -> "gain 0.50" after the reboot.
Encoder re-seated before this flash (Timur 16:58/17:12 boundary readings): every wedge line within 1.5 deg,
18|1 = 0.26, 1|2 = 19.86.  Spins 85-104 (latest block before the flash): all reported safe wedges, none in a dare.
Rollback: work/party6_20260925/upload_candidate.py (flash 6, volume 30).
