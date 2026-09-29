"""Flash 10 (2026-09-29): no crawl over the last peg; tick at flapper release.
Applies to the flash-9 source (party9_20260928).  Exact-once anchors (the pass-1/3
margin line exactly twice); aborts if any drifts.  Universal newlines; the caller
restores CRLF on MILL-PC.

Owner 9/29 08:33: "Sometimes it tries to steer where it would be better to brake at
the end of the spin.  So it slows down to almost a stop and then rotates a wheel a
bit."  With the flapper a free wheel cannot creep over a peg: the 9/28 free-coast
1 kHz tails show a peg stopping the wheel (and bouncing it back) at 0.02-0.04 rev/s.
The controlled stop is a constant-decel ramp (0.05-0.09 rev/s^2), so a landing X deg
past a line crosses that peg at sqrt(2 a X): 3 deg -> ~0.03 rev/s = the motor visibly
pushing a nearly stopped wheel over the last peg.  Flash-9 spins: #7 stopped against
the next peg (target 3.5 deg before it, +3.3 err), #4 landed 6 deg past a peg.
Rule: a planned landing is >= PEG_ENTRY_MARGIN_DEG (9) past the last line crossed
(that peg is crossed at >= ~0.05-0.07 rev/s) and >= 4.5 deg before the next.
1. Pass 1 / pass 3: the travel-entry edge margin is max(existing, 9); pass-3 cushion
   stays mid-span when the span is under 6 deg (review: keep off the dare edge).
2. Shadow, tiered (review: a single strict rule starved it into pass-3 hard brakes):
   tier 1 peg rule within max(natural-16, natural/1.3); tier 2 peg rule to max(natural-16, natural/1.6);
   tier 3 the flash-9 rule (>= 3 deg from any line, to natural-12.5).  All tiers keep
   the landing 6.5 deg from dares; upper bound natural-3.
3. Pass 5: carry clearance 6 -> 9 (the dare's exit peg is the one behind); the
   'before' candidate also needs 9 deg past its own entry peg.
4. Owner 08:39 "make click not straight between wedges but 1 degree after. That's when
   clicker releases": the audio tick fires when the point 1 deg behind the pointer
   (direction of travel, latched with hysteresis; a flip re-bases without a tick)
   crosses a line."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def rep(name, old, new, count=1):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == count, f'{name}: anchor count {n} != {count}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


INO = 'prize_wheel_gpt.ino'
IMPL = 'pw_party_impl.h'

rep(INO, '''const float CARRY_CLEAR_DEG = 6.0f;       // flash 9: was 10 - stop just past (or short of) the dare
''', '''const float CARRY_CLEAR_DEG = 9.0f;       // flash 10: = PEG_ENTRY_MARGIN_DEG (flash 9: 6, before: 10)
// flash 10 (owner 9/29 "slows down to almost a stop and then rotates a wheel a bit"):
// a planned landing sits this far past the last line (peg) crossed, so the ramp
// crosses that peg at >= ~0.05-0.07 rev/s and never shoves a crawling wheel over it.
const float PEG_ENTRY_MARGIN_DEG = 9.0f;
const float PEG_AHEAD_MARGIN_DEG = 4.5f;  // and this far before the next peg
''')

rep(INO, '''TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
''', '''// flash 10: degrees travelled since the last wedge line (peg) at angle a.
static inline float pegBehindDeg(int dir, float a) {
  float w = fmodf(a, WEDGE_DEG);
  if (w < 0.0f) w += WEDGE_DEG;
  return (dir > 0) ? w : WEDGE_DEG - w;
}

TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {
''')

rep(INO, '''      float loM, hiM; wedgeEdgeMargins(w, &loM, &hiM);
''', '''      float loM, hiM; wedgeEdgeMargins(w, &loM, &hiM);
      if (dir > 0) loM = fmaxf(loM, PEG_ENTRY_MARGIN_DEG);   // flash 10: entry edge = the peg behind
      else         hiM = fmaxf(hiM, PEG_ENTRY_MARGIN_DEG);
''', count=2)

rep(INO, '''        float d = fminf(lo + 6.0f, hi);   // cushion past the interior entry''',
    '''        float d = fminf(lo + 6.0f, (hi - lo < 6.0f) ? 0.5f * (lo + hi) : hi);   // cushion past the interior entry (flash 10: mid-span when narrow)''')

rep(INO, '''      bool shadowOk = false;
      const float rFloor = fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f);
      for (int k = 0; k < 32 && !shadowOk; ++k) {
        float r = (k < 26) ? naturalDeg - 6.0f - 0.5f * (float)k       // -6 .. -18.5 (floored below)
                           : naturalDeg - 5.5f + 0.5f * (float)(k - 26); // -5.5 .. -3
        if (r < rFloor || r < naturalDeg - 12.5f) continue;
        float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
        if (landAng < 0.0f) landAng += 360.0f;
        float within = fmodf(landAng, WEDGE_DEG);
        float lineDist = fminf(within, WEDGE_DEG - within);
        if (lineDist >= FLAPPER_KEEPOUT_DEG && !isDare(wedgeAtAngle(landAng)) &&
            dareDistanceDeg(landAng) >= DARE_PROXIMITY_FAULT_DEG + 4.5f) {   // 6.5: covers a 3.8 deg flapper undershoot
          out.runwayDeg = r;
          out.wedge = wedgeAtAngle(landAng);
          shadowOk = true;
        }
      }
      if (!shadowOk) out.found = false;
''', '''      bool shadowOk = false;
      const float rFloor = fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f);
      // flash 10: tiered.  0 = peg rule (9 past the last peg, 4.5 before the next)
      // within 1.3x the natural decel; 1 = peg rule to natural-16; 2 = flash-9 rule
      // (3 deg from any line, to natural-12.5).  Never a pass-3 hard brake first.
      for (int tier = 0; tier < 3 && !shadowOk; ++tier) {
        const float rLow = (tier == 0) ? fmaxf(naturalDeg - 16.0f, naturalDeg / 1.3f)
                         : (tier == 1) ? fmaxf(naturalDeg - 16.0f, naturalDeg / 1.6f) : naturalDeg - 12.5f;
        for (int k = 0; k < 40 && !shadowOk; ++k) {
          float r = (k < 26) ? naturalDeg - 6.0f - 0.5f * (float)k       // -6 .. -18.5 (floored below)
                             : naturalDeg - 5.5f + 0.5f * (float)(k - 26); // -5.5 .. -3 (k < 32)
          if (k >= 32 || r < rFloor || r < rLow) continue;
          float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
          if (landAng < 0.0f) landAng += 360.0f;
          const float behind = pegBehindDeg(dir, landAng);
          const bool pegOk = (tier < 2)
              ? (behind >= PEG_ENTRY_MARGIN_DEG && WEDGE_DEG - behind >= PEG_AHEAD_MARGIN_DEG)
              : (fminf(behind, WEDGE_DEG - behind) >= FLAPPER_KEEPOUT_DEG);
          if (pegOk && !isDare(wedgeAtAngle(landAng)) &&
              dareDistanceDeg(landAng) >= DARE_PROXIMITY_FAULT_DEG + 4.5f) {   // 6.5: covers a 3.8 deg flapper undershoot
            out.runwayDeg = r;
            out.wedge = wedgeAtAngle(landAng);
            shadowOk = true;
          }
        }
      }
      if (!shadowOk) out.found = false;
''')

rep(INO, '''        if (!isDare(wedgeAtAngle(ang)) && dareDistanceDeg(ang) >= DARE_EDGE_MARGIN_DEG) { dBefore = d; break; }''',
    '''        if (!isDare(wedgeAtAngle(ang)) && dareDistanceDeg(ang) >= DARE_EDGE_MARGIN_DEG &&
            pegBehindDeg(dir, ang) >= PEG_ENTRY_MARGIN_DEG) { dBefore = d; break; }   // flash 10''')

# 4. audio tick at flapper release
rep(IMPL, '''static int pwPrevWedge = -1;
''', '''static int pwPrevWedge = -1;
#define PW_TICK_RELEASE_DEG 1.0f   /* flash 10: flapper releases the peg 1 deg past the line */
static int8_t pwTickSign = 1;      /* direction of travel, latched with hysteresis   */
''')
rep(IMPL, '''    int wedge = currentWedge();
    if (wedge != pwPrevWedge) {''', '''    /* flash 10 (owner 9/29 "make click not straight between wedges but 1 degree
     * after. That's when clicker releases"): the flapper touches a peg 0.2 deg
     * before the line and snaps free 1 deg after it, so the tick is taken from
     * the angle 1 deg behind the pointer in the direction of travel.            */
    if (encoderVelocityValid) {
      int8_t sgn = pwTickSign;
      if (omega > 0.03f) sgn = 1;
      else if (omega < -0.03f) sgn = -1;
      if (sgn != pwTickSign) {   /* direction flip: re-base silently, no stray tick */
        pwTickSign = sgn;
        pwPrevWedge = wedgeAtAngle(wheelAngleDeg() - (float)pwTickSign * PW_TICK_RELEASE_DEG);
      }
    }
    int wedge = wedgeAtAngle(wheelAngleDeg() - (float)pwTickSign * PW_TICK_RELEASE_DEG);
    if (wedge != pwPrevWedge) {''')

rep(INO, 'Serial.println(F("# build: party9-20260928 (flash 9:',
    'Serial.println(F("# build: party10-20260929 (flash 10: landings 9 deg past the last peg, tick at flapper release) on party9-20260928 (flash 9:')
print('flash10 applied')
