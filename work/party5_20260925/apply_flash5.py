"""Flash 5 (2026-09-25): weak pushes coast free while their natural stop is safe; 3.0 A.
Applies to the flash-4 source (party4_20260923).  Exact-once anchors; aborts if any drifts.
Reads/writes text with universal newlines (the caller restores CRLF on MILL-PC)."""
from pathlib import Path
p = Path(__file__).resolve().parent / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
s = p.read_text(encoding='utf-8')


def once(old, new):
    global s
    n = s.count(old)
    assert n == 1, f'anchor count {n}: {old[:80]!r}'
    s = s.replace(old, new)


# 1. owner 2026-09-25 "more power": 2.7 -> 3.0 A RMS (motor rating 3 A; TMC5160 at
#    R_SENSE 0.075 reaches 3.06 A; GLOBALSCALER ~250 of 256).  Hold stays 800 mA.
once('''const uint16_t CUR_CAPTURE_MA   = 2700;
const uint16_t CUR_BRAKE_MA     = 2700; // retain capture torque through braking/settling
''', '''const uint16_t CUR_CAPTURE_MA   = 3000;  // flash 5: 2700 -> 3000 (owner: "more power")
const uint16_t CUR_BRAKE_MA     = 3000; // retain capture torque through braking/settling
''')

# 2. spin record flag
once('''  uint8_t frozen;              // 2026-09-23: field frozen (fight/reversal) this spin
''', '''  uint8_t frozen;              // 2026-09-23: field frozen (fight/reversal) this spin
  uint8_t letCoast;            // flash 5: 1 = weak push left to coast, 2 = handed back to capture
''')

# 3. let-coast rule + helper, right before tryReserveTarget
once('''bool tryReserveTarget(uint32_t nowMs) {
''', '''// 2026-09-25 flash 5 (owner: "At slow spins it moves extremely erratically"):
// the motor grabbed every weak push the moment the hand let go (0.15-0.20 rev/s)
// and forced a friction-only slowdown on a wheel that gravity was still moving:
// wheel and field fought (9/25 spins 10 and 12: wheel 8-13 deg ahead, surges).
// Now a weak push is left alone - no motor, it stops by itself and is celebrated
// like any safe landing (RES_NO_REACHABLE_SAFE on a safe wedge) - for as long as
// its predicted stop is clearly safe.  Re-checked every control tick; the band
// runs LET_COAST_PAST_DEG past the prediction because the friction model is
// gravity-blind and over-estimates friction (the real wheel rolls further).  The
// moment the band touches a dare the normal capture (shadow / carry) takes over,
// and a rest on a dare still gets the P7 recovery.
const float LET_COAST_MAX_PEAK_REV_S = 0.30f;
const float LET_COAST_CLEAR_DEG = 4.0f;     // every point of the band this far from any dare
const float LET_COAST_BEFORE_DEG = 6.0f;    // band starts this far short of the prediction
const float LET_COAST_PAST_DEG = 12.0f;     // ...and runs this far past it
const float LET_COAST_ROLLBACK_MAX_REV_S = 0.35f;  // slower reverse roll = gravity, not a guest

bool letCoastStopSafe(int dir, float curAngle, float speedRevS) {
  float nat = naturalStopDistanceDeg(speedRevS, dir);
  if (!isfinite(nat) || nat < 0.0f) return false;
  for (float d = fmaxf(0.0f, nat - LET_COAST_BEFORE_DEG); d <= nat + LET_COAST_PAST_DEG; d += 1.0f) {
    float a = fmodf(curAngle + (float)dir * d, 360.0f);
    if (a < 0.0f) a += 360.0f;
    if (isDare(wedgeAtAngle(a)) || dareDistanceDeg(a) < LET_COAST_CLEAR_DEG) return false;
  }
  return true;
}

bool tryReserveTarget(uint32_t nowMs) {
''')

