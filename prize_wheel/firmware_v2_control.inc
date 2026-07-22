/* --------------------------- Driver helpers ----------------------------- */
bool tmcHealthy = false;

static void driverConfig() {
  driver.begin();
  driver.I_scale_analog(false);
  driver.toff(4);
  driver.blank_time(24);
  driver.microsteps(16);
  driver.en_spreadCycle(true);
  driver.pwm_autoscale(true);
  driver.rms_current(RMS_CURRENT_MA, 1.0);
  driver.TCOOLTHRS(0);
  driver.semin(0);
  driver.semax(0);
  driver.iholddelay(0);
  driver.TPOWERDOWN(255);
}

static void driverActive(uint16_t currentMa) {
  driver.freewheel(0);
  driver.rms_current(currentMa, 1.0);
  if (stepper) stepper->enableOutputs();
  else digitalWrite(PIN_EN, LOW);
}

static void driverFreewheel() {
  if (stepper) {
    stepper->forceStopAndNewPosition(0);
    stepper->setJumpStart(0);
    stepper->disableOutputs();
  } else {
    digitalWrite(PIN_EN, HIGH);
  }
  driver.freewheel(1);
  driver.ihold(0);
}

/* ------------------------------ Prediction ------------------------------ */
static float predictStopAngle() {
  int direction = omega >= 0.0f ? 1 : -1;
  float speed = fabsf(omega);
  if (speed < 0.001f) return wheelAngleDeg();

  float c = direction > 0 ? cw_c : ccw_c;
  float b = direction > 0 ? cw_b : ccw_b;
  float omegaRad = speed * TWO_PI;
  float travelRad = (1.0f / b) * omegaRad -
      (c / (b * b)) * logf(1.0f + b * omegaRad / c);
  return positiveFmod(wheelAngleDeg() + direction * travelRad * RAD_TO_DEG,
                      360.0f);
}

static bool predictedStopCouldBeDare(float angleDeg) {
  int wedge = wedgeAtAngle(angleDeg);
  if (isDare(wedge)) return true;

  float within = positiveFmod(angleDeg, WEDGE_DEG);
  if (within < PREDICTION_DARE_MARGIN_DEG &&
      isDare(wedge - 1)) return true;
  if (within > WEDGE_DEG - PREDICTION_DARE_MARGIN_DEG &&
      isDare(wedge + 1)) return true;
  return false;
}

static float minimumRunwayDeg(float speedRevS) {
  float accelRevS2 = (float)DRAG_SLEW_SPS2 / WHEEL_USTEPS_PER_REV;
  float plannerStopDeg = speedRevS * speedRevS / (2.0f * accelRevS2) * 360.0f;
  return plannerStopDeg + DRAG_MIN_RUNWAY_MARGIN_DEG;
}

static bool chooseNearestSafeTarget(int direction, float currentAngle,
                                    float minRunwayDeg,
                                    float& targetAngle,
                                    float& runwayDeg) {
  bool found = false;
  float bestDistance = 1000000.0f;
  float bestTarget = 0.0f;

  for (int wedge = 0; wedge < NUM_WEDGES; ++wedge) {
    if (isDare(wedge)) continue;

    // Center is 15 degrees from either edge and therefore respects the
    // configured edge margin without randomizing intervention length.
    float target = (wedge + 0.5f) * WEDGE_DEG;
    float distance = forwardDistanceDeg(direction, currentAngle, target);
    if (distance < minRunwayDeg || distance > DRAG_MAX_RUNWAY_DEG) continue;
    if (distance < bestDistance) {
      found = true;
      bestDistance = distance;
      bestTarget = target;
    }
  }

  if (!found) return false;
  targetAngle = bestTarget;
  runwayDeg = bestDistance;
  return true;
}

