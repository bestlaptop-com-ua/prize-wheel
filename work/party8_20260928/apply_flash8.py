"""Flash 8 (2026-09-28): flapper-aware landings.  Applies to the flash-7 source
(party7_20260928).  Exact-once anchors; aborts if any drifts.  Universal newlines;
the caller restores CRLF on MILL-PC.

Owner 17:41 "installed flapper ... Sometimes its final steer is not natural";
18:11 "Crept to one [a peg]; flapper ... touches about 0.2 deg [before the line]
and releases 1 deg after".
1. Exact final ramp: RAMP_STOP kept the capture-time deceleration, so the field
   stopped (lead - remain) past the target and the wheel ~1.5 deg beyond that:
   103 steered spins 9/22-9/28 err = +3.6 mean (up to +12), and err - (lead - remain)
   = -0.5 +/- 2.  At RAMP_STOP the ramp is now re-sized to the remaining distance
   minus that measured 1.5 deg wheel lead (only ever steeper, never above the
   spin's own decel cap).
2. Shadow landings (quality 3, 53 of 103 spins) sat at natural-6 with no regard
   for wedge lines; 24 of 103 landings ended within 2 deg of a line (those spins
   were before the flapper went in, 9/28 ~17:45; with it, such a stop = the wheel
   creeping up to a peg and parking the flapper on it).  The shadow runway is now
   shortened (up to 6.5 deg more braking) until the landing is >= 3 deg from every
   line and still clear of dares; if no such runway exists it keeps the old one."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def once(name, old, new):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == 1, f'{name}: anchor count {n}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


INO = 'prize_wheel_gpt.ino'

# 1. constants next to the stop gate
once(INO, '''const float OVERSHOOT_TOL_DEG         = 4.0f;
''', '''const float OVERSHOOT_TOL_DEG         = 4.0f;
// flash 8 (2026-09-28): exact final ramp + flapper keep-out (owner: the wheel
// "crept to" a peg).  103 steered spins: the wheel rests 1.5 deg (sd 2) beyond
// the field's stop point, and the flapper touches a peg 0.2 deg before a wedge
// line and releases it 1 deg after.
const float EXACT_STOP_WHEEL_LEAD_DEG = 1.5f;
const float EXACT_STOP_MIN_DEG        = 3.0f;
const float FLAPPER_KEEPOUT_DEG       = 3.0f;   // shadow landings stay this far from any line
''')

# 2. shadow pass: keep the landing off the pegs
once(INO, '''      out.found = true;
      out.wedge = wedgeAtAngle(settleAng);
      out.runwayDeg = naturalDeg - 6.0f;   // 2026-09-22: 2 deg was inside prediction noise (plan refused)
      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;
      out.quality = 3;
''', '''      out.found = true;
      out.wedge = wedgeAtAngle(settleAng);
      out.runwayDeg = naturalDeg - 6.0f;   // 2026-09-22: 2 deg was inside prediction noise (plan refused)
      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;
      out.quality = 3;
      // flash 8: never park the flapper on a peg.  Brake a little more (up to
      // 6.5 deg) until the landing is FLAPPER_KEEPOUT_DEG from every line, in
      // a safe wedge, clear of dares; otherwise keep the legacy runway.
      for (float r = naturalDeg - 6.0f; r >= naturalDeg - 12.5f && r >= fmaxf(shadowMin, MIN_RESERVE_RUNWAY_DEG + 1.0f); r -= 0.5f) {
        float landAng = fmodf(curAngle + (float)dir * r, 360.0f);
        if (landAng < 0.0f) landAng += 360.0f;
        float within = fmodf(landAng, WEDGE_DEG);
        float lineDist = fminf(within, WEDGE_DEG - within);
        if (lineDist >= FLAPPER_KEEPOUT_DEG && !isDare(wedgeAtAngle(landAng)) &&
            dareDistanceDeg(landAng) >= DARE_PROXIMITY_FAULT_DEG + 3.0f) {
          out.runwayDeg = r;
          out.wedge = wedgeAtAngle(landAng);
          break;
        }
      }
''')

# 3. exact final ramp at the stop request
once(INO, '''  if (remaining < -OVERSHOOT_TOL_DEG || remaining <= stopLeadDeg) {
    stopRequested = true;
    stepper->stopMove();
    Serial.printf("# RAMP_STOP a=%lu remain=%.1f lead=%.1f fas=%.3f\\n",
                  (unsigned long)stepper->getAcceleration(), remaining,
                  stopLeadDeg, fasWheelRevS());
    return;
  }
''', '''  if (remaining < -OVERSHOOT_TOL_DEG || remaining <= stopLeadDeg) {
    // flash 8: re-size the final ramp so the field stops EXACT_STOP_WHEEL_LEAD_DEG
    // short of the target (the wheel rests that much beyond the field).  Only
    // ever steeper than the capture plan, never above this spin's decel cap.
    int exact = 0;
    float need = 0.0f;
    if (remaining - EXACT_STOP_WHEEL_LEAD_DEG >= EXACT_STOP_MIN_DEG) {
      const float v = fasWheelRevS();
      const float distRev = (remaining - EXACT_STOP_WHEEL_LEAD_DEG) / 360.0f;
      need = v * v / (2.0f * distRev) * WHEEL_USTEPS_PER_REV;
      const uint32_t cur = stepper->getAcceleration();
      uint32_t cap = (planDecelCapSps2 > cur) ? planDecelCapSps2 : cur;
      if (spin.targetQuality == 0) {  // pass 1 stays near the natural decel (model runs ~30% low with the flapper)
        const float nat = 1.25f * naturalDecelRevS2(fmaxf(forward, 0.0f), takeoverDir) * WHEEL_USTEPS_PER_REV;
        if (isfinite(nat) && nat < (float)cap) cap = (nat > (float)cur) ? (uint32_t)nat : cur;
      }
      if (isfinite(need) && need > (float)cur) {
        uint32_t sps2 = (need >= (float)cap) ? cap : (uint32_t)ceilf(need);
        if (sps2 > cur && fasSetAcceleration(sps2)) {
          stepper->applySpeedAcceleration();
          planDecelRevS2 = (float)sps2 / WHEEL_USTEPS_PER_REV;
          exact = 1;
        }
      }
    }
    stopRequested = true;
    stepper->stopMove();
    Serial.printf("# RAMP_STOP a=%lu remain=%.1f lead=%.1f fas=%.3f exact=%d need=%.0f\\n",
                  (unsigned long)stepper->getAcceleration(), remaining,
                  stopLeadDeg, fasWheelRevS(), exact, need);
    return;
  }
''')

# 3b. stale comment
once(INO, '''  // Both overshoot and normal stop use the SAME planned acceleration.
''', '''  // Overshoot and normal stop use the planned acceleration; flash 8 re-sizes
  // it once at the stop request (exact final ramp, below).
''')

# 4. banner
once(INO, 'Serial.println(F("# build: party7-20260928 (flash 7:',
     'Serial.println(F("# build: party8-20260928 (flash 8: exact final ramp, shadow landings 3 deg off the flapper pegs) on party7-20260928 (flash 7:')
print('flash8 applied')
