from pathlib import Path
import shutil
p=Path(__file__).parent;src=p.parent/'prize_wheel_gpt/prize_wheel_gpt.ino'
bak=p/'before-gentle6';bak.mkdir(exist_ok=False);shutil.copy2(src,bak/src.name)
header=r'''#pragma once
#include <stdint.h>

// Attended hardware evaluation only: choose a safe wedge center with enough
// runway for a >=6-second pulse ramp. Does not use the uncalibrated coast model.
struct PwGentleTarget {
  bool found;
  int wedge;
  float angleDeg;
  float runwayDeg;
  uint32_t accelerationLimit;
};

constexpr PwGentleTarget pwGentleTarget(uint32_t entryHz, float curAngle,
    int dir, uint32_t forbiddenMask, int wedges, float stepsPerRev,
    uint32_t maxAcceleration, float minSeconds, float advanceDeg) {
  PwGentleTarget out = {false, -1, 0, 0, 0};
  if (!entryHz || !(curAngle >= 0 && curAngle < 360) ||
      (dir != 1 && dir != -1) || wedges < 1 || wedges > 31 ||
      !(stepsPerRev > 0) || !maxAcceleration || !(minSeconds > 0) ||
      !(advanceDeg >= 0)) return out;
  uint32_t allowed = (uint32_t)((float)entryHz / minSeconds);
  if (allowed > maxAcceleration) allowed = maxAcceleration;
  // Match the existing planner's minimum acceleration; never silently shorten
  // the minimum ramp for a very weak spin.
  if (!allowed || (float)allowed < 0.008f * stepsPerRev) return out;
  float minimum = ((float)entryHz * entryHz) / (2.0f * allowed) *
                  360.0f / stepsPerRev + advanceDeg + 4.0f;
  float best = minimum + 361.0f;
  for (int w = 0; w < wedges; ++w) {
    if (forbiddenMask & (1UL << w)) continue;
    float angle = ((float)w + 0.5f) * (360.0f / wedges);
    float d = dir > 0 ? angle - curAngle : curAngle - angle;
    if (d < 0) d += 360.0f;
    while (d < minimum) d += 360.0f;
    if (d < best) {
      best = d;
      out = {true, w, angle, d, allowed};
    }
  }
  return out;
}

// Compile-time checks exercise the production helper in both directions over
// speeds/angles, including count rounding used by the production planner.
constexpr bool pwGentleTargetChecks() {
  constexpr uint32_t forbidden = (1UL << 1) | (1UL << 5);
  for (int dir = -1; dir <= 1; dir += 2) {
    for (int angle = 0; angle < 360; angle += 15) {
      for (uint32_t hz = 160; hz <= 2176; hz += 137) {
        PwGentleTarget t = pwGentleTarget(hz, (float)angle, dir,
            forbidden, 12, 3200.0f, 320, 6.0f, 0.0f);
        if (!t.found || t.wedge < 0 || t.wedge >= 12 ||
            (forbidden & (1UL << t.wedge)) ||
            t.accelerationLimit > 320 ||
            (float)hz / t.accelerationLimit < 6.0f) return false;
        float target = (float)angle + dir * t.runwayDeg;
        while (target < 0) target += 360.0f;
        while (target >= 360) target -= 360.0f;
        float error = target - t.angleDeg;
        if (error < -0.002f || error > 0.002f) return false;
        uint32_t counts = (uint32_t)(t.runwayDeg * 4096.0f / 360.0f + 0.5f);
        float steps = ((float)counts * 360.0f / 4096.0f) * 3200.0f / 360.0f;
        float needed = (float)hz * hz / (2.0f * steps);
        uint32_t acceleration = (uint32_t)needed;
        if ((float)acceleration < needed) ++acceleration;
        if (acceleration < 26) acceleration = 26;
        if (acceleration > t.accelerationLimit ||
            (float)hz / acceleration < 6.0f) return false;
      }
    }
  }
  return !pwGentleTarget(100, 0, 1, forbidden, 12, 3200, 320, 6, 0).found &&
         !pwGentleTarget(1400, 0, 0, forbidden, 12, 3200, 320, 6, 0).found &&
         !pwGentleTarget(1400, 0, 1, 0xFFF, 12, 3200, 320, 6, 0).found;
}
static_assert(pwGentleTargetChecks(), "gentle target duration/geometry checks");
'''
(p.parent/'prize_wheel_gpt/pw_gentle_test.h').write_bytes(header.replace('\n','\r\n').encode())
s=src.read_text()
def change(a,b):
 global s
 assert s.count(a)==1,a[:100]
 s=s.replace(a,b)
