/*
 * Powered always-capture controller.
 *
 * Unlike the electronic-drag experiment, this controller is allowed to drive
 * the wheel forward after takeover. Encoder position remains authoritative:
 * the motor starts near the measured wheel speed, follows a distance-based
 * forward velocity profile, and decelerates toward a random safe wedge center.
 * It never commands a reversal. If a target is materially crossed before the
 * stop request, the controller chooses another safe target farther ahead.
 */

static const float POWERED_DECISION_REV_S = 0.28f;
static const uint16_t POWERED_PRECHARGE_MS = 50;
static const uint16_t POWERED_ENTRY_CURRENT_MA = 350;
static const uint16_t POWERED_RUN_CURRENT_MA = 700;
static const uint16_t POWERED_CURRENT_RISE_MS = 160;
static const uint32_t POWERED_SLEW_SPS2 = 800;
static const float POWERED_ENTRY_FRACTION = 0.97f;
static const float POWERED_CRUISE_REV_S = 0.22f;
static const float POWERED_MAX_REV_S = 0.30f;
static const float POWERED_MIN_REV_S = 0.025f;
static const uint16_t POWERED_CONTROL_PERIOD_MS = 10;
static const uint16_t POWERED_ENTRY_BLEND_MS = 450;
static const float POWERED_MIN_RUNWAY_MARGIN_DEG = 18.0f;
static const float POWERED_STOP_LEAD_DEG = 3.5f;
static const float POWERED_RETARGET_CROSS_DEG = 4.0f;
static const float POWERED_RETARGET_MIN_DEG = 75.0f;
static const uint8_t POWERED_MAX_RETARGETS = 2;
static const uint16_t POWERED_STALL_WINDOW_MS = 400;
static const float POWERED_STALL_MIN_PROGRESS_DEG = 1.0f;
static const uint16_t POWERED_TIMEOUT_MS = 8000;

static uint32_t poweredProgressStartedMs = 0;
static int32_t poweredProgressStartCounts = 0;
static float poweredEntryRevS = 0.0f;
static uint8_t poweredRetargetCount = 0;

static float poweredMinimumRunwayDeg(float speedRevS) {
  float accelRevS2 = (float)POWERED_SLEW_SPS2 / WHEEL_USTEPS_PER_REV;
  float plannerStopDeg = speedRevS * speedRevS /
      (2.0f * accelRevS2) * 360.0f;
  return plannerStopDeg + POWERED_MIN_RUNWAY_MARGIN_DEG;
}

static bool startPoweredStepper(int encoderDirection, uint32_t startHz) {
  int fasSign = fasSignForEncoderDirection(encoderDirection);
  if (fasSign == 0 || !stepper || startHz == 0) return false;

  stepper->forceStopAndNewPosition(0);
  stepper->setAcceleration(POWERED_SLEW_SPS2);
  if (stepper->setSpeedInHz(startHz) != 0) return false;

  uint32_t jumpStep = (uint32_t)lroundf(
      ((float)startHz * (float)startHz) /
      (2.0f * (float)POWERED_SLEW_SPS2));
  stepper->setJumpStart(jumpStep);

  MoveResultCode result = fasSign > 0 ? stepper->runForward()
                                      : stepper->runBackward();
  return result == MOVE_OK;
}

static void beginDrag(int direction, float targetAngle, float runwayDeg) {
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

  driverActive(PRECHARGE_CURRENT_MA);
  mode = DRAG_PRECHARGE;

  Serial.printf("SPIN#%lu POWER_PRECHARGE dir=%+d targetAngle=%.1f targetWedge=%d runwayDeg=%.1f currentMa=%u\n",
                (unsigned long)spinRecord.number, direction, targetAngle,
                wedgeAtAngle(targetAngle), runwayDeg, PRECHARGE_CURRENT_MA);
}

