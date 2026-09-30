"""Flash 12 (2026-09-30): the wedge-uniform pick works again; every landing balances.
Applies to the flash-11 source (party11_20260930).  Exact-count anchors; aborts if
any drifts.  Universal newlines; the caller restores CRLF on MILL-PC.

Owner 9/30 09:25: "It seems like it prefers some wedges and avoid other. I've never
seen 1 and 10 for example."  Log, flash 11 (30 spins): 0 pass-1 (wedge-uniform)
picks, 26 of 30 landings on a wedge next to a dare (2/4/7/9/12/14/15/17), labels
6/10/11 never.  Flash 10 (46 spins, g=1.0) was fair (chi-square p=0.83).
Cause: flash 11 scaled BOTH ends of the pass-1 window by brakeGain - top
0.9*natural/g - 5, coupling floor latency + (0.98v)^2/(2*1.05*g*a) - so the fixed
latency and shave terms closed it at every catch speed (friction-model maths, cw and
ccw, 0.25-0.40 rev/s: width -2 .. -19 deg).  Every spin fell to the shadow firm tier,
which takes the FIRST acceptable spot after the last dare, or to pass 3 hard brakes
(7 of 30; spin 30 slipped 10.5 deg at 1.8x).
1. Pass 1 window top = the TRUE natural coast again (as flash 10); its coupling floor
   and plan check use min(brakeGain, 1.3) x natural decel (the firm-tier cap).
   reachWindowWidthDeg matches (only feeds urgency/cal-defer; at catch speeds
   inWindow is always true).
2. Balancing counts every finished spin's landing wedge (closeSpin; guest re-spins
   excluded) instead of only pass-1 picks.  RAM only, as before.
3. Shadow firm tier: every acceptable landing grouped by wedge, deficit-weighted
   wedge, then a random 0.5-deg grid point inside its span - instead of always the
   firmest (first after a dare).  Same checks on every candidate; nothing
   safety-relevant changes.  Passes 3/4/5 and all safety tests untouched.
Review 9/30 (applied): pass-1 floor also >= natural/min(g,1.3), never firmer than the
firm tier (widths now cw 21/32/43/56, ccw 18/28/39/51 deg); pass-1 exact-stop cap
min(min(g,1.3)+0.25, 1.6) x natural (flash 10: 1.25); only real landings count;
deficitPick guards n=0."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def rep(name, old, new, count=1):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == count, f'{name}: anchor count {n} != {count}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


INO = 'prize_wheel_gpt.ino'

rep(INO, '''const float BRAKE_GAIN_MIN = 1.0f, BRAKE_GAIN_MAX = 1.6f;
''', '''const float BRAKE_GAIN_MIN = 1.0f, BRAKE_GAIN_MAX = 1.6f;
// flash 12: pass 1 and the shadow firm tier never plan firmer than this x natural.
const float FIRM_GAIN_CAP = 1.3f;
''')

# 1. pass-1 window: true natural top, capped-gain floor/check
rep(INO, '''// flash 11: pass 1's view of the coast (see BRAKE_GAIN_DEFAULT).
float plannedStopDistanceDeg(float speedRevS, int dir) {
  return naturalStopDistanceDeg(speedRevS, dir) / brakeGain;
}
float plannedDecelRevS2(float speedRevS, int dir) {
  return naturalDecelRevS2(speedRevS, dir) * brakeGain;
}
''', '''// flash 12: pass 1's braking-rate ceiling, min(brakeGain, 1.3) x natural decel;
// its window top is the TRUE natural coast.  Flash 11 also put the top at
// natural/g and the window closed at every catch speed (9/30: 0 of 30 wedge-
// uniform picks, landings piled next to the dares).
static inline float firmGain() { return fminf(brakeGain, FIRM_GAIN_CAP); }
float plannedDecelRevS2(float speedRevS, int dir) {
  return naturalDecelRevS2(speedRevS, dir) * firmGain();
}

// flash 12: deficit-weighted draw among candidate wedges (every candidate has
// already passed its pass's full safety test): a wedge that has landed less this
// power cycle is proportionally likelier.
static uint8_t deficitPick(const int* wedges, uint8_t n) {
  if (n == 0) return 0;   // callers guard; never index past the array
  uint16_t maxLanded = 0;
  for (uint8_t i = 0; i < n; ++i)
    if (wedgeChosenCount[wedges[i]] > maxLanded) maxLanded = wedgeChosenCount[wedges[i]];
  uint32_t weights[NUM_WEDGES];
  uint32_t totalWeight = 0;
  for (uint8_t i = 0; i < n; ++i) {
    weights[i] = 1u + (uint32_t)(maxLanded - wedgeChosenCount[wedges[i]]);
    totalWeight += weights[i];
  }
  uint32_t r = (uint32_t)random((long)totalWeight);
  for (uint8_t i = 0; i < n; ++i) {
    if (r < weights[i]) return i;
    r -= weights[i];
  }
  return n - 1;
}
''')
rep(INO, '''  float winMaxP1 = NATURAL_REACH_FRACTION * plannedStopDistanceDeg(speedRevS, dir)
                 - NATURAL_SHAVE_MARGIN_DEG;                                       // flash 11: pass 1
''', '''  float winMaxP1 = winMax;   // flash 12: true coast (flash 11's natural/g closed pass 1)
''')
rep(INO, '''  float winMax = NATURAL_REACH_FRACTION * plannedStopDistanceDeg(speedRevS, dir)   // flash 11
''', '''  float winMax = NATURAL_REACH_FRACTION * naturalStopDistanceDeg(speedRevS, dir)   // flash 12: true coast
''')

# review 9/30: pass 1 never plans firmer than the firm tier (landing >= natural/1.3)
rep(INO, '''  winMin = fmaxf(winMin, profileMin);
  float assistMin''', '''  winMin = fmaxf(winMin, profileMin);
  winMin = fmaxf(winMin, naturalDeg / firmGain());   // flash 12: pass 1 <= firm-tier braking
  float assistMin''')

# pass 1 draw via the shared helper; counting moves to closeSpin
rep(INO, '''    uint16_t maxChosen = 0;
    for (uint8_t i = 0; i < candCount; ++i) {
      uint16_t ct = wedgeChosenCount[candWedge[i]];
      if (ct > maxChosen) maxChosen = ct;
    }
    uint32_t weights[NUM_WEDGES];
    uint32_t totalWeight = 0;
    for (uint8_t i = 0; i < candCount; ++i) {
      weights[i] = 1u + (uint32_t)(maxChosen - wedgeChosenCount[candWedge[i]]);
      totalWeight += weights[i];
    }
    uint32_t r = (uint32_t)random((long)totalWeight);
    uint8_t pick = candCount - 1;
    for (uint8_t i = 0; i < candCount; ++i) {
      if (r < weights[i]) { pick = i; break; }
      r -= weights[i];
    }
    if (wedgeChosenCount[candWedge[pick]] < 60000u) ++wedgeChosenCount[candWedge[pick]];
''', '''    const uint8_t pick = deficitPick(candWedge, candCount);   // flash 12: counted at landing
''')
rep(INO, '''// Deficit-weighted selection state: how many times each wedge has been chosen
// by Pass 1 this power cycle.  RAM only - a reboot restarts the balancing.
''', '''// Deficit-weighted selection state: how many spins have landed on each wedge
// this power cycle (flash 12: every finished spin, counted in closeSpin; was
// pass-1 picks only).  RAM only - a reboot restarts the balancing.
''')

# 2. count every finished landing
rep(INO, '''  spin.finalWedge = currentWedge();
  if (spin.targetWedge >= 0) {
''', '''  spin.finalWedge = currentWedge();
  // flash 12: balance on real landings (not re-spins, faults or control locks)
  const bool landed = result == RES_CONTROLLED_SAFE || result == RES_EDGE_SAFE ||
                      result == RES_OFF_TARGET_SAFE || result == RES_GUEST_STOPPED ||
                      result == RES_NO_REACHABLE_SAFE;
  if (landed && spin.finalWedge >= 0 && spin.finalWedge < NUM_WEDGES &&
      !isDare(spin.finalWedge) && wedgeChosenCount[spin.finalWedge] < 60000u)
    ++wedgeChosenCount[spin.finalWedge];
  if (spin.targetWedge >= 0) {
''')

# 3. firm tier: weighted over every acceptable landing
rep(INO, '''      // flash 11: firmer stop first (owner 9/30 "make braking rate higher") -
      // at most min(brakeGain, 1.3) x the natural decel, firmest first, and only
      // when the whole stretch from the landing to the TRUE natural stop (+12) is
      // dare-free, so a slip or undershoot still rolls onto safe ground.
      if (brakeGain > 1.05f) {
        const float rHard = fmaxf(naturalDeg / fminf(brakeGain, 1.3f), rFloor);''',
'''      // flash 11: firmer stop first (owner 9/30 "make braking rate higher") -
      // at most min(brakeGain, 1.3) x the natural decel, and only when the whole
      // stretch from the landing to the TRUE natural stop (+12) is dare-free, so a
      // slip or undershoot still rolls onto safe ground.  flash 12: not the firmest
      // (= always the first spot after a dare) but a deficit-weighted wedge among
      // every acceptable landing, then a random point inside its span.
      if (brakeGain > 1.05f) {
        const float rHard = fmaxf(naturalDeg / firmGain(), rFloor);''')
rep(INO, '''        for (float r = fmaxf(rHard, sClear); r <= naturalDeg - 6.0f && !shadowOk; r += 0.5f) {
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
      }''', '''        int fW[NUM_WEDGES];
        float fLo[NUM_WEDGES], fHi[NUM_WEDGES];
        uint8_t fN = 0;
        for (float r = fmaxf(rHard, sClear); r <= naturalDeg - 6.0f; r += 0.5f) {
          float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
          if (landAng < 0.0f) landAng += 360.0f;
          const float behind = pegBehindDeg(dir, landAng);
          if (behind < PEG_ENTRY_MARGIN_DEG || WEDGE_DEG - behind < PEG_AHEAD_MARGIN_DEG ||
              isDare(wedgeAtAngle(landAng)) ||
              dareDistanceDeg(landAng) < DARE_PROXIMITY_FAULT_DEG + 4.5f) continue;
          const int w = wedgeAtAngle(landAng);
          // extend a span only across consecutive accepted grid points, so every
          // grid point inside [fLo, fHi] passed the checks above
          if (fN > 0 && fW[fN - 1] == w && r - fHi[fN - 1] < 0.75f) { fHi[fN - 1] = r; continue; }
          if (fN >= NUM_WEDGES) break;
          fW[fN] = w; fLo[fN] = r; fHi[fN] = r; ++fN;
        }
        if (fN > 0) {
          const uint8_t k = deficitPick(fW, fN);
          const long steps = lroundf((fHi[k] - fLo[k]) / 0.5f);
          out.runwayDeg = fLo[k] + 0.5f * (float)random(steps + 1);
          out.wedge = fW[k];
          shadowOk = true;
        }
      }''')

rep(INO, '''        const float nat = fminf(1.25f * brakeGain, 1.6f) *   // flash 11''',
    '''        const float nat = fminf(firmGain() + 0.25f, 1.6f) *   // flash 12 (flash 10: 1.25 at g=1)''')

rep(INO, 'Serial.println(F("# build: party11-20260930 (flash 11:',
    'Serial.println(F("# build: party12-20260930 (flash 12: wedge-uniform pick restored, every landing balances) on party11-20260930 (flash 11:')
print('flash12 applied')
