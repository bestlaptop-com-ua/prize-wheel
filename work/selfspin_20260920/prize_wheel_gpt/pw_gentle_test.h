#pragma once
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
