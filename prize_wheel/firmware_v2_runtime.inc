/* --------------------------- Spin state machine ------------------------- */
static bool detectConfirmedSpin(int& confirmedDirection) {
  if (!encoderMotionReady()) {
    spinCandidateStartedMs = 0;
    return false;
  }

  uint32_t nowMs = millis();
  int32_t minTravel = countsForDegrees(SPIN_CONFIRM_TRAVEL_DEG);
  int32_t backtrack = countsForDegrees(SPIN_CANCEL_BACKTRACK_DEG);

  if (spinCandidateStartedMs == 0) {
    if (fabsf(omega) >= SPIN_DETECT_REV_S) {
      spinCandidateStartedMs = nowMs;
      spinCandidateStartCounts = encoderCountsMT;
      spinCandidateDirection = omega >= 0.0f ? 1 : -1;
    }
    return false;
  }

  int32_t signedTravel = spinCandidateDirection *
      (encoderCountsMT - spinCandidateStartCounts);
  if (signedTravel < -backtrack ||
      nowMs - spinCandidateStartedMs > SPIN_CONFIRM_TIMEOUT_MS) {
    spinCandidateStartedMs = 0;
    return false;
  }

  if (nowMs - spinCandidateStartedMs >= SPIN_CONFIRM_MS &&
      signedTravel >= minTravel) {
    confirmedDirection = spinCandidateDirection;
    spinCandidateStartedMs = 0;
    return true;
  }
  return false;
}

static bool guestOverrideDetected() {
  if (!encoderMotionReady() || fabsf(omega) <= GUEST_OVERRIDE_REV_S) {
    guestOverrideStartedMs = 0;
    return false;
  }
  if (guestOverrideStartedMs == 0) guestOverrideStartedMs = millis();
  return millis() - guestOverrideStartedMs >= GUEST_OVERRIDE_CONFIRM_MS;
}

static void restartAsGuestSpin() {
  driverFreewheel();
  int direction = omega >= 0.0f ? 1 : -1;
  resetSpinRecord(direction);
  mode = FREE_SPIN;
  settleStartedMs = 0;
  guestOverrideStartedMs = 0;
}

static void evaluateDecision() {
  if (spinRecord.decisionMade || !encoderMotionReady()) return;

  float speed = fabsf(omega);
  if (spinRecord.peakOmega < DECISION_MIN_PEAK_REV_S) return;
  if (speed > DECISION_REV_S || speed < DECISION_MIN_REV_S) return;

  float predictedAngle = predictStopAngle();
  bool unsafe = predictedStopCouldBeDare(predictedAngle);
  if (!unsafe) {
    recordDecision(false, predictedAngle, 0.0f, "predicted-safe");
    return;
  }

  if (observeOnly) {
    recordDecision(false, predictedAngle, 0.0f, "observe-only");
    return;
  }

  if (!automaticControlReady()) {
    recordDecision(false, predictedAngle, 0.0f, "control-lockout");
    return;
  }

  float minRunway = minimumRunwayDeg(speed);
  float targetAngle = 0.0f;
  float runwayDeg = 0.0f;
  if (!chooseNearestSafeTarget(spinRecord.direction, wheelAngleDeg(),
                               minRunway, targetAngle, runwayDeg)) {
    recordDecision(false, predictedAngle, 0.0f, "no-safe-runway");
    return;
  }

  recordDecision(true, predictedAngle, targetAngle, "predicted-dare");
  beginDrag(spinRecord.direction, targetAngle, runwayDeg);
}

static bool settledFor(uint16_t durationMs) {
  if (!encoderMotionReady() || fabsf(omega) > STILL_REV_S) {
    settleStartedMs = 0;
    return false;
  }
  if (settleStartedMs == 0) settleStartedMs = millis();
  return millis() - settleStartedMs >= durationMs;
}

static void enterSafeHold() {
  driverActive(HOLD_CURRENT_MA);
  safeHoldStartedMs = millis();
  mode = SAFE_HOLD;
}

static void setupStateMachine() {
  memset(&spinRecord, 0, sizeof(spinRecord));
  mode = IDLE;
  driverFreewheel();
}