/* --------------------------- Spin event record -------------------------- */
static const float SNAPSHOT_SPEEDS[] = {0.35f, 0.30f, 0.26f, 0.20f, 0.15f, 0.10f};
static const uint8_t SNAPSHOT_COUNT = sizeof(SNAPSHOT_SPEEDS) / sizeof(SNAPSHOT_SPEEDS[0]);

struct SpinRecord {
  uint32_t number;
  int direction;
  uint32_t startedMs;
  float peakOmega;
  float previousSpeed;
  bool snapshotLogged[SNAPSHOT_COUNT];
  bool decisionMade;
  float decisionOmega;
  float predictedAngle;
  int predictedWedge;
  bool predictedUnsafe;
  bool dragAttempted;
  bool dragCompleted;
  int targetWedge;
  float targetAngle;
  float targetErrorDeg;
  bool primaryStopRecorded;
  int primaryStopWedge;
  float primaryStopAngle;
  bool recoveryUsed;
  uint8_t recoveryAttempts;
};

uint32_t spinCounter = 0;
SpinRecord spinRecord;

static void resetSpinRecord(int direction) {
  memset(&spinRecord, 0, sizeof(spinRecord));
  spinRecord.number = ++spinCounter;
  spinRecord.direction = direction;
  spinRecord.startedMs = millis();
  spinRecord.peakOmega = fabsf(omega);
  spinRecord.previousSpeed = fabsf(omega);
  spinRecord.predictedWedge = -1;
  spinRecord.targetWedge = -1;
  spinRecord.primaryStopWedge = -1;
  Serial.printf("SPIN#%lu START dir=%+d omegaAtConfirm=%.3f\n",
                (unsigned long)spinRecord.number, direction, fabsf(omega));
}

static void updateSpinSnapshots() {
  float speed = fabsf(omega);
  if (speed > spinRecord.peakOmega) spinRecord.peakOmega = speed;

  for (uint8_t i = 0; i < SNAPSHOT_COUNT; ++i) {
    float threshold = SNAPSHOT_SPEEDS[i];
    if (!spinRecord.snapshotLogged[i] &&
        spinRecord.previousSpeed > threshold && speed <= threshold) {
      float predicted = predictStopAngle();
      Serial.printf("SPIN#%lu PRED speed=%.3f angle=%.1f curWedge=%d predAngle=%.1f predWedge=%d\n",
                    (unsigned long)spinRecord.number, speed, wheelAngleDeg(),
                    currentWedge(), predicted, wedgeAtAngle(predicted));
      spinRecord.snapshotLogged[i] = true;
    }
  }
  spinRecord.previousSpeed = speed;
}

static void recordDecision(bool steer, float predictedAngle,
                           float targetAngle, const char* reason) {
  spinRecord.decisionMade = true;
  spinRecord.decisionOmega = omega;
  spinRecord.predictedAngle = predictedAngle;
  spinRecord.predictedWedge = wedgeAtAngle(predictedAngle);
  spinRecord.predictedUnsafe = predictedStopCouldBeDare(predictedAngle);
  spinRecord.dragAttempted = steer;
  spinRecord.targetAngle = targetAngle;
  spinRecord.targetWedge = steer ? wedgeAtAngle(targetAngle) : -1;

  if (steer) {
    Serial.printf("SPIN#%lu DECISION omega=%.3f peak=%.3f curWedge=%d predStopAngle=%.1f predStopWedge=%d action=STEER targetWedge=%d reason=%s\n",
                  (unsigned long)spinRecord.number, omega, spinRecord.peakOmega,
                  currentWedge(), predictedAngle, spinRecord.predictedWedge,
                  spinRecord.targetWedge, reason);
  } else {
    Serial.printf("SPIN#%lu DECISION omega=%.3f peak=%.3f curWedge=%d predStopAngle=%.1f predStopWedge=%d action=LEAVE reason=%s\n",
                  (unsigned long)spinRecord.number, omega, spinRecord.peakOmega,
                  currentWedge(), predictedAngle, spinRecord.predictedWedge,
                  reason);
  }
}

