"""Flash 6 (2026-09-25): a free coast that stops is held (800 mA) at once.
Applies to the flash-5 source (party5_20260925).  Exact-once anchors; aborts if any drifts.
Reads/writes text with universal newlines (the caller restores CRLF on MILL-PC)."""
from pathlib import Path
p = Path(__file__).resolve().parent / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
s = p.read_text(encoding='utf-8')


def once(old, new):
    global s
    n = s.count(old)
    assert n == 1, f'anchor count {n}: {old[:80]!r}'
    s = s.replace(old, new)


# 1. constants + state for the coast hold, next to the hold anchor state
once('''uint32_t holdDeflectSinceMs = 0;
''', '''uint32_t holdDeflectSinceMs = 0;
// flash 6 (owner 2026-09-25 15:16 "Even if motor coasts freely we should hold it after
// it stops"): an unbalanced wheel that coasts to a stop on a slope rolls back (9/25
// spin 16 -> 17: stopped at 350, rolled back onto dare 15, P7).  A free coast that
// stops is now clamped by the hold field at once, before a rollback can start.
const uint16_t COAST_HOLD_STILL_MS = 150;     // still this long -> field on (turning point)
const float COAST_HOLD_STABLE_DEG = 0.3f;     // the engage snap has died when the wheel
const uint16_t COAST_HOLD_STABLE_MS = 500;    // ...stays this close for this long
const float COAST_HOLD_SLIP_DEG = 25.0f;      // a hand dragging the pending hold (a gravity ratchet of
                                              // 1-3 poles is caught, re-checked and held instead)
bool coastHoldPending = false;                // SOFT_HOLD entered from a free coast
bool coastHoldP7 = true;                      // false after a hand stop (owner decision 2: no move)
float coastHoldEngageDeg = 0.0f;
float coastHoldRefDeg = 0.0f, coastHoldMinDev = 0.0f, coastHoldMaxDev = 0.0f;
uint32_t coastHoldRefSinceMs = 0;
''')

# 2. helpers, right before loop()
once('''void loop() {
''', '''// flash 6: static hold field for a free coast that stopped (no pulses).  The
// field comes on at whatever electrical phase the driver was left in, so the
// rotor snaps up to 3.6 deg; SOFT_HOLD anchors only once that has died and
// re-checks the dare zone then (coastHoldPending).
bool coastHoldAvailable() {
  return controlAvailable() && stepper != nullptr && encoderPositionFresh();
}

void engageCoastHold(uint32_t nowMs, bool allowP7) {
  captureLease.cancel();   // no stale EN permission may act on the hold
  if (stepper->isRampGeneratorActive()) stepper->forceStop();   // guarded: see landingVerdict
  setCurrentStage(CS_PRECHARGE);   // freewheel off, outputs enabled
  setCurrentStage(CS_HOLD1);       // 800 mA, the same hold as after a controlled landing
  holdAnchorValid = false; holdDeflectSinceMs = 0;
  coastHoldPending = true;
  coastHoldP7 = allowP7;
  coastHoldEngageDeg = coastHoldRefDeg = wheelAngleDeg();
  coastHoldMinDev = coastHoldMaxDev = 0.0f;
  coastHoldRefSinceMs = nowMs;
  state = ST_SOFT_HOLD;
  stateEnteredMs = nowMs;
  Serial.printf("# COAST-HOLD: free coast stopped at %.1f wedge %d: hold field on\\n",
                coastHoldRefDeg, currentWedge());
}

void loop() {
''')

# 3. free-coast rest: with control available, declare the stop at the turning point
#    (150 ms still) instead of 0.5 s / 2.5 s, and hold instead of freewheeling.
once('''        else if (nowMs - settleStillSinceMs >= (spin.letCoast ? LET_COAST_SETTLE_MS : SETTLE_MS)) {  // flash 5
''', '''        else if (nowMs - settleStillSinceMs >= (coastHoldAvailable() ? COAST_HOLD_STILL_MS    // flash 6
                                                 : spin.letCoast ? LET_COAST_SETTLE_MS : SETTLE_MS)) {  // flash 5
''')
once('''            letCoastWatchUntilMs = nowMs + LET_COAST_WATCH_MS;   // flash 5 (review: also after contact)
          closeSpin(res);
          state = ST_IDLE_STOPPED;
''', '''            letCoastWatchUntilMs = nowMs + LET_COAST_WATCH_MS;   // flash 5 (review: also after contact)
          closeSpin(res);
          if (coastHoldAvailable()) { engageCoastHold(nowMs, !handStop); break; }   // flash 6: hold where it stopped
          state = ST_IDLE_STOPPED;
''')