static void serviceStateMachine() {
  // Release a normal post-intervention hold at the first sign of a guest touch;
  // do not make the guest overcome holding torque for the full spin-confirm time.
  if (mode == SAFE_HOLD && encoderMotionReady() &&
      fabsf(omega) >= 0.04f) {
    driverFreewheel();
    mode = IDLE;
    spinCandidateStartedMs = 0;
  }

  int confirmedDirection = 1;
  bool newSpin = (mode == IDLE) && detectConfirmedSpin(confirmedDirection);

  if (newSpin) {
    driverFreewheel();
    resetSpinRecord(confirmedDirection);
    settleStartedMs = 0;
    safeHoldStartedMs = 0;
    mode = FREE_SPIN;
  }

  bool guestOverride = guestOverrideDetected();

  switch (mode) {
    case IDLE:
      break;

    case FREE_SPIN:
      if (!encoderMotionReady()) break;
      updateSpinSnapshots();
      evaluateDecision();
      if (mode != FREE_SPIN) break;
      if (fabsf(omega) <= STILL_REV_S) {
        settleStartedMs = 0;
        mode = SETTLE;
      }
      break;

    case DRAG_PRECHARGE:
      if (guestOverride) {
        restartAsGuestSpin();
        break;
      }
      if (!encoderPositionFresh()) {
        driverFreewheel();
        mode = FREE_SPIN;
        break;
      }
      if (millis() - dragPrechargeStartedMs >= PRECHARGE_MS) {
        if (!launchDrag()) {
          driverFreewheel();
          mode = FREE_SPIN;
          Serial.printf("SPIN#%lu DRAG_CANCEL reason=launch-failed\n",
                        (unsigned long)spinRecord.number);
        }
      }
      break;

    case DRAG_ACTIVE:
      if (guestOverride) {
        restartAsGuestSpin();
        break;
      }
      serviceDrag();
      break;

    case DRAG_STOPPING:
      if (guestOverride) {
        restartAsGuestSpin();
        break;
      }
      if (stepper && stepper->isRunning() &&
          millis() - dragStopRequestedMs > 2000) {
        stepper->forceStopAndNewPosition(0);
        Serial.printf("SPIN#%lu DRAG_FORCE_STOP reason=stop-timeout\n",
                      (unsigned long)spinRecord.number);
      }
      if (!stepper || !stepper->isRunning()) {
        spinRecord.dragCompleted = true;
        spinRecord.targetErrorDeg = dragRemainingDeg();
        driverActive(HOLD_CURRENT_MA);
        settleStartedMs = 0;
        mode = SETTLE;
      }
      break;

    case SETTLE:
      if (guestOverride) {
        restartAsGuestSpin();
        break;
      }
      if (!settledFor(SETTLE_MS)) break;

      if (!spinRecord.decisionMade) {
        float predicted = predictStopAngle();
        recordDecision(false, predicted, 0.0f, "settled-before-decision");
      }
      recordPrimaryStop();

      if (!zeroCalibrated) {
        printFinalLanding();
        driverFreewheel();
        mode = IDLE;
      } else if (isDare(currentWedge())) {
        if (observeOnly) {
          printFinalLanding();
          driverFreewheel();
          mode = IDLE;
        } else {
          beginRecovery();
        }
      } else {
        printFinalLanding();
        if (spinRecord.dragAttempted) {
          enterSafeHold();
        } else {
          // The prediction said LEAVE and the natural stop is safe. Preserve
          // the central contract: the motor remains completely untouched.
          driverFreewheel();
          mode = IDLE;
        }
      }
      break;

    case RECOVERY_PRECHARGE:
      if (!encoderPositionFresh()) {
        driverActive(HOLD_CURRENT_MA);
        mode = FAULT_HOLD;
        Serial.printf("SPIN#%lu FAULT code=RECOVERY_PRECHARGE_SENSOR_STALE\n",
                      (unsigned long)spinRecord.number);
        break;
      }
      if (millis() - recoveryStartedMs >= RECOVERY_PRECHARGE_MS) {
        if (!launchRecovery()) {
          driverActive(HOLD_CURRENT_MA);
          mode = FAULT_HOLD;
          Serial.printf("SPIN#%lu FAULT code=RECOVERY_LAUNCH_FAILED\n",
                        (unsigned long)spinRecord.number);
        }
      }
      break;

    case RECOVERY_ACTIVE:
      serviceRecovery();
      break;

    case RECOVERY_SETTLE:
      if (!settledFor(SETTLE_MS)) break;
      if (isDare(currentWedge())) {
        beginRecovery();
      } else {
        printFinalLanding();
        enterSafeHold();
      }
      break;

    case SAFE_HOLD:
      // A short hold prevents immediate post-stop drift. Release automatically
      // so the wheel feels loose before the next guest spin.
      if (millis() - safeHoldStartedMs >= SAFE_HOLD_AFTER_SETTLE_MS) {
        driverFreewheel();
        mode = IDLE;
        settleStartedMs = 0;
        spinCandidateStartedMs = 0;
      }
      break;

    case DIR_PROBE:
      serviceDirectionProbe();
      break;

    case FAULT_HOLD:
      // Intentionally held until an operator uses 'r'. Do not silently accept
      // a dare landing or make repeated uncontrolled recovery moves.
      break;
  }
}

