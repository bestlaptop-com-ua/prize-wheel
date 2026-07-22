// Prize Wheel firmware v3: powered always-capture random-safe experiment.
#include "firmware_v2_core.inc"

// Keep the original drag implementation available under legacy names while
// replacing the active controller with the powered encoder-distance version.
#define beginDrag beginLegacyDrag
#define launchDrag launchLegacyDrag
#define serviceDrag serviceLegacyDrag
#define stopDragIntoHold stopLegacyDragIntoHold
#include "firmware_v2_control.inc"
#undef stopDragIntoHold
#undef serviceDrag
#undef launchDrag
#undef beginDrag

// Keep the legacy implementation link-checked in strict host builds.
[[maybe_unused]] static auto legacyBeginDragRef = &beginLegacyDrag;
[[maybe_unused]] static auto legacyLaunchDragRef = &launchLegacyDrag;
[[maybe_unused]] static auto legacyServiceDragRef = &serviceLegacyDrag;
[[maybe_unused]] static auto legacyMinimumRunwayRef = &minimumRunwayDeg;

#include "firmware_v2_recovery_ui.inc"
// Powered capture may carry the wheel farther than the brake-only experiment.
// A 240-degree envelope guarantees at least one safe candidate at the 0.28
// rev/s decision speed without ever reversing.
#define DRAG_MAX_RUNWAY_DEG 240.0f
#include "firmware_always_capture_random.inc"
#undef DRAG_MAX_RUNWAY_DEG
#include "firmware_powered_capture.inc"

// Reuse the validated state machine and replace only its decision thresholds
// and target selector. Every valid spin is captured unless observe-only is on.
#define predictedStopCouldBeDare(angleDeg) (true)
#define chooseNearestSafeTarget chooseRandomReachableSafeTarget
#define minimumRunwayDeg poweredMinimumRunwayDeg
#define DECISION_REV_S POWERED_DECISION_REV_S
#define DECISION_MIN_PEAK_REV_S 0.0f
#define recordDecision recordAlwaysCaptureDecision
#define setup setupV2Base
#include "firmware_v2_runtime.inc"
#undef setup
#undef recordDecision
#undef DECISION_MIN_PEAK_REV_S
#undef DECISION_REV_S
#undef minimumRunwayDeg
#undef chooseNearestSafeTarget
#undef predictedStopCouldBeDare

void setup() {
  setupV2Base();
  randomSeed((uint32_t)micros() ^ (uint32_t)encoderCountsMT);
  Serial.println(F("# EXPERIMENT mode=POWERED_ALWAYS_CAPTURE_RANDOM target=random-reachable-safe; send o for motor-free observation"));
}