change('#include "pw_brake_profile.h"','#include "pw_brake_profile.h"\n#include "pw_gentle_test.h"')
change('const uint32_t DECEL_CEILING_SPS2     = 650;    // natural-motion decel ceiling','const bool GENTLE_BRAKE_TEST = true; // attended evaluation; bypass guessed friction targeting\nconst float GENTLE_TEST_MIN_SECONDS = 6.0f;\nconst uint32_t DECEL_CEILING_SPS2     = 320;    // 0.10 rev/s^2 maximum')
change('const uint32_t ASSIST_DECEL_MAX_SPS2  = 1100;   // weak-spin nearest-target cap','const uint32_t ASSIST_DECEL_MAX_SPS2  = 320;    // no aggressive fallback')
needle='TargetChoice chooseSafeTarget(int dir, float curAngle, float speedRevS) {'
wrapper=r'''TargetChoice chooseGentleTestTarget(int dir, float curAngle, uint32_t entryHz,
                                   float advanceDeg) {
  uint32_t forbidden = 0;
  for (int w = 0; w < NUM_WEDGES; ++w) if (isDare(w)) forbidden |= 1UL << w;
  PwGentleTarget t = pwGentleTarget(entryHz, curAngle, dir, forbidden,
      NUM_WEDGES, WHEEL_USTEPS_PER_REV, DECEL_CEILING_SPS2,
      GENTLE_TEST_MIN_SECONDS, advanceDeg);
  TargetChoice out;
  out.found = t.found;
  out.wedge = t.wedge;
  out.targetAngleDeg = t.angleDeg;
  out.runwayDeg = t.runwayDeg;
  out.decelCapSps2 = t.accelerationLimit;
  out.quality = 4; // attended six-second-minimum braking test
  return out;
}

'''
change(needle,wrapper+needle+r'''
  if (GENTLE_BRAKE_TEST) {
    uint32_t entryHz = (uint32_t)floorf(fminf(TRAIL_FRACTION * speedRevS,
        CAPTURE_MAX_CMD_REV_S) * WHEEL_USTEPS_PER_REV);
    return chooseGentleTestTarget(dir, curAngle, entryHz,
        speedRevS * ENGAGE_LATENCY_S * 360.0f);
  }
''')
change('if (fits < FIT_PERSIST_MIN_FITS && !urgent &&','if (!GENTLE_BRAKE_TEST && fits < FIT_PERSIST_MIN_FITS && !urgent &&')
needle='  if (hz < 40) return false;\n\n  float remainingDeg = remainingTargetDeg();'
change(needle,r'''  if (hz < 40) return false;

  if (GENTLE_BRAKE_TEST) {
    // Re-select using actual entry speed AFTER precharge. A speed change must
    // not shorten the six-second minimum calculated at reservation.
    TargetChoice test = chooseGentleTestTarget(takeoverDir, wheelAngleDeg(), hz, 0);
    if (!test.found) return false;
    targetCountsMT = encoderCountsMT + takeoverDir * countsForDegrees(test.runwayDeg);
    planDecelCapSps2 = test.decelCapSps2;
    spin.targetWedge = test.wedge;
    spin.targetAngleDeg = test.targetAngleDeg;
    spin.runwayDeg = degreesForCounts(takeoverDir * (targetCountsMT - reserveCounts));
    spin.targetQuality = test.quality;
    Serial.printf("# GENTLE_TEST min_s=%.1f cap=%lu targetW=%d runway=%.1f\n",
        GENTLE_TEST_MIN_SECONDS, (unsigned long)planDecelCapSps2,
        test.wedge, test.runwayDeg);
  }

  float remainingDeg = remainingTargetDeg();''')
change('v2-brake2200-20260918; smooth ramp; brake2200; hold1650; tracking-loss latch','v2-gentle6-20260918; TEST min6s cap320; brake2200; hold1650')
change('uint8_t targetQuality;       // 0 wedge-uniform, 1 nearest-int, 2 edge, 3 shadow','uint8_t targetQuality;       // 0 uniform, 1 nearest, 2 edge, 3 shadow, 4 gentle test')
src.write_bytes(s.replace('\n','\r\n').encode())
for old,new in [('compile_brake2200.py','compile_gentle6.py'),('upload_verify_brake2200.py','upload_verify_gentle6.py'),('brake2200_capture.py','gentle6_capture.py')]:
 s=(p/old).read_text().replace('brake2200','gentle6')
 (p/new).write_bytes(s.replace('\n','\r\n').encode())
(p/'gentle6_commands.txt').write_text('?\ns\nd\n')
print('Prepared gentle6 test firmware; compile-time production-helper checks; no upload yet.')