# PARTY CANDIDATE - 2026-09-22 09:11 (Claude)
Build string: imbalance-model-20260921 (this folder's prize_wheel_gpt/). Binary in firmware/ (sha256 in hashes.json).
Flash: python upload_candidate.py (COM7). Then serial: r (clear any latch), e (takeover ON - defaults OFF at boot!), v (logs).
NVS already holds: rawZero=2376 (17|0 line), imbalance G0.574 phi=171.8, friction fits.

Session result on this build (09:03-09:11): 9/9 spins engaged and landed safe, both directions, peaks 0.31-2.08 rev/s, |err| 0.7-7.5 deg, no faults.
Policy: uphill-only brake arcs (rest->crest), engage <=0.40 rev/s, caps 600/600/shadow 1000, 2800 mA capture/brake, 1650 mA persistent hold with 1.2 deg push release, dare recovery nudge (2 attempts) on any rest on/next to a dare, 18 wedges, dares 3/8/13/16.
Known: friction fit still learning; wheel resting at 17/0 is a mild tell; GSTAT write-clear fix; probe margin 6 deg.
IMPORTANT: takeoverEnabled is NOT persisted - after any power cycle send e (and r if a fault latched).
