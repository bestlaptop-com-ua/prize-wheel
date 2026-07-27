# MISSION — autonomous, no approvals needed

Objective: smooth, natural takeover and spin-down to a uniformly random SAFE
wedge in BOTH directions, verified end-to-end by encoder + camera, on the
hardware as it is. Find problems, prove them with data, apply the simplest
fix, re-verify. The owner judges final guest-readiness; your job is to make
the technical case airtight.

Ordered tasks:
T1. Spin generator: add serial command `g` (and `G` for the other direction)
    to the firmware: accelerate the wheel from rest at <= half the persisted
    ceiling to a target 0.35-0.45 rev/s, then RELEASE to freewheel so the
    normal spin pipeline sees a natural decaying spin. Respect direction
    invariants (the generated direction IS the spin direction). Commit,
    flash, verify over the bridge.
T2. Camera calibration: with a gentle `g` move, fit camera scale + sign
    against encoder angle over the same window; store in
    %TEMP%\pw_cam_cal.txt; verify a second move agrees within 1.5 deg.
    From here, every motor experiment gets three-way verification.
T3. Baseline campaign: >= 10 generated spins per direction through the full
    v4 pipeline. Per spin log: engage speed, ramp accel, aborts, landing
    wedge (encoder) vs camera-implied wedge, post-release drift. Build the
    distribution. Catalog every anomaly with timestamps.
T4. Fix what T3 exposes, one minimal change at a time (commit + flash +
    re-test each). Priorities: no aborts, no rattle, no audible click at
    engage, landings on target, cam-enc agreement < 2 deg, holds inaudible.
    Characterize the desync dead zone precisely if raising engage speed
    seems worthwhile; otherwise design within 0.30.
T5. If face-vs-encoder divergence reappears (> 5 deg): stop, attribute with
    the three-way matrix, reproduce ONCE deliberately under camera watch if
    needed, then write the proof + simplest mechanical fix proposal. Do not
    repeat slip-inducing runs beyond proof.
T6. Acceptance: >= 30 generated spins mixed directions with zero dare rests
    (including after release), zero aborts, zero latch events, plausibly
    uniform safe-wedge distribution, cam-enc within 2 deg throughout.
    Then: final REPORT.md at repo root (findings, evidence, remaining risks,
    the owner checklist for a live-guest rehearsal), update CLAUDE_VARIANT.md,
    commit everything on claude/adaptive-v2, checkout clean main, and print
    a one-paragraph summary as your last output.

Cadence rules: prefer many small proven steps; after each firmware change run
at least 4 verification spins before concluding; keep a rolling notes file
(AGENT_NOTES.md) so a restarted session can resume; if the board stops
responding, use the RTS reset procedure; if anything grips a latch, reset,
record, continue.