# 4. apply the rule inside tryReserveTarget, after the engage-window checks
once('''  if (!inWindow && !urgent) return false;
''', '''  if (!inWindow && !urgent) return false;

  // flash 5: a weak push coasts free while its natural stop is clearly safe.
  if (spin.peakRevS <= LET_COAST_MAX_PEAK_REV_S &&
      letCoastStopSafe(spinDir, wheelAngleDeg(), speed)) {
    if (spin.letCoast == 0) {
      spin.letCoast = 1;
      Serial.printf("SPIN#%lu LET-COAST peak=%.3f speed=%.3f natStop=%.1f: no motor, stop is safe\\n",
                    (unsigned long)spin.number, spin.peakRevS, speed,
                    naturalStopDistanceDeg(speed, spinDir));
    }
    return false;
  }
  if (spin.letCoast == 1) {
    spin.letCoast = 2;
    Serial.printf("SPIN#%lu LET-COAST ended at %.3f rev/s: stop no longer clear of dares, capturing\\n",
                  (unsigned long)spin.number, speed);
  }
''')

# 5. review BLOCKER (19:0x agent): a let-coast wheel that stops on a slope rolls back.
#    The reversal branch used to close the spin and reclassify via MOTION_CANDIDATE,
#    whose MANUAL/IDLE exits never run the P7 dare check.  A slow reverse roll of a
#    let-coast wheel (gravity) now stays in SPIN_RELEASED, so the rest path (hand
#    check, P7 dare recovery, landing verdict) always runs.  A fast reversal
#    (>= LET_COAST_ROLLBACK_MAX_REV_S, a guest re-spinning the other way) keeps the
#    old reclassification, which confirms a new spin at that speed and captures it.
once('''      if (omega * (float)spinDir < -SPIN_DETECT_REV_S) {
        if (releasedReverseSinceMs == 0) releasedReverseSinceMs = nowMs;
''', '''      if (omega * (float)spinDir < -SPIN_DETECT_REV_S) {
        if (spin.letCoast && fabsf(omega) < LET_COAST_ROLLBACK_MAX_REV_S) {
          releasedReverseSinceMs = 0;  // flash 5: gravity rollback of a let-coast wheel - stay, rest path runs P7
        } else if (releasedReverseSinceMs == 0) releasedReverseSinceMs = nowMs;
''')
#    ...and a reversed let-coast wheel must not be taken for a forward re-push
#    (SPIN_PUSH's "redirected" exit has the same no-P7 reclassification).
once('''      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S && energyRose) {
''', '''      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S && energyRose &&
          (!spin.letCoast || omega * (float)spinDir > 0.0f)) {  // flash 5: rollback is not a re-push
''')

# 6. SUMMARY carries the flag
once('''      "attempts=%u anomalies=%u syncErr=%.1f frozen=%u\\n",''',
     '''      "attempts=%u anomalies=%u syncErr=%.1f frozen=%u letCoast=%u\\n",''')
once('''      (unsigned)captureCycle.attempts(), (unsigned)spinAnomalies, spin.maxSyncErrDeg,
      (unsigned)spin.frozen);''', '''      (unsigned)captureCycle.attempts(), (unsigned)spinAnomalies, spin.maxSyncErrDeg,
      (unsigned)spin.frozen, (unsigned)spin.letCoast);''')

# 7. banner
once('''# build: party4-20260923 (flash 4: catch speed varies per spin 0.40/0.35/0.30/0.25) on''',
     '''# build: party5-20260925 (flash 5: weak pushes coast free when their stop is safe, 3.0 A) on party4-20260923 (flash 4: catch speed varies per spin 0.40/0.35/0.30/0.25) on''')