static void recordPrimaryStop() {
  if (spinRecord.primaryStopRecorded) return;
  spinRecord.primaryStopRecorded = true;
  spinRecord.primaryStopAngle = wheelAngleDeg();
  spinRecord.primaryStopWedge = currentWedge();
  Serial.printf("SPIN#%lu PRIMARY_STOP source=%s wedge=%d angle=%.1f isDare=%d\n",
                (unsigned long)spinRecord.number,
                spinRecord.dragAttempted ? "DRAG" : "NATURAL",
                spinRecord.primaryStopWedge, spinRecord.primaryStopAngle,
                isDare(spinRecord.primaryStopWedge) ? 1 : 0);
}

static void printFinalLanding() {
  Serial.printf("SPIN#%lu LANDED wedge=%d angle=%.1f isDare=%d peak=%.3f predictedWedge=%d predictedAngle=%.1f drag=%d recovery=%d primaryWedge=%d targetWedge=%d targetErrorDeg=%.1f\n",
                (unsigned long)spinRecord.number, currentWedge(), wheelAngleDeg(),
                isDare(currentWedge()) ? 1 : 0, spinRecord.peakOmega,
                spinRecord.predictedWedge, spinRecord.predictedAngle,
                spinRecord.dragAttempted ? 1 : 0,
                spinRecord.recoveryUsed ? 1 : 0,
                spinRecord.primaryStopWedge, spinRecord.targetWedge,
                spinRecord.targetErrorDeg);
}

/* ------------------------------ State ----------------------------------- */
enum Mode : uint8_t {
  IDLE,
  FREE_SPIN,
  DRAG_PRECHARGE,
  DRAG_ACTIVE,
  DRAG_STOPPING,
  SETTLE,
  RECOVERY_PRECHARGE,
  RECOVERY_ACTIVE,
  RECOVERY_SETTLE,
  SAFE_HOLD,
  DIR_PROBE,
  FAULT_HOLD
};

Mode mode = IDLE;

static uint8_t currentStateByte() {
  return (uint8_t)mode;
}

bool observeOnly = false;
bool emergencyRecoveryEnabled = EMERGENCY_RECOVERY_DEFAULT;
bool verboseControl = false;

bool motorDirectionCalibrated = false;
int motorPositiveEncoderSign = 0;

uint32_t spinCandidateStartedMs = 0;
int32_t spinCandidateStartCounts = 0;
int spinCandidateDirection = 1;
uint32_t guestOverrideStartedMs = 0;
uint32_t settleStartedMs = 0;
uint32_t safeHoldStartedMs = 0;

/* -------------------------- Drag controller ----------------------------- */
int dragDirection = 1;
float dragTargetAngle = 0.0f;
int32_t dragStartCounts = 0;
int32_t dragTargetTravelCounts = 0;
uint32_t dragPrechargeStartedMs = 0;
uint32_t dragStartedMs = 0;
uint32_t dragLastControlMs = 0;
uint32_t dragCurrentHz = 0;
uint32_t dragStopRequestedMs = 0;
float dragLowestForwardSpeed = 0.0f;
uint32_t dragOppositeStartedMs = 0;
uint32_t dragSpeedupStartedMs = 0;
bool dragCurrentRaised = false;

static int fasSignForEncoderDirection(int encoderDirection) {
  if (!motorDirectionCalibrated || motorPositiveEncoderSign == 0) return 0;
  return encoderDirection == motorPositiveEncoderSign ? 1 : -1;
}

static bool automaticControlReady() {
  return tmcHealthy && zeroCalibrated && motorDirectionCalibrated &&
         stepper != nullptr && encoderMotionReady();
}

static int32_t dragTravelledCounts() {
  return dragDirection * (encoderCountsMT - dragStartCounts);
}

static float dragRemainingDeg() {
  int32_t remaining = dragTargetTravelCounts - dragTravelledCounts();
  return remaining * 360.0f / 4096.0f;
}

