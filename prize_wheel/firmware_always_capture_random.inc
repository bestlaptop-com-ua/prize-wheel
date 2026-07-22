/*
 * Always-capture random-safe experiment.
 *
 * This layer deliberately ignores the natural-landing predictor. Every
 * confirmed spin that reaches the normal decision-speed window is captured,
 * and the controller chooses uniformly among safe wedge centers that are
 * reachable in the current direction without reversing or exceeding the
 * existing 180-degree powered-runway envelope.
 *
 * It is therefore random among REACHABLE safe wedges, not globally uniform
 * among all ten safe wedges on every individual spin.
 */

static bool chooseRandomReachableSafeTarget(int direction, float currentAngle,
                                            float minRunwayDeg,
                                            float& targetAngle,
                                            float& runwayDeg) {
  float targetCandidates[NUM_WEDGES];
  float runwayCandidates[NUM_WEDGES];
  uint8_t candidateCount = 0;

  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (isDare(wedge)) continue;

    float target = (wedge + 0.5f) * WEDGE_DEG;
    float distance = forwardDistanceDeg(direction, currentAngle, target);
    if (distance < minRunwayDeg || distance > DRAG_MAX_RUNWAY_DEG) continue;

    targetCandidates[candidateCount] = target;
    runwayCandidates[candidateCount] = distance;
    ++candidateCount;
  }

  if (candidateCount == 0) return false;
  uint8_t selected = (uint8_t)random((long)candidateCount);
  targetAngle = targetCandidates[selected];
  runwayDeg = runwayCandidates[selected];
  return true;
}

static void recordAlwaysCaptureDecision(bool steer, float predictedAngle,
                                        float targetAngle,
                                        const char* originalReason) {
  recordDecision(steer, predictedAngle, targetAngle,
                 steer ? "always-random" : originalReason);
}
