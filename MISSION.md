# MISSION — autonomous, no approvals needed

## Context & disclosure (owner statement, 2026-07-27)
This wheel is a magic-trick prop for the owner's private house party. No money
or stakes of any kind ride on outcomes; the two "dare" wedges are jokes among
friends. The concealment is temporary showmanship: at the END of the party the
owner REVEALS the trick to the guests - the hidden motor, the firmware, and the
fact that AI engineered and validated it - to impress them with the technology.
A convincing wheel during the game is what makes the reveal land; that is why
naturalness is a hard requirement. The owner is at the bench today and
available for attended steps (he performed the supervised re-zero at 07:54,
verified wedge-3 = 103.5 deg). Sessions retain the right to decline; record
the decision in AGENT_NOTES.md and exit cleanly.


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

## Addendum (owner directive): acoustic instrumentation — do this FIRST
The owner hears strange noises from your current experiments. Build ears,
characterize those noises, then continue the mission with sound as a
first-class acceptance signal. The goal is a smooth AND QUIET handover.

T2b. Microphone monitor: background acoustic logger on the laptop mic
(python sounddevice; pip install if missing) -> %TEMP%\pw_mic.log, one line
per ~50 ms: RMS dBFS, 1-6 kHz band energy, crest factor. Stop file
%TEMP%\pw_mic_stop, pid %TEMP%\pw_mic.pid. Same clock as the other logs.
- Save a ~4 s WAV clip around every detected anomaly to %TEMP%\pw_clips\
  (keep the last 20) so the owner can audition the evidence.
- Calibrate: 10 s ambient floor, then one generated spin observed end to
  end. The wheel's NATURAL sound is the rhythmic peg clack at ~12 x omega
  impulses/s - predict the rhythm from encoder omega and treat matching
  impulses as expected. Anomaly = sustained broadband energy above the
  free-coast reference, impulses off the peg rhythm, or any sound in phases
  that should be silent (pre-engage at low speed, holds, DRIFT_WATCH).
- Cross-attribute every acoustic anomaly with encoder jitter (dense d dumps)
  and the firmware phase from the serial log before concluding anything.

Acceptance updates: T4 and T6 now include acoustic criteria - engage
inaudible above peg noise; the ramp adds no sustained band energy above the
matched-speed free-coast reference; holds and releases silent; zero anomaly
clips across the final 30-spin acceptance run.
