# party11_20260930 - flash 11: firmer steered stops (brakeGain 1.4, +/- live)

Base: work/party10_20260929 (flash 10). Source = flash-10 source + `apply_flash11.py` (see its docstring);
`stage11.py` stages it on MILL-PC; `pw_chain11.ps1` compiled and uploaded it.
LF-normalized sha256 prize_wheel_gpt.ino 9aa59753d2ad692980d71c1e9d44eb8b97988633143a9f453545357025d9df38
(local = MILL-PC; pw_party_impl.h unchanged from flash 10).

## Why (Timur 2026-09-30 08:32)
"I still think it oversteers. Can we make braking rate higher."  Flash 10 planned steered stops at ~1.0x
(<= 1.05x) the learned natural decel: 200-450 deg / 4-8 s of controlled roll after capture.

## What changed
brakeGain (default 1.4; '+'/'-' at rest, 0.1 steps, 1.0-1.6, NVS 'brakeGain'; in 'f' and RESERVE g=):
pass-1 window/coupling floor/urgency/plan check at natural/g and g x decel; shadow firm tier first
(<= min(g,1.3)x, peg rule, 6.5 deg from dares, stretch to true natural+12 dare-free); passes 3/4/5 and all
safety tests keep the true natural; exact-stop cap min(1.25g,1.6) x true natural.
Independent review: first draft scaled the fallbacks (blocking) - fixed; max 2.0 -> 1.6; stretch scan made
linear, +4 -> +12; help text.  Local + MILL-PC compile exit 0, no sketch warnings.

## Result
Upload exit 0 08:56:08 (arm approved ~08:47, reason stated the upload). `?` party11 banner; `s` IDLE_STOPPED
fault=NONE tmc=1 takeover=1 rawZero=1925; `f` cw 0.2292/0.0783 fits 28, ccw 0.2706/0.0656 fits 53, brakeGain=1.40.
Rollback: `-` x4 (brakeGain 1.0 ~ flash 10 pass-1 behaviour) or work/party10_20260929/upload_candidate.py.
