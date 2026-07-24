// Prize Wheel firmware v5: hybrid moving / CW stop-and-drive capture.
#include "firmware_v2_core.inc"

// Keep the original v2 drag implementation available under legacy names.
#define beginDrag beginLegacyDrag
#define launchDrag launchLegacyDrag
#define serviceDrag serviceLegacyDrag
#define stopDragIntoHold stopLegacyDragIntoHold
#include "firmware_v2_control.inc"
#undef stopDragIntoHold
#undef serviceDrag
#undef launchDrag
#undef beginDrag

[[maybe_unused]] static auto legacyBeginDragRef = &beginLegacyDrag;
[[maybe_unused]] static auto legacyLaunchDragRef = &launchLegacyDrag;
[[maybe_unused]] static auto legacyServiceDragRef = &serviceLegacyDrag;
[[maybe_unused]] static auto legacyMinimumRunwayRef = &minimumRunwayDeg;

// Replace v2 direction-probe/UI functions with the bidirectional versions.
#define startDirectionProbe startDirectionProbeV2
#define serviceDirectionProbe serviceDirectionProbeV2
#define printStatus printStatusV2
#define printHelp printHelpV2
#define handleSerial handleSerialV2
#include "firmware_v2_recovery_ui.inc"
#undef handleSerial
#undef printHelp
#undef printStatus
#undef serviceDirectionProbe
#undef startDirectionProbe

[[maybe_unused]] static auto legacyStartDirectionProbeRef = &startDirectionProbeV2;
[[maybe_unused]] static auto legacyServiceDirectionProbeRef = &serviceDirectionProbeV2;
[[maybe_unused]] static auto legacyPrintStatusRef = &printStatusV2;
[[maybe_unused]] static auto legacyPrintHelpRef = &printHelpV2;
[[maybe_unused]] static auto legacyHandleSerialRef = &handleSerialV2;

#include "firmware_v4_bidir_ui.inc"

#define DRAG_MAX_RUNWAY_DEG 240.0f
#include "firmware_always_capture_random.inc"
#undef DRAG_MAX_RUNWAY_DEG

// Keep the v4 moving controller for direction -1, and overlay direction +1
// with the near-stop finite-position strategy.
#define poweredMinimumRunwayDeg poweredMinimumRunwayDegV4
#define beginDrag beginDragV4
#define launchDrag launchDragV4
#define serviceDrag serviceDragV4
#define stopDragIntoHold stopDragIntoHoldV4
#include "firmware_powered_capture.inc"
#undef stopDragIntoHold
#undef serviceDrag
#undef launchDrag
#undef beginDrag
#undef poweredMinimumRunwayDeg

#include "firmware_cw_stop_drive_v5.inc"

#define predictedStopCouldBeDare(angleDeg) (true)
#define chooseNearestSafeTarget chooseRandomReachableSafeTarget
#define minimumRunwayDeg poweredMinimumRunwayDeg
#define DECISION_REV_S decisionMaximumSpeed()
#define DECISION_MIN_REV_S decisionMinimumSpeed()
#define DECISION_MIN_PEAK_REV_S decisionMinimumPeakRequirement()
#define PRECHARGE_MS POWERED_PRECHARGE_MS
#define recordDecision recordAlwaysCaptureDecision
#define setup setupV2Base
#include "firmware_v2_runtime.inc"
#undef setup
#undef recordDecision
#undef PRECHARGE_MS
#undef DECISION_MIN_PEAK_REV_S
#undef DECISION_MIN_REV_S
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
    Serial.println(F("# V5 CONTROL LOCKED: run bidirectional p probe before powered spins"));
  }
  randomSeed((uint32_t)micros() ^ (uint32_t)encoderCountsMT);
  Serial.println(F("# EXPERIMENT mode=POWERED_RANDOM_V5 ccw=moving cw=stop-and-drive; run p before powered spins"));
}
