// Prize Wheel firmware v4: powered always-capture random-safe experiment.
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

// Keep the previous one-way probe and serial UI link-checked under legacy
// names. V4 supplies a two-way continuous-run probe and updated serial UI.
#define startDirectionProbe startDirectionProbeV3
#define serviceDirectionProbe serviceDirectionProbeV3
#define printStatus printStatusV3
#define printHelp printHelpV3
#define handleSerial handleSerialV3
#include "firmware_v2_recovery_ui.inc"
#undef handleSerial
#undef printHelp
#undef printStatus
#undef serviceDirectionProbe
#undef startDirectionProbe
[[maybe_unused]] static auto legacyProbeServiceRef = &serviceDirectionProbeV3;
[[maybe_unused]] static auto legacyHandleSerialRef = &handleSerialV3;
#include "firmware_v4_bidir_ui.inc"
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
#define DECISION_MIN_PEAK_REV_S decisionMinimumPeakRequirement()
#define PRECHARGE_MS POWERED_PRECHARGE_MS
#define recordDecision recordAlwaysCaptureDecision
#define setup setupV2Base
#include "firmware_v2_runtime.inc"
#undef setup
#undef recordDecision
#undef PRECHARGE_MS
#undef DECISION_MIN_PEAK_REV_S
#undef DECISION_REV_S
#undef minimumRunwayDeg
#undef chooseNearestSafeTarget
#undef predictedStopCouldBeDare

void setup() {
  setupV2Base();
  bool bidirOk = preferences.getBool("bidir_ok", false);
  if (!bidirOk) {
    motorDirectionCalibrated = false;
    motorPositiveEncoderSign = 0;
    Serial.println(F("# V4 CONTROL LOCKED: run bidirectional p probe before powered spins"));
  }
  randomSeed((uint32_t)micros() ^ (uint32_t)encoderCountsMT);
  Serial.println(F("# EXPERIMENT mode=POWERED_RANDOM_V4 bidirectional-probe+falling-gate direction-profile; run p before powered spins"));
}