static bool startContinuousStepper(int encoderDirection, uint32_t startHz) {
  int fasSign = fasSignForEncoderDirection(encoderDirection);
  if (fasSign == 0 || !stepper || startHz == 0) return false;

  stepper->forceStopAndNewPosition(0);
  stepper->setAcceleration(DRAG_SLEW_SPS2);
  if (stepper->setSpeedInHz(startHz) != 0) return false;
  uint32_t jumpStep = (uint32_t)lroundf(
      ((float)startHz * (float)startHz) / (2.0f * DRAG_SLEW_SPS2));
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
  dragLowestForwardSpeed = fmaxf(0.0f, omega * direction);
  dragOppositeStartedMs = 0;
  dragSpeedupStartedMs = 0;
  dragCurrentRaised = false;
  driverActive(PRECHARGE_CURRENT_MA);
  mode = DRAG_PRECHARGE;

  Serial.printf("SPIN#%lu DRAG_PRECHARGE dir=%+d targetAngle=%.1f targetWedge=%d runwayDeg=%.1f currentMa=%u\n",
                (unsigned long)spinRecord.number, direction, targetAngle,
                wedgeAtAngle(targetAngle), runwayDeg, PRECHARGE_CURRENT_MA);
}

static bool launchDrag() {
  float forwardSpeed = omega * dragDirection;
  if (forwardSpeed <= DRAG_MIN_REV_S) return false;

  float startRevS = fminf(forwardSpeed * DRAG_ENTRY_FRACTION,
                          DRAG_MAX_REV_S);
  if (startRevS < DRAG_MIN_REV_S) startRevS = DRAG_MIN_REV_S;
  uint32_t startHz = (uint32_t)lroundf(startRevS * WHEEL_USTEPS_PER_REV);

  driverActive(DRAG_CURRENT_START_MA);
  if (!startContinuousStepper(dragDirection, startHz)) {
    driverFreewheel();
    return false;
  }

  dragCurrentHz = startHz;
  dragStartedMs = millis();
  dragLastControlMs = dragStartedMs;
  dragLowestForwardSpeed = forwardSpeed;
  mode = DRAG_ACTIVE;
  Serial.printf("SPIN#%lu DRAG_START dir=%+d wheelRevS=%.3f commandRevS=%.3f hz=%lu targetAngle=%.1f\n",
                (unsigned long)spinRecord.number, dragDirection, forwardSpeed,
                startRevS, (unsigned long)startHz, dragTargetAngle);
  return true;
}

static void stopDragIntoHold(const char* reason) {
  if (stepper && stepper->isRunning() && !stepper->isStopping()) {
    stepper->stopMove();
  }
  dragStopRequestedMs = millis();
  mode = DRAG_STOPPING;
  Serial.printf("SPIN#%lu DRAG_STOP reason=%s remainingDeg=%.1f wheelRevS=%.3f commandHz=%lu\n",
                (unsigned long)spinRecord.number, reason, dragRemainingDeg(),
                omega * dragDirection, (unsigned long)dragCurrentHz);
}

