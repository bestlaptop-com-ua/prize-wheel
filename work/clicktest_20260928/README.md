# clicktest_20260928 - speaker test: the wheel's click sound on repeat

Why: Timur 2026-09-28 12:19 "Play clicks" while testing the new HW-576 (TPA3116D2 mono) amp.
Flash 6 cannot play the tick on command (ticks only fire on wedge crossings during a spin),
so this small sketch plays flash 6's own tick sample (track 1 of pw_samples.h, 2880 samples)
in a slowing-spin pattern: 25 clicks, gap 120 ms growing x1.08 to ~760 ms, then 2.5 s quiet,
repeat. Same audio path as flash 6: I2S0 left-justified, 32-bit slots, 22.05 kHz,
BCK 15 / LRCK 16 / DIN 17 -> PCM5102A, gain 1.0 (= flash-6 volume 30).
The motor driver is held OFF (EN GPIO7 high, STEP/DIR low): no steering while it runs.

- `clicktest/tick.h` = first 96 lines of work/party6_20260925/prize_wheel_gpt/pw_samples.h + TICK_N.
- Local build and MILL-PC build: exit 0, no sketch warnings, 339 KB.
- Uploaded 2026-09-28 12:22 (exit 0, hash-verified); serial shows `click run N`.
- Restore: work/party6_20260925/upload_candidate.py (flash 6).