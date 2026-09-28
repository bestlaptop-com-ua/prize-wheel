# party9_20260928 - flash 9: nearest safe exit, crawl dare recovery, aurora + glints standby

Base: work/party8_20260928 (flash 8). Source = flash-8 source + `apply_flash9.py` (exact-once anchors, see its
docstring for the owner's words and the flash-8 data); `stage9.py` stages it on MILL-PC.
LF-normalized sha256: prize_wheel_gpt.ino 58c7c6526d6a7a9582df020e45a82865477beb1b861d1ead9274b68f35bf86b6,
pw_party_impl.h 9f9702e182c79326a5df40b25cf6ffcc06699fd6e255f2da474b1589639bac62 (local = MILL-PC).

## Why (Timur 2026-09-28 18:38, with video)
Weak spins not convincing: a spin that could have stopped before a dare was forced through it; dare recovery
looked worse ("it should keep making minimal speed until it is on a safe wedge"); standby wave not organic,
add bright random sparkles.  Flash-8 spins 1-12: err within +/-2.2 deg; spin 11 (shadow target 3.7 deg past
dare 3, stopped 3.8 short in the dare) and spin 2 (weak q1 brake slipped 44 deg into dare 16) needed P7.

## What changed
1. Shadow (q3): the landing itself >= 6.5 deg from dares and >= 3 deg from lines (natural-6..-12.5, then
   natural-5.5..-3); none -> no shadow.
2. Pass 5 "nearest safe exit": carry just past the dare (clearance 10 -> 6 deg) or stop short of it
   (<= 1.3x natural decel, 8 deg certified margin, quality 1), whichever is nearer the natural stop.
3. Dare recovery: attempt 1 rolls back over the entry peg when it is <= 5 deg behind; target 6.5 deg past the
   exit peg (was the neighbour's centre); 130 Hz / 150 sps^2 crawl at 3.0 A (was 220 / 400 / 2.2 A);
   verdict wait in the dare zone 150 ms (was 500).
4. Standby LEDs: inoise8 aurora (teal-blue-violet, breathing) + white glints (~5/s, bursts, ~0.4 s fade).
Independent review: one blocking issue (unbounded 'before' brake) fixed with the 1.3x / 8 deg bound; shadow
and recovery margins raised to 6.5 as recommended.

## Result
Local compile (arduino-cli 3.3.10) and MILL-PC compile exit 0, no sketch warnings, 1,703,963 bytes (54%).
Arm approved ~19:06 (Telegram, reason stated the upload); upload exit 0 19:12, hash verified. `?` party9 banner;
`s` IDLE_STOPPED fault=NONE tmc=1 takeover=1 rawZero=1925; `f` friction kept (cw 0.1768/0.0907 fits 27,
ccw 0.3407/0.1472 fits 41).
Rollback: work/party8_20260928/upload_candidate.py (flash 8).
