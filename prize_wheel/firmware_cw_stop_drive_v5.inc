/* CW/+1 stop-and-drive overlay for powered firmware v5. */
static const uint16_t CW_V5_CAPTURE_CURRENT_MA = 450;
static const uint16_t CW_V5_CAPTURE_STILL_CONFIRM_MS = 120;
static const uint16_t CW_V5_CAPTURE_TIMEOUT_MS = 1600;
static const float CW_V5_CAPTURE_STILL_REV_S = 0.018f;
static const float CW_V5_CAPTURE_REVERSE_ABORT_REV_S = 0.040f;
static const uint16_t CW_V5_CAPTURE_REVERSE_ABORT_MS = 100;
static const float CW_V5_MIN_RUNWAY_DEG = 90.0f;
static const float CW_V5_KEEP_TARGET_MIN_RUNWAY_DEG = 75.0f;
static const float CW_V5_MAX_RUNWAY_DEG = 240.0f;
static const uint16_t CW_V5_MOVE_CURRENT_MA = 950;
static const uint32_t CW_V5_MOVE_SPEED_HZ = 640;
static const uint32_t CW_V5_MOVE_ACCEL_SPS2 = 400;
static const float CW_V5_STOP_LEAD_DEG = 2.5f;
static const float CW_V5_OPPOSITE_TOLERANCE_DEG = 1.5f;
static const float CW_V5_OVERSHOOT_ABORT_DEG = 5.0f;
static const uint16_t CW_V5_STALL_WINDOW_MS = 500;
static const float CW_V5_STALL_MIN_PROGRESS_DEG = 1.0f;
static const uint16_t CW_V5_MOVE_TIMEOUT_MS = 9000;

enum CwV5Phase : uint8_t {
  CW_V5_IDLE,
  CW_V5_CAPTURE_WAIT,
  CW_V5_POSITION_MOVE
};

static CwV5Phase cwV5Phase = CW_V5_IDLE;
static uint32_t cwV5CaptureStartedMs = 0;
static uint32_t cwV5StillStartedMs = 0;

static float poweredMinimumRunwayDeg(float speedRevS) {
  if (spinRecord.direction > 0) return CW_V5_MIN_RUNWAY_DEG;
  return poweredMinimumRunwayDegV4(speedRevS);
}

static void cwV5Fault(const char* code) {
  if (stepper) stepper->forceStopAndNewPosition(0);
  driverActive(HOLD_CURRENT_MA);
  mode = FAULT_HOLD;
  Serial.printf("SPIN#%lu FAULT code=%s remainingDeg=%.1f wheelRevS=%.3f commandHz=%lu\n",
                (unsigned long)spinRecord.number, code, dragRemainingDeg(),
                omega * dragDirection, (unsigned long)dragCurrentHz);
}

static void beginDrag(int direction, float targetAngle, float runwayDeg) {
  if (direction < 0) {
    cwV5Phase = CW_V5_IDLE;
    beginDragV4(direction, targetAngle, runwayDeg);
    return;
  }

  dragDirection = direction;
  dragTargetAngle = targetAngle;
  dragStartCounts = encoderCountsMT;
  dragTargetTravelCounts = countsForDegrees(runwayDeg);
  dragPrechargeStartedMs = millis();
  dragStartedMs = 0;
  dragLastControlMs = 0;
  dragCurrentHz = 0;
  dragStopRequestedMs = 0;
  dragOppositeStartedMs = 0;
  dragSpeedupStartedMs = 0;
  dragCurrentRaised = false;
  poweredProgressStartedMs = 0;
  poweredProgressStartCounts = encoderCountsMT;
  poweredEntryRevS = 0.0f;
  poweredRetargetCount = 0;
  poweredFasSign = 0;
  cwV5StillStartedMs = 0;
  cwV5CaptureStartedMs = millis();
  cwV5Phase = CW_V5_CAPTURE_WAIT;

  driverActive(CW_V5_CAPTURE_CURRENT_MA);
  mode = DRAG_PRECHARGE;
  Serial.printf("SPIN#%lu CW_CAPTURE_ARM speed=%.3f targetAngle=%.1f targetWedge=%d initialRunwayDeg=%.1f currentMa=%u\n",
                (unsigned long)spinRecord.number, omega, targetAngle,
                wedgeAtAngle(targetAngle), runwayDeg,
                CW_V5_CAPTURE_CURRENT_MA);
}