# 8. second review (14:1x): a let-coast rest was declared at a swing's turning point
#    (still < 0.02 rev/s for >= SETTLE_MS) and the rollback then ran outside the spin
#    (IDLE -> MOTION_CANDIDATE -> MANUAL -> IDLE: no P7).  (a) a let-coast spin must be
#    still for LET_COAST_SETTLE_MS before it counts as stopped, so a rollback starts
#    inside the spin; (b) for LET_COAST_WATCH_MS after a let-coast landing, an
#    unconfirmed movement that settles on/near a dare gets the P7 recovery.
once("""bool spinOpen = false;
""", """bool spinOpen = false;
uint32_t letCoastWatchUntilMs = 0;   // flash 5: P7 watch after a let-coast landing
""")
once("""    if (isDare(wedgeAtAngle(a)) || dareDistanceDeg(a) < LET_COAST_CLEAR_DEG) return false;
  }
  return true;
}
""", """    if (isDare(wedgeAtAngle(a)) || dareDistanceDeg(a) < LET_COAST_CLEAR_DEG) return false;
  }
  return true;
}

const uint32_t LET_COAST_SETTLE_MS = 2500;   // still this long before a let-coast stop counts
const uint32_t LET_COAST_WATCH_MS = 20000;   // P7 watch on unconfirmed movement afterwards

bool letCoastRollbackGuard(uint32_t nowMs) {
  if (letCoastWatchUntilMs == 0) return false;
  if ((int32_t)(nowMs - letCoastWatchUntilMs) >= 0) { letCoastWatchUntilMs = 0; return false; }
  if (!controlAvailable() || !takeoverEnabled) return false;
  float a = wheelAngleDeg();
  if (!isDare(currentWedge()) && dareDistanceDeg(a) >= DARE_PROXIMITY_FAULT_DEG) return false;
  letCoastWatchUntilMs = 0;
  dareRecoveryAttempts = 0;
  Serial.printf("# LET-COAST rollback settled at %.1f wedge %d (dare zone): P7\\n", a, currentWedge());
  return startDareRecovery("let-coast rollback");
}
""")
once("""  spin.number = spinCounter;
  engageIdx = """, """  spin.number = spinCounter;
  letCoastWatchUntilMs = 0;   // flash 5: a new spin ends the rollback watch
  engageIdx = """)
once("""        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          finishFrictionCapture("coast-end", PwFitEnd::CoastEnd);
""", """        else if (nowMs - settleStillSinceMs >= (spin.letCoast ? LET_COAST_SETTLE_MS : SETTLE_MS)) {  // flash 5
          finishFrictionCapture("coast-end", PwFitEnd::CoastEnd);
""")
once("""            if (startDareRecovery("free coast")) break;
          }
          closeSpin(res);
          state = ST_IDLE_STOPPED;
          stateEnteredMs = nowMs;
""", """            if (startDareRecovery("free coast")) break;
          }
          if (spin.letCoast && !handStop && controlAvailable())
            letCoastWatchUntilMs = nowMs + LET_COAST_WATCH_MS;   // flash 5 (review: also after contact)
          closeSpin(res);
          state = ST_IDLE_STOPPED;
          stateEnteredMs = nowMs;
""")
once("""      // Nudge that already ended.
      if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          state = ST_IDLE_STOPPED;
""", """      // Nudge that already ended.
      if (encoderMotionReady() && fabsf(omega) <= STILL_REV_S) {
        if (settleStillSinceMs == 0) settleStillSinceMs = nowMs;
        else if (nowMs - settleStillSinceMs >= SETTLE_MS) {
          if (letCoastRollbackGuard(nowMs)) break;   // flash 5
          state = ST_IDLE_STOPPED;
""")
once("""                        travel, (unsigned long)(nowMs - stateEnteredMs), currentWedge());
          state = ST_IDLE_STOPPED;
""", """                        travel, (unsigned long)(nowMs - stateEnteredMs), currentWedge());
          if (letCoastRollbackGuard(nowMs)) break;   // flash 5
          state = ST_IDLE_STOPPED;
""")

p.write_text(s, encoding='utf-8')
print('applied flash-5 edits')
