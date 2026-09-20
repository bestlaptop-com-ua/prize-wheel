# Plywood wheel: capture fixes and unresolved hardware validation

**This branch is not ready for deployment. Automatic takeover defaults off.**
It integrates reviewed controller fixes into the production selector, but the
new heavy wheel has not passed both-direction pickup, feedback accuracy,
retained-prize or normal hand-spin validation.

## Actual motor-generated hand-spin trials

The wheel was secured with its rotation area inaccessible. Each diagnostic
used a single bounded acceleration to approximately 0.20 rev/s, disabled the
motor to emulate release, then allowed one takeover attempt. Requested current
was capped at the existing 2200 mA; this is a software request, not a measured
phase-current value. Directions below are encoder signs, not a viewing-side
claim about clockwise motion.

| Trial | Result |
| --- | --- |
| Earlier negative, inherited pickup | 350 mA static precharge produced encoder parking and short reverse snaps before any capture pulses. Repeated logical reservations could re-enable precharge during the same coast. |
| Pulse-first negative | Actual pulse proof preceded torque. Capture near 0.152 rev/s, planned deceleration 68 STEP/s², final held encoder error 1.7 degrees. Powered pulse travel 183.600 degrees versus encoder 183.252 degrees. A brief 0.70-degree reverse encoder transient remains. |
| Pulse-first positive | Capture near 0.202 rev/s followed by a 4.746-degree raw-encoder jump in 8.479 ms while pulses advanced 0.5625 degrees. Filtered speed crossed the 0.30 rev/s diagnostic guard; outputs disabled and diagnostic fault15 persisted. No repeat capture occurred. |

The diagnostic used its own nearest-center target policy, not the production
selector in this branch. Its successful direction does not validate this
integration, the 320 STEP/s² configured ceiling, or arbitrary hand-spun phase.
The successfully landed disc moved about 30–31 degrees after the diagnostic
released hold. The timed hold inherited by this production source therefore
still needs a retained-prize design and validation for the new mechanics.

Camera footage shows no sustained whole-disc acceleration matching the
positive pickup's reported 0.301 rev/s, but 20 fps footage cannot resolve the
short encoder excursion. Possible local shaft/coupling movement and magnetic
angle disturbance remain unresolved. A later cross-recording check compares
the two stationary pre-spin baselines, avoiding the late-status timing gap:
raw883/count3301 to raw957/count3227 means -6.504 degrees in the encoder frame,
while the four visible near-hub features change approximately +12.80 degrees
modulo one turn (19.31-degree median disagreement). Both baselines remain fixed
through the start command. This establishes a feature/encoder discrepancy
subject to image geometry and feature attachment; it does not prove coupling
slip or establish that the features are rigidly attached to the plywood.
Moving-phase comparisons still have unmeasured camera exposure latency.
Audio was recorded but not auditioned;
quietness and elimination of rattle are unverified.

## Disabled sensor check

A separate compile-disabled audit was flashed after the failed positive
trial. Three stationary snapshots showed magnet detected, neither weak nor
strong field flag, AGC74, magnitude2078–2079 and stable raw911. EN stayed high,
PCNT stayed zero, fault15/guard0 were preserved, and position stayed287.67deg.
TMC MRES4/INTPOL1/DEDGE0 and GSTAT0 were observed. The snapshot only reads
registers; it does not prove magnetic accuracy under motor excitation or
electrical phase lock. No further powered trial followed.

## Remaining work before release

1. Establish rear-encoder versus actual disc correspondence. With power
   isolated for access, inspect the coupling hubs, shaft fixing and magnet
   attachment. Add visible shaft and disc witness marks. Then compare a slow
   unpowered rotation and synchronized stationary endpoints with raw angle.
   The existing camera cannot see the coupling or rear shaft.
2. Resolve the enable-correlated encoder jump before more capture tuning.
   Basic magnetic status at rest cannot exclude energized-field interference,
   local rotor movement or compliant/slipping connections. Do not hide it by
   raising current, relaxing overspeed limits or filtering away raw evidence.
3. Measure usable coast dynamics after feedback is trustworthy. The inherited
   seed model (`c=.55`, `b=.28`, radians/second units) and bounded entry/ramp
   envelope currently leave no reachable production capture interval. The
   narrow 0.20 rev/s emulation also cannot satisfy the old 1.2 rad/s learning
   span after its sample-speed floor. Persistence fixes do not calibrate this
   heavier, visibly uneven passive wheel.
4. Validate a hold/release policy that retains the selected prize and permits
   a natural next hand spin, including thermal duty and starting resistance.
5. Test the final production selector and effects in both directions after
   those issues are resolved. Require actual pickup, accurate retained landing,
   no repeated excitation of one coast, and direct rattle observation.

## Evidence identity

The frozen pulse-first diagnostic source archive was
`ee489e3159c22f5bc15b758c3ee505af0d769aace472616ff06066aeb73ea777`;
enabled application SHA-256 was
`b7a774c162019bc756d38be5e4712b7c388f2d7f38a615ec8f9a6571d05555bb`.
The disabled audit application was
`d17b3564477c9ecc24e87dce57c2a1a12e2b6c35dd7ac506deafb39a458acb83`.
Raw serial events, 16,384/14,231-row traces and the two complete videos are
retained in the local investigation workspace. Camera credentials and private
footage are not included in this branch.

See [capture integration](CAPTURE_INTEGRATION.md),
[compatibility and persistence](PRODUCTION_COMPATIBILITY.md), and
[pinned build instructions](BUILD_PRODUCTION.md).