/* -------------------------------- Setup --------------------------------- */
void setup() {
  Serial.begin(115200);
  delay(300);

  preferences.begin("prizewheel", false);
  zeroCalibrated = preferences.getBool("zero_ok", false);
  wedgeZeroLogicalCounts = positiveMod(preferences.getInt("zero_count", 0), 4096);
  motorPositiveEncoderSign = preferences.getInt("pos_sign", 0);
  motorDirectionCalibrated = preferences.getBool("dir_ok", false) &&
      (motorPositiveEncoderSign == 1 || motorPositiveEncoderSign == -1);

  Wire.begin(PIN_SDA, PIN_SCL);
  Wire.setClock(400000);
  Wire.setTimeOut(3);

  if (readRawSample()) {
    primeEncoder(encoderRead.raw, encoderRead.doneUs, false);
    samplerScheduled = true;
    nextSampleDueUs = encoderRead.doneUs + ENCODER_SAMPLE_PERIOD_US;
    Serial.printf("# AS5600 primed raw=%u logical=%ld i2c_us=%u\n",
                  encoderRead.raw, (long)encoderCountsMT, encoderRead.i2cUs);
  } else {
    Serial.printf("# AS5600 initial read failed tx=%u requested=%u available=%u\n",
                  encoderRead.txStatus, encoderRead.requested,
                  encoderRead.available);
  }

  pinMode(PIN_EN, OUTPUT);
  digitalWrite(PIN_EN, LOW);

  TMC_SERIAL.begin(115200, SERIAL_8N1, TMC_RX_PIN, TMC_TX_PIN);
  driverConfig();

  engine.init();
  stepper = engine.stepperConnectToPin(PIN_STEP);
  if (stepper) {
    stepper->setDirectionPin(PIN_DIR, INVERT_DIR);
    stepper->setEnablePin(PIN_EN, true);
    stepper->setAutoEnable(false);
  }

  uint8_t connection = driver.test_connection();
  tmcHealthy = connection == 0;
  Serial.printf("# TMC test_connection=%u healthy=%d\n",
                connection, tmcHealthy ? 1 : 0);

  Serial.printf("# calibration zero=%d zeroCount=%ld motorDir=%d\n",
                zeroCalibrated ? 1 : 0, (long)wedgeZeroLogicalCounts,
                motorDirectionCalibrated ? motorPositiveEncoderSign : 0);
  if (!tmcHealthy || !zeroCalibrated || !motorDirectionCalibrated) {
    Serial.println(F("# CONTROL LOCKED until TMC healthy and z+p calibrations pass; observation still works"));
  }

  printHelp();
  setupStateMachine();
}

void loop() {
  updateEncoder();
  handleSerial();
  serviceStateMachine();
}
