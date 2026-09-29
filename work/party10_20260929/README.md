# party10_20260929 - flash 10: landings 9 deg past the last peg, tick at flapper release

Base: work/party9_20260928 (flash 9). Source = flash-9 source + `apply_flash10.py` (see its docstring);
`stage10.py` stages it on MILL-PC; `pw_chain10.ps1` (Temp) waited for the compile and uploaded it.
LF-normalized sha256: prize_wheel_gpt.ino 308f74ea1538955fdd6e97178b7deae85ab94b8763a9f7bc3c7f48e68ecf7dd3,
pw_party_impl.h 3eb955a6e0e6d885c66e7c6e2143132de1ca22d6f5cef91ef715470004c9368f (local = MILL-PC).

## Why (Timur 2026-09-29)
08:33 "Sometimes it tries to steer where it would be better to brake at the end of the spin. So it slows
down to almost a stop and then rotates a wheel a bit."  08:39 "make click not straight between wedges but
1 degree after. That's when clicker releases."
A constant-decel stop X deg past a line crosses that peg at sqrt(2aX) (3 deg -> ~0.03 rev/s); a free
flapper wheel stops at a peg below ~0.04 rev/s (9/28 free-coast tails), so the motor pushing a nearly
stopped wheel over the last peg is the tell.  Flash-9 spin 7 parked against a peg; spin 4 landed 6 deg past one.

## What changed
1. Pass 1/3 travel-entry margin >= 9 deg; pass-3 cushion mid-span on narrow spans.
2. Shadow tiers: peg rule (9 behind / 4.5 ahead) within 1.3x natural decel -> peg rule within 1.6x ->
   flash-9 rule; landing always >= 6.5 deg from dares.
3. Pass 5 carry clearance 9; 'before' landing also 9 deg past its entry peg.
4. Audio tick when the point 1 deg behind the pointer (direction latched, flip re-bases silently) crosses a line.
Independent review: first draft starved the shadow pass (-> pass-3 hard brakes); tiered version simulated
equal to flash 9 at every natural stop; its two notes applied.  Local compile exit 0, no sketch warnings.
## Result
MILL-PC compile exit 0 (09:03:59), no sketch warnings; upload exit 0 09:05:03 (arm approved ~08:41, reason stated
the upload). `?` party10 banner; `s` IDLE_STOPPED fault=NONE tmc=1 takeover=1 rawZero=1925; `f` friction kept
(cw 0.1768/0.0907 fits 27, ccw 0.3525/0.0722 fits 44).
Rollback: work/party9_20260928/upload_candidate.py (flash 9).
