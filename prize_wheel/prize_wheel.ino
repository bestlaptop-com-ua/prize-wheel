// Prize Wheel firmware v2: always-capture random-safe experiment.
#include "firmware_v2_core.inc"
#include "firmware_v2_control.inc"
#include "firmware_v2_recovery_ui.inc"
#include "firmware_always_capture_random.inc"

// Reuse the validated v2 state machine while replacing only its decision layer:
// every valid spin is considered unsafe, the target selector is randomized, and
// even weak confirmed spins are eligible for capture.
#define predictedStopCouldBeDare(angleDeg) (true)
#define chooseNearestSafeTarget chooseRandomReachableSafeTarget
#define DECISION_MIN_PEAK_REV_S 0.0f
#define recordDecision recordAlwaysCaptureDecision
#define setup setupV2Base
#include "firmware_v2_runtime.inc"
#undef setup
#undef recordDecision
#undef DECISION_MIN_PEAK_REV_S
#undef chooseNearestSafeTarget
#undef predictedStopCouldBeDare

void setup() {
  setupV2Base();
  randomSeed((uint32_t)micros() ^ (uint32_t)encoderCountsMT);
  Serial.println(F("# EXPERIMENT mode=ALWAYS_CAPTURE_RANDOM target=random-reachable-safe; send o for motor-free observation"));
}