static bool launchDrag() {
  float forwardSpeed = omega * dragDirection;
  if (forwardSpeed <= POWERED_MIN_REV_S) return false;

  poweredEntryRevS = fminf(forwardSpeed * POWERED_ENTRY_FRACTION,
                           POWERED_MAX_REV_S);
  if (poweredEntryRevS < POWERED_MIN_REV_S) {
    poweredEntryRevS = POWERED_MIN_REV_S;
  }
  uint32_t startHz = (uint32_t)lroundf(
      poweredEntryRevS * WHEEL_USTEPS_PER_REV);

  driverActive(POWERED_ENTRY_CURRENT_MA);
  if (!startPoweredStepper(dragDirection, startHz)) {
    driverFreewheel();
    return false;
  }

  dragCurrentHz = startHz;
  dragStartedMs = millis();
  dragLastControlMs = dragStartedMs;
  poweredProgressStartedMs = dragStartedMs;
  poweredProgressStartCounts = encoderCountsMT;
  mode = DRAG_ACTIVE;

  Serial.printf("SPIN#%lu POWER_START dir=%+d wheelRevS=%.3f commandRevS=%.3f hz=%lu targetAngle=%.1f targetWedge=%d\n",
                (unsigned long)spinRecord.number, dragDirection, forwardSpeed,
                poweredEntryRevS, (unsigned long)startHz, dragTargetAngle,
                wedgeAtAngle(dragTargetAngle));
  return true;
}

static void stopDragIntoHold(const char* reason) {
  if (stepper && stepper->isRunning() && !stepper->isStopping()) {
    stepper->stopMove();
  }
  dragStopRequestedMs = millis();
  mode = DRAG_STOPPING;
  Serial.printf("SPIN#%lu POWER_STOP reason=%s remainingDeg=%.1f wheelRevS=%.3f commandHz=%lu\n",
                (unsigned long)spinRecord.number, reason, dragRemainingDeg(),
                omega * dragDirection, (unsigned long)dragCurrentHz);
}

static bool retargetPoweredCapture() {
  if (poweredRetargetCount >= POWERED_MAX_RETARGETS) return false;

  float newTarget = 0.0f;
  float newRunway = 0.0f;
  if (!chooseRandomReachableSafeTarget(dragDirection, wheelAngleDeg(),
                                       POWERED_RETARGET_MIN_DEG,
                                       newTarget, newRunway)) {
    return false;
  }

  ++poweredRetargetCount;
  dragTargetAngle = newTarget;
  dragStartCounts = encoderCountsMT;
  dragTargetTravelCounts = countsForDegrees(newRunway);
  spinRecord.targetAngle = newTarget;
  spinRecord.targetWedge = wedgeAtAngle(newTarget);
  poweredProgressStartedMs = millis();
  poweredProgressStartCounts = encoderCountsMT;

  Serial.printf("SPIN#%lu POWER_RETARGET count=%u targetAngle=%.1f targetWedge=%d runwayDeg=%.1f\n",
                (unsigned long)spinRecord.number, poweredRetargetCount,
                newTarget, spinRecord.targetWedge, newRunway);
  return true;
}

static void enterPoweredFault(const char* code) {
  if (stepper) stepper->forceStopAndNewPosition(0);
  driverActive(HOLD_CURRENT_MA);
  mode = FAULT_HOLD;
  Serial.printf("SPIN#%lu FAULT code=%s remainingDeg=%.1f wheelRevS=%.3f commandHz=%lu\n",
                (unsigned long)spinRecord.number, code, dragRemainingDeg(),
                omega * dragDirection, (unsigned long)dragCurrentHz);
}