static void serviceDrag() {
  if (!stepper) {
    driverFreewheel();
    mode = FREE_SPIN;
    return;
  }

  if (!encoderPositionFresh()) {
    stepper->forceStopAndNewPosition(0);
    driverActive(HOLD_CURRENT_MA);
    mode = FAULT_HOLD;
    Serial.printf("SPIN#%lu FAULT code=ENCODER_STALE_DURING_DRAG\n",
                  (unsigned long)spinRecord.number);
    return;
  }

  if (!encoderMotionReady()) return;  // planner continues through a short re-prime

  float forwardSpeed = omega * dragDirection;
  if (forwardSpeed < -OPPOSITE_ABORT_REV_S) {
    if (dragOppositeStartedMs == 0) dragOppositeStartedMs = millis();
    if (millis() - dragOppositeStartedMs >= OPPOSITE_ABORT_MS) {
      stopDragIntoHold("opposite-motion");
      return;
    }
  } else {
    dragOppositeStartedMs = 0;
  }

  if (forwardSpeed > 0.0f && forwardSpeed < dragLowestForwardSpeed) {
    dragLowestForwardSpeed = forwardSpeed;
    dragSpeedupStartedMs = 0;
  } else if (forwardSpeed > dragLowestForwardSpeed + SPEEDUP_ABORT_REV_S) {
    if (dragSpeedupStartedMs == 0) dragSpeedupStartedMs = millis();
    if (millis() - dragSpeedupStartedMs >= SPEEDUP_ABORT_MS) {
      stopDragIntoHold("wheel-speed-up");
      return;
    }
  } else {
    dragSpeedupStartedMs = 0;
  }

  float remainingDeg = dragRemainingDeg();
  if (remainingDeg <= TARGET_CAPTURE_DEG ||
      remainingDeg < -TARGET_OVERSHOOT_ABORT_DEG) {
    stopDragIntoHold(remainingDeg < 0.0f ? "target-crossed" : "capture-zone");
    return;
  }

  if (millis() - dragStartedMs > DRAG_TIMEOUT_MS) {
    stopDragIntoHold("timeout");
    return;
  }

  if (!dragCurrentRaised && millis() - dragStartedMs >= DRAG_CURRENT_RISE_MS) {
    driver.rms_current(DRAG_CURRENT_MA, 1.0);
    dragCurrentRaised = true;
  }

  uint32_t nowMs = millis();
  if (nowMs - dragLastControlMs < DRAG_CONTROL_PERIOD_MS) return;
  dragLastControlMs = nowMs;

  float profileDistanceDeg = fmaxf(0.0f, remainingDeg - TARGET_CAPTURE_DEG);
  float accelRevS2 = (float)DRAG_SLEW_SPS2 / WHEEL_USTEPS_PER_REV;
  float profileLimitRevS = sqrtf(
      2.0f * accelRevS2 * (profileDistanceDeg / 360.0f));
  float blend = fminf(1.0f, (float)(millis() - dragStartedMs) /
                               (float)DRAG_BLEND_MS);
  float trailFraction = DRAG_ENTRY_FRACTION +
      blend * (DRAG_FINAL_FRACTION - DRAG_ENTRY_FRACTION);
  float trailLimitRevS = fmaxf(0.0f, forwardSpeed) * trailFraction;
  float commandRevS = fminf(trailLimitRevS, profileLimitRevS);
  if (commandRevS > DRAG_MAX_REV_S) commandRevS = DRAG_MAX_REV_S;

  if (commandRevS < DRAG_MIN_REV_S || forwardSpeed <= DRAG_MIN_REV_S) {
    stopDragIntoHold("low-speed");
    return;
  }

  uint32_t requestedHz = (uint32_t)lroundf(commandRevS * WHEEL_USTEPS_PER_REV);
  if (requestedHz < 1) requestedHz = 1;
  if (requestedHz != dragCurrentHz) {
    if (stepper->setSpeedInHz(requestedHz) == 0) {
      // applySpeedAcceleration() updates a continuous run without issuing a new
      // runForward/runBackward command and without changing direction.
      stepper->applySpeedAcceleration();
      dragCurrentHz = requestedHz;
    }
  }

  if (verboseControl) {
    static uint32_t lastPrintMs = 0;
    if (nowMs - lastPrintMs >= 150) {
      lastPrintMs = nowMs;
      Serial.printf("# DRAG angle=%.1f target=%.1f remaining=%.1f omega=%.3f commandRevS=%.3f hz=%lu\n",
                    wheelAngleDeg(), dragTargetAngle, remainingDeg, omega,
                    commandRevS, (unsigned long)dragCurrentHz);
    }
  }
}