# 4. SOFT_HOLD: a coast hold anchors only after the engage snap died, then re-checks the dare zone
once('''      uint32_t age = nowMs - stateEnteredMs;
      if (age >= 300 && !holdAnchorValid && encoderPositionFresh()) {
        holdAnchorDeg = wheelAngleDeg(); holdAnchorValid = true;
      }
''', '''      uint32_t age = nowMs - stateEnteredMs;
      bool coastSlip = false;
      if (coastHoldPending) {
        // flash 6: no anchor (so no "pushed" release) while the engage snap rings;
        // a push is caught by the speed test below or by a slip past the worst snap
        // swing.  Once the wheel stays inside a 0.3 deg window for 0.5 s, anchor at
        // the window middle and re-check the dare zone (the snap can carry the wheel
        // up to 3.6 deg toward a dare) - unless a hand stopped it (owner decision 2).
        if (encoderPositionFresh()) {
          float a = wheelAngleDeg();
          float e = fabsf(a - coastHoldEngageDeg);
          if (e > 180.0f) e = 360.0f - e;
          float d = a - coastHoldRefDeg;
          if (d > 180.0f) d -= 360.0f; else if (d < -180.0f) d += 360.0f;
          if (d < coastHoldMinDev) coastHoldMinDev = d;
          if (d > coastHoldMaxDev) coastHoldMaxDev = d;
          if (e > COAST_HOLD_SLIP_DEG) {
            coastSlip = true;
            Serial.printf("# COAST-HOLD released: moved %.1f deg from engage after %lu ms\\n",
                          e, (unsigned long)age);
          } else if (coastHoldMaxDev - coastHoldMinDev > COAST_HOLD_STABLE_DEG) {
            coastHoldRefDeg = a; coastHoldMinDev = coastHoldMaxDev = 0.0f; coastHoldRefSinceMs = nowMs;
          } else if (nowMs - coastHoldRefSinceMs >= COAST_HOLD_STABLE_MS) {
            coastHoldPending = false;
            float mid = fmodf(coastHoldRefDeg + 0.5f * (coastHoldMinDev + coastHoldMaxDev) + 360.0f, 360.0f);
            if (coastHoldP7 && (isDare(wedgeAtAngle(mid)) || dareDistanceDeg(mid) < DARE_PROXIMITY_FAULT_DEG)) {
              Serial.printf("# COAST-HOLD settled at %.1f wedge %d (dare zone): P7\\n", mid, wedgeAtAngle(mid));
              dareRecoveryAttempts = 0;
              if (startDareRecovery("coast hold")) break;
              Serial.println(F("# COAST-HOLD: dare recovery could not start; holding"));
            }
            holdAnchorDeg = mid; holdAnchorValid = true;
            Serial.printf("# COAST-HOLD anchored at %.1f wedge %d after %lu ms\\n",
                          mid, wedgeAtAngle(mid), (unsigned long)age);
          }
        }
      } else if (age >= 300 && !holdAnchorValid && encoderPositionFresh()) {
        holdAnchorDeg = wheelAngleDeg(); holdAnchorValid = true;
      }
''')

# 5. every exit from SOFT_HOLD clears the pending coast hold
once('''      if (pushed || (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S)) {
        holdAnchorValid = false; holdDeflectSinceMs = 0;
''', '''      if (pushed || (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S)) {
        holdAnchorValid = false; holdDeflectSinceMs = 0;
        coastHoldPending = false;   // flash 6
''')
once('''          holdAnchorValid = false; holdDeflectSinceMs = 0;
          latchFault(FC_ENCODER_STALE, "encoder lost in hold; resting wheel released");
''', '''          holdAnchorValid = false; holdDeflectSinceMs = 0;
          coastHoldPending = false;   // flash 6
          latchFault(FC_ENCODER_STALE, "encoder lost in hold; resting wheel released");
''')

# 5b. a slip past the worst snap swing releases like a push
once('''      bool pushed = false;
      if (holdAnchorValid && encoderPositionFresh()) {
''', '''      bool pushed = coastSlip;   // flash 6
      if (holdAnchorValid && encoderPositionFresh()) {
''')
once('''      if (pushed) Serial.printf("# HOLD released: pushed %.2f deg off anchor after %lu ms\\n",
''', '''      if (pushed && !coastSlip) Serial.printf("# HOLD released: pushed %.2f deg off anchor after %lu ms\\n",
''')

# 5c. review: leave SOFT_HOLD at once when the driver check redirected or faulted it,
#     and no stale pending flag survives a re-judged landing
once('''    case ST_SOFT_HOLD: {
      static uint32_t holdBlindSinceMs = 0;
      controlDriverSafe(nowMs);
''', '''    case ST_SOFT_HOLD: {
      static uint32_t holdBlindSinceMs = 0;
      controlDriverSafe(nowMs);
      if (state != ST_SOFT_HOLD) break;   // flash 6 (review): re-apply redirect or fault
''')
once('''    holdAnchorValid = false; holdDeflectSinceMs = 0;
    setCurrentStage(CS_BRAKE);
''', '''    holdAnchorValid = false; holdDeflectSinceMs = 0;
    coastHoldPending = false;   // flash 6
    setCurrentStage(CS_BRAKE);
''')
once('''void landingVerdict() {
''', '''void landingVerdict() {
  coastHoldPending = false;   // flash 6: every verdict ends in a normally anchored hold
''')

# 6. banner
once('''# build: party5-20260925 (flash 5: weak pushes coast free when their stop is safe, 3.0 A) on''',
     '''# build: party6-20260925 (flash 6: a free coast that stops is held at 800 mA) on party5-20260925 (flash 5: weak pushes coast free when their stop is safe, 3.0 A) on''')

p.write_text(s, encoding='utf-8')
print('applied flash-6 edits')
