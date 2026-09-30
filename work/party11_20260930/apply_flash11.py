"""Flash 11 (2026-09-30): firmer steered stops (brakeGain), tunable live.
Applies to the flash-10 source (party10_20260929).  Exact-count anchors; aborts if
any drifts.  Universal newlines; the caller restores CRLF on MILL-PC.

Owner 9/30 08:32: "I still think it oversteers. Can we make braking rate higher."
Flash-10 spins 1-42: steered stops were planned at ~1.0-1.05x the learned natural
deceleration - capture at 0.25-0.40 rev/s, then 200-450 deg / 4-8 s of slow
controlled roll.  The free-coast predictions are fine, so the plan changes, not the
model.  brakeGain (default 1.4, '+'/'-' at rest in 0.1 steps, 1.0-1.6, NVS
'brakeGain', shown by 'f' and in RESERVE lines):
1. Pass 1 (wedge-uniform) plans as if friction were brakeGain x the model: its
   window top, coupling floor, the urgency window and the pass-1 plan check use
   natural/g and g x natural decel.
2. Shadow gets a firm tier first: landing from natural/min(g,1.3) up to natural-6
   (firmest first), peg rule, >= 6.5 deg from dares, and only if the whole stretch
   from that landing to the TRUE natural stop (+12) is dare-free, so a slip or an
   undershoot still rolls onto safe ground; else the flash-10 tiers.
3. Passes 3/4/5 (carry / before / nearest interior) and every safety test keep the
   TRUE natural stop (review 9/30: scaling them put the fallbacks in the 9/23 slip
   regime).  Exact-stop cap: min(1.25 g, 1.6) x true natural decel."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def rep(name, old, new, count=1):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == count, f'{name}: anchor count {n} != {count}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


def rep_in_segment(name, start, end, old, new, count):
    p = D / name
    s = p.read_text(encoding='utf-8')
    i = s.index(start); j = s.index(end, i)
    seg = s[i:j]
    n = seg.count(old)
    assert n == count, f'{name}: segment anchor count {n} != {count}: {old!r}'
    p.write_text(s[:i] + seg.replace(old, new) + s[j:], encoding='utf-8', newline='\n')


INO = 'prize_wheel_gpt.ino'

rep(INO, '''const float COUPLE_MARGIN             = 1.05f;
''', '''const float COUPLE_MARGIN             = 1.05f;
// flash 11 (owner 9/30 "it oversteers ... make braking rate higher"): pass 1 plans
// as if friction were brakeGain x the learned model; shadow tries a firmer stop
// first.  Live-tunable at rest with '+'/'-', NVS "brakeGain".
const float BRAKE_GAIN_DEFAULT = 1.4f;
const float BRAKE_GAIN_MIN = 1.0f, BRAKE_GAIN_MAX = 1.6f;
float brakeGain = BRAKE_GAIN_DEFAULT;
''')

rep(INO, '''TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
''', '''// flash 11: pass 1's view of the coast (see BRAKE_GAIN_DEFAULT).
float plannedStopDistanceDeg(float speedRevS, int dir) {
  return naturalStopDistanceDeg(speedRevS, dir) / brakeGain;
}
float plannedDecelRevS2(float speedRevS, int dir) {
  return naturalDecelRevS2(speedRevS, dir) * brakeGain;
}

TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
''')

# pass 1 window uses the planned coast; passes 3/4 keep the true one
rep(INO, '''  float winMax = NATURAL_REACH_FRACTION * naturalDeg - NATURAL_SHAVE_MARGIN_DEG;
''', '''  float winMax = NATURAL_REACH_FRACTION * naturalDeg - NATURAL_SHAVE_MARGIN_DEG;   // passes 3/4: true coast
  float winMaxP1 = NATURAL_REACH_FRACTION * plannedStopDistanceDeg(speedRevS, dir)
                 - NATURAL_SHAVE_MARGIN_DEG;                                       // flash 11: pass 1
''')
rep_in_segment(INO, '  // Pass 1: wedge-uniform', '  // Pass 2 (shadow capture)', 'winMax', 'winMaxP1', 4)
rep(INO, '''(2.0f * COUPLE_MARGIN * naturalDecelRevS2(speedRevS, dir)) * 360.0f;''',
    '''(2.0f * COUPLE_MARGIN * plannedDecelRevS2(speedRevS, dir)) * 360.0f;   // flash 11''', count=2)
rep(INO, '''  float winMax = NATURAL_REACH_FRACTION * naturalStopDistanceDeg(speedRevS, dir)''',
    '''  float winMax = NATURAL_REACH_FRACTION * plannedStopDistanceDeg(speedRevS, dir)   // flash 11''')
rep(INO, '''plan.decelRevS2 > naturalDecelRevS2(forward, takeoverDir))) return false;''',
    '''plan.decelRevS2 > plannedDecelRevS2(forward, takeoverDir))) return false;   // flash 11''')
rep(INO, '''        const float nat = 1.25f * naturalDecelRevS2(fmaxf(forward, 0.0f), takeoverDir) * WHEEL_USTEPS_PER_REV;''',
    '''        const float nat = fminf(1.25f * brakeGain, 1.6f) *   // flash 11
                          naturalDecelRevS2(fmaxf(forward, 0.0f), takeoverDir) * WHEEL_USTEPS_PER_REV;''')

# shadow: firm tier first (true-natural safety stretch)
rep(INO, '''      const float rFloor = fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f);
''', '''      const float rFloor = fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f);
      // flash 11: firmer stop first (owner 9/30 "make braking rate higher") -
      // at most min(brakeGain, 1.3) x the natural decel, firmest first, and only
      // when the whole stretch from the landing to the TRUE natural stop (+12) is
      // dare-free, so a slip or undershoot still rolls onto safe ground.
      if (brakeGain > 1.05f) {
        const float rHard = fmaxf(naturalDeg / fminf(brakeGain, 1.3f), rFloor);
        // one backward scan: the lowest sClear with [sClear, natural+12] dare-free
        // (12 = LET_COAST_PAST_DEG: the real wheel may roll past the prediction)
        float sClear = naturalDeg + 13.0f;
        for (float s = naturalDeg + 12.0f; s >= rHard; s -= 1.0f) {
          float a = fmodf(curAngle + (float)dir * s, 360.0f);
          if (a < 0.0f) a += 360.0f;
          if (isDare(wedgeAtAngle(a)) || dareDistanceDeg(a) < DARE_PROXIMITY_FAULT_DEG) break;
          sClear = s;
        }
        for (float r = fmaxf(rHard, sClear); r <= naturalDeg - 6.0f && !shadowOk; r += 0.5f) {
          float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
          if (landAng < 0.0f) landAng += 360.0f;
          const float behind = pegBehindDeg(dir, landAng);
          if (behind < PEG_ENTRY_MARGIN_DEG || WEDGE_DEG - behind < PEG_AHEAD_MARGIN_DEG ||
              isDare(wedgeAtAngle(landAng)) ||
              dareDistanceDeg(landAng) < DARE_PROXIMITY_FAULT_DEG + 4.5f) continue;
          out.runwayDeg = r;
          out.wedge = wedgeAtAngle(landAng);
          shadowOk = true;
        }
      }
''')

rep(INO, '''    takeoverEnabled = preferences.getBool("takeover", false);
''', '''    takeoverEnabled = preferences.getBool("takeover", false);
    {  // flash 11
      float g = preferences.getFloat("brakeGain", BRAKE_GAIN_DEFAULT);
      brakeGain = (isfinite(g) && g >= BRAKE_GAIN_MIN && g <= BRAKE_GAIN_MAX) ? g : BRAKE_GAIN_DEFAULT;
      Serial.printf("# brakeGain=%.2f (from NVS)\\n", brakeGain);
    }
''')

rep(INO, '''    case 'f':
      Serial.printf("# friction cw: c=%.4f b=%.4f fits=%u | ccw: c=%.4f b=%.4f fits=%u\\n",
                    cw_c, cw_b, cwFitCount, ccw_c, ccw_b, ccwFitCount);
      break;''', '''    case 'f':
      Serial.printf("# friction cw: c=%.4f b=%.4f fits=%u | ccw: c=%.4f b=%.4f fits=%u | brakeGain=%.2f\\n",
                    cw_c, cw_b, cwFitCount, ccw_c, ccw_b, ccwFitCount, brakeGain);
      break;
    case '+':
    case '-':   // flash 11: steered-stop braking rate, at rest only
      if (state != ST_IDLE_STOPPED && state != ST_SOFT_HOLD)
        Serial.println(F("# brake gain: only at rest"));
      else {
        float g = brakeGain + ((command == '+') ? 0.1f : -0.1f);
        g = roundf(g * 10.0f) / 10.0f;
        if (g < BRAKE_GAIN_MIN) g = BRAKE_GAIN_MIN;
        if (g > BRAKE_GAIN_MAX) g = BRAKE_GAIN_MAX;
        brakeGain = g;
        bool saved = preferencesAvailable && preferences.putFloat("brakeGain", g) == sizeof(float);
        Serial.printf("# brakeGain=%.2f persisted=%d\\n", brakeGain, saved ? 1 : 0);
      }
      break;''')

rep(INO, '''                "speed=%.3f q=%u urgent=%d\\n",
                (unsigned long)spin.number, choice.wedge, choice.targetAngleDeg,
                choice.runwayDeg, spin.naturalStopDeg, speed, choice.quality,
                urgent && !inWindow);''', '''                "speed=%.3f q=%u urgent=%d g=%.2f\\n",
                (unsigned long)spin.number, choice.wedge, choice.targetAngleDeg,
                choice.runwayDeg, spin.naturalStopDeg, speed, choice.quality,
                urgent && !inWindow, brakeGain);''')

rep(INO, '''    " e  toggle automatic takeover\\n"''', '''    " e  toggle automatic takeover\\n"
    " +/- steered-stop braking rate (brakeGain, 1.0-1.6, at rest)\\n"''')

rep(INO, 'Serial.println(F("# build: party10-20260929 (flash 10:',
    'Serial.println(F("# build: party11-20260930 (flash 11: firmer steered stops, brakeGain 1.4, +/- live) on party10-20260929 (flash 10:')
print('flash11 applied')