static bool cwV5ChooseTarget(float& targetAngle, float& runwayDeg) {
  runwayDeg = forwardDistanceDeg(1, wheelAngleDeg(), dragTargetAngle);
  if (runwayDeg >= CW_V5_KEEP_TARGET_MIN_RUNWAY_DEG &&
      runwayDeg <= CW_V5_MAX_RUNWAY_DEG &&
      !isDare(wedgeAtAngle(dragTargetAngle))) {
    targetAngle = dragTargetAngle;
    return true;
  }

  return chooseRandomReachableSafeTarget(
      1, wheelAngleDeg(), CW_V5_MIN_RUNWAY_DEG,
      targetAngle, runwayDeg);
}

static bool cwV5StartPositionMove() {
  float targetAngle = 0.0f;
  float runwayDeg = 0.0f;
  if (!cwV5ChooseTarget(targetAngle, runwayDeg)) {
    cwV5Fault("CW_POSITION_NO_SAFE_TARGET");
    return true;
  }

  int fasSign = fasSignForEncoderDirection(1);
  int32_t moveUsteps = ustepsForDegrees(runwayDeg);
  if (fasSign == 0 || !stepper || moveUsteps <= 0) {
    cwV5Fault("CW_POSITION_INVALID_MOVE");
    return true;
  }

  dragTargetAngle = targetAngle;
  dragStartCounts = encoderCountsMT;
  dragTargetTravelCounts = countsForDegrees(runwayDeg);
  spinRecord.targetAngle = targetAngle;
  spinRecord.targetWedge = wedgeAtAngle(targetAngle);

  stepper->forceStopAndNewPosition(0);
  driverActive(CW_V5_MOVE_CURRENT_MA);
  stepper->setSpeedInHz(CW_V5_MOVE_SPEED_HZ);
  stepper->setAcceleration(CW_V5_MOVE_ACCEL_SPS2);
  stepper->setJumpStart(0);
  MoveResultCode result = stepper->move(fasSign * moveUsteps);
  if (result != MOVE_OK) {
    cwV5Fault("CW_POSITION_QUEUE_FAILED");
    return true;
  }

  poweredFasSign = fasSign;
  dragCurrentHz = CW_V5_MOVE_SPEED_HZ;
  dragStartedMs = millis();
  dragLastControlMs = dragStartedMs;
  poweredProgressStartedMs = dragStartedMs;
  poweredProgressStartCounts = encoderCountsMT;
  dragCurrentRaised = true;
  cwV5Phase = CW_V5_POSITION_MOVE;
  mode = DRAG_ACTIVE;

  Serial.printf("SPIN#%lu CW_POSITION_START fasDir=%+d fromAngle=%.1f targetAngle=%.1f targetWedge=%d runwayDeg=%.1f speedHz=%lu currentMa=%u\n",
                (unsigned long)spinRecord.number, fasSign, wheelAngleDeg(),
                targetAngle, spinRecord.targetWedge, runwayDeg,
                (unsigned long)CW_V5_MOVE_SPEED_HZ, CW_V5_MOVE_CURRENT_MA);
  return true;
}

static bool cwV5ServiceCaptureWait() {
  if (!encoderPositionFresh()) {
    cwV5Fault("CW_CAPTURE_ENCODER_STALE");
    return true;
  }
  if (!encoderMotionReady()) return true;

  uint32_t nowMs = millis();
  if (omega < -CW_V5_CAPTURE_REVERSE_ABORT_REV_S) {
    if (dragOppositeStartedMs == 0) dragOppositeStartedMs = nowMs;
    if (nowMs - dragOppositeStartedMs >= CW_V5_CAPTURE_REVERSE_ABORT_MS) {
      cwV5Fault("CW_CAPTURE_ROLLBACK");
      return true;
    }
  } else {
    dragOppositeStartedMs = 0;
  }

  if (fabsf(omega) <= CW_V5_CAPTURE_STILL_REV_S) {
    if (cwV5StillStartedMs == 0) cwV5StillStartedMs = nowMs;
    if (nowMs - cwV5StillStartedMs >= CW_V5_CAPTURE_STILL_CONFIRM_MS) {
      Serial.printf("SPIN#%lu CW_CAPTURE_SETTLED angle=%.1f wedge=%d omega=%.4f settleMs=%lu\n",
                    (unsigned long)spinRecord.number, wheelAngleDeg(),
                    currentWedge(), omega,
                    (unsigned long)(nowMs - cwV5StillStartedMs));
      return cwV5StartPositionMove();
    }
  } else {
    cwV5StillStartedMs = 0;
  }

  if (nowMs - cwV5CaptureStartedMs > CW_V5_CAPTURE_TIMEOUT_MS) {
    cwV5Fault("CW_CAPTURE_SETTLE_TIMEOUT");
    return true;
  }

  if (verboseControl && nowMs - dragLastControlMs >= 150) {
    dragLastControlMs = nowMs;
    Serial.printf("# CW_CAPTURE angle=%.1f omega=%.4f stillMs=%lu currentMa=%u\n",
                  wheelAngleDeg(), omega,
                  cwV5StillStartedMs == 0 ? 0UL :
                      (unsigned long)(nowMs - cwV5StillStartedMs),
                  CW_V5_CAPTURE_CURRENT_MA);
  }
  return true;
}

