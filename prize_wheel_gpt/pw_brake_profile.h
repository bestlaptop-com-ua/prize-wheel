#pragma once
#include <math.h>
#include <stdint.h>

struct PwBrakePlan {
  bool feasible;
  uint32_t accelerationSps2;
  float decelRevS2;
};

// Same total deceleration is used for feasibility, pulse ramps and stopping.
inline float pwBrakeDistanceDeg(float speedRevS, float decelRevS2) {
  if (!(speedRevS >= 0.0f) || !(decelRevS2 > 0.0f)) return INFINITY;
  return 180.0f * speedRevS * speedRevS / decelRevS2;
}

inline PwBrakePlan pwPlanBrake(uint32_t entryHz, float remainingDeg,
                              float stepsPerRev, uint32_t capSps2) {
  PwBrakePlan out = {false, 0, 0.0f};
  if (!entryHz || !(remainingDeg > 0.0f) || !isfinite(remainingDeg) ||
      !(stepsPerRev > 0.0f) || !isfinite(stepsPerRev) || !capSps2) return out;
  float steps = remainingDeg * stepsPerRev / 360.0f;
  float required = ceilf((float)entryHz * (float)entryHz / (2.0f * steps));
  required = fmaxf(required, ceilf(0.008f * stepsPerRev));
  if (!isfinite(required) || required > (float)capSps2) return out;
  out.feasible = true;
  out.accelerationSps2 = (uint32_t)required;
  out.decelRevS2 = required / stepsPerRev;
  return out;
}

inline float pwLimitBrakeCommand(float previous, float desired,
                                float decelRevS2, float dtSeconds) {
  // A delayed loop must not deliver a large catch-up change in one update.
  float dt = fmaxf(0.0f, fminf(dtSeconds, 0.050f));
  float lower = fmaxf(0.0f, previous - decelRevS2 * dt);
  return fminf(previous, fmaxf(lower, desired));
}