static void serviceDrag() {
  if (!stepper) {
    driverFreewheel();
    mode = FREE_SPIN;
    return;
  }

  if (!encoderPositionFresh()) {
    enterPoweredFault("ENCODER_STALE_DURING_POWERED_CAPTURE");
    return;
  }
  if (!encoderMotionReady()) return;

  float forwardSpeed = omega * dragDirection;
  if (forwardSpeed < -OPPOSITE_ABORT_REV_S) {
    if (dragOppositeStartedMs == 0) dragOppositeStartedMs = millis();
    if (millis() - dragOppositeStartedMs >= OPPOSITE_ABORT_MS) {
      enterPoweredFault("OPPOSITE_MOTION_DURING_POWERED_CAPTURE");
      return;
    }
  } else {
    dragOppositeStartedMs = 0;
  }

  float remainingDeg = dragRemainingDeg();
  if (remainingDeg < -POWERED_RETARGET_CROSS_DEG) {
    if (!retargetPoweredCapture()) {
      stopDragIntoHold("target-crossed-no-retarget");
    }
    return;
  }

  if (remainingDeg <= POWERED_STOP_LEAD_DEG) {
    stopDragIntoHold("target-approach");
    return;
  }

  if (millis() - dragStartedMs > POWERED_TIMEOUT_MS) {
    enterPoweredFault("POWERED_CAPTURE_TIMEOUT");
    return;
  }

  if (!dragCurrentRaised &&
      millis() - dragStartedMs >= POWERED_CURRENT_RISE_MS) {
    driver.rms_current(POWERED_RUN_CURRENT_MA, 1.0);
    dragCurrentRaised = true;
  }

  uint32_t nowMs = millis();
  if (nowMs - poweredProgressStartedMs >= POWERED_STALL_WINDOW_MS) {
    int32_t progressCounts = dragDirection *
        (encoderCountsMT - poweredProgressStartCounts);
    float progressDeg = progressCounts * 360.0f / 4096.0f;
    if (progressDeg < POWERED_STALL_MIN_PROGRESS_DEG &&
        dragCurrentHz > (uint32_t)lroundf(0.05f * WHEEL_USTEPS_PER_REV)) {
      enterPoweredFault("POWERED_CAPTURE_STALL");
      return;
    }
    poweredProgressStartedMs = nowMs;
    poweredProgressStartCounts = encoderCountsMT;
  }

  if (nowMs - dragLastControlMs < POWERED_CONTROL_PERIOD_MS) return;
  dragLastControlMs = nowMs;

  float accelRevS2 = (float)POWERED_SLEW_SPS2 / WHEEL_USTEPS_PER_REV;
  float profileDistanceDeg = fmaxf(0.0f,
      remainingDeg - POWERED_STOP_LEAD_DEG);
  float brakingLimitRevS = sqrtf(
      2.0f * accelRevS2 * (profileDistanceDeg / 360.0f));

  float elapsedBlend = fminf(1.0f,
      (float)(nowMs - dragStartedMs) / (float)POWERED_ENTRY_BLEND_MS);
  float entryBlendLimit = poweredEntryRevS + elapsedBlend *
      (POWERED_CRUISE_REV_S - poweredEntryRevS);
  float commandRevS = fminf(brakingLimitRevS,
                            fmaxf(POWERED_MIN_REV_S, entryBlendLimit));
  commandRevS = fminf(commandRevS, POWERED_CRUISE_REV_S);
  commandRevS = fminf(commandRevS, POWERED_MAX_REV_S);

  if (commandRevS < POWERED_MIN_REV_S) {
    stopDragIntoHold("profile-complete");
    return;
  }

  uint32_t requestedHz = (uint32_t)lroundf(
      commandRevS * WHEEL_USTEPS_PER_REV);
  if (requestedHz < 1) requestedHz = 1;
  if (requestedHz != dragCurrentHz &&
      stepper->setSpeedInHz(requestedHz) == 0) {
    stepper->applySpeedAcceleration();
    dragCurrentHz = requestedHz;
  }

  if (verboseControl) {
    static uint32_t lastPrintMs = 0;
    if (nowMs - lastPrintMs >= 150) {
      lastPrintMs = nowMs;
      Serial.printf("# POWER angle=%.1f target=%.1f remaining=%.1f omega=%.3f commandRevS=%.3f hz=%lu currentMa=%u\n",
                    wheelAngleDeg(), dragTargetAngle, remainingDeg, omega,
                    commandRevS, (unsigned long)dragCurrentHz,
                    dragCurrentRaised ? POWERED_RUN_CURRENT_MA
                                      : POWERED_ENTRY_CURRENT_MA);
    }
  }
}