static bool launchDrag() {
  if (cwV5Phase == CW_V5_CAPTURE_WAIT) return cwV5ServiceCaptureWait();
  return launchDragV4();
}

static void cwV5ServicePositionMove() {
  if (!stepper || !encoderPositionFresh()) {
    cwV5Fault("CW_POSITION_SENSOR_OR_STEPPER");
    return;
  }
  if (!encoderMotionReady()) return;

  uint32_t nowMs = millis();
  int32_t travelledCounts = encoderCountsMT - dragStartCounts;
  float travelledDeg = travelledCounts * 360.0f / 4096.0f;
  float remainingDeg = dragRemainingDeg();

  if (travelledDeg < -CW_V5_OPPOSITE_TOLERANCE_DEG) {
    cwV5Fault("CW_POSITION_OPPOSITE_MOTION");
    return;
  }
  if (remainingDeg < -CW_V5_OVERSHOOT_ABORT_DEG) {
    cwV5Fault("CW_POSITION_OVERSHOOT");
    return;
  }
  if (nowMs - dragStartedMs > CW_V5_MOVE_TIMEOUT_MS) {
    cwV5Fault("CW_POSITION_TIMEOUT");
    return;
  }

  if (nowMs - poweredProgressStartedMs >= CW_V5_STALL_WINDOW_MS) {
    int32_t progressCounts = encoderCountsMT - poweredProgressStartCounts;
    float progressDeg = progressCounts * 360.0f / 4096.0f;
    if (progressDeg < CW_V5_STALL_MIN_PROGRESS_DEG && stepper->isRunning()) {
      cwV5Fault("CW_POSITION_STALL");
      return;
    }
    poweredProgressStartedMs = nowMs;
    poweredProgressStartCounts = encoderCountsMT;
  }

  if (remainingDeg <= CW_V5_STOP_LEAD_DEG && stepper->isRunning()) {
    stepper->stopMove();
    dragStopRequestedMs = nowMs;
    mode = DRAG_STOPPING;
    Serial.printf("SPIN#%lu CW_POSITION_STOP reason=target-approach remainingDeg=%.1f travelledDeg=%.1f wheelRevS=%.3f\n",
                  (unsigned long)spinRecord.number, remainingDeg,
                  travelledDeg, omega);
    return;
  }

  if (!stepper->isRunning()) {
    dragStopRequestedMs = nowMs;
    mode = DRAG_STOPPING;
    Serial.printf("SPIN#%lu CW_POSITION_STOP reason=planner-complete remainingDeg=%.1f travelledDeg=%.1f wheelRevS=%.3f\n",
                  (unsigned long)spinRecord.number, remainingDeg,
                  travelledDeg, omega);
    return;
  }

  if (verboseControl && nowMs - dragLastControlMs >= 150) {
    dragLastControlMs = nowMs;
    Serial.printf("# CW_POSITION angle=%.1f target=%.1f remaining=%.1f travelled=%.1f omega=%.3f hz=%lu\n",
                  wheelAngleDeg(), dragTargetAngle, remainingDeg,
                  travelledDeg, omega, (unsigned long)dragCurrentHz);
  }
}

static void serviceDrag() {
  if (cwV5Phase == CW_V5_POSITION_MOVE) {
    cwV5ServicePositionMove();
    return;
  }
  serviceDragV4();
}
