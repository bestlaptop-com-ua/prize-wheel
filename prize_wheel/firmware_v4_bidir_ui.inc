/* -------- V4 bidirectional continuous-run probe and decision gate -------- */
static const uint16_t BIDIR_PROBE_CURRENT_MA = 500;
static const uint32_t BIDIR_PROBE_SPEED_HZ = 140;
static const uint32_t BIDIR_PROBE_ACCEL_SPS2 = 300;
static const uint16_t BIDIR_PROBE_RUN_MS = 900;
static const uint16_t BIDIR_PROBE_SETTLE_MS = 150;
static const uint16_t BIDIR_PROBE_STOP_TIMEOUT_MS = 1800;
static const float BIDIR_PROBE_MIN_DEG = 2.0f;

enum DirectionProbePhase : uint8_t {
  DIR_PROBE_IDLE_PHASE,
  DIR_PROBE_FORWARD_RUNNING,
  DIR_PROBE_FORWARD_STOPPING,
  DIR_PROBE_FORWARD_SETTLE,
  DIR_PROBE_REVERSE_RUNNING,
  DIR_PROBE_REVERSE_STOPPING
};

DirectionProbePhase directionProbePhase = DIR_PROBE_IDLE_PHASE;
int32_t directionProbeOriginCounts = 0;
int32_t directionProbePhaseStartCounts = 0;
uint32_t directionProbePhaseStartedMs = 0;
float directionProbeForwardDeg = 0.0f;
float directionProbeReverseDeg = 0.0f;

static const uint16_t DECISION_TREND_SAMPLE_MS = 60;
static const uint16_t DECISION_FALLING_CONFIRM_MS = 120;
static const float DECISION_FALLING_DROP_REV_S = 0.004f;
static const float DECISION_RISING_RESET_REV_S = 0.004f;
static const float DECISION_PEAK_DROP_REV_S = 0.045f;

static uint32_t decisionTrendSpinNumber = 0;
static uint32_t decisionTrendSampleMs = 0;
static uint32_t decisionFallingStartedMs = 0;
static float decisionTrendReferenceSpeed = 0.0f;

static float decisionMinimumPeakRequirement() {
  float speed = fabsf(omega);
  uint32_t nowMs = millis();

  if (decisionTrendSpinNumber != spinRecord.number) {
    decisionTrendSpinNumber = spinRecord.number;
    decisionTrendSampleMs = nowMs;
    decisionFallingStartedMs = 0;
    decisionTrendReferenceSpeed = speed;
    return 1000.0f;
  }

  if (nowMs - decisionTrendSampleMs >= DECISION_TREND_SAMPLE_MS) {
    float change = speed - decisionTrendReferenceSpeed;
    if (change <= -DECISION_FALLING_DROP_REV_S) {
      if (decisionFallingStartedMs == 0) {
        decisionFallingStartedMs = nowMs;
      }
    } else if (change >= DECISION_RISING_RESET_REV_S) {
      decisionFallingStartedMs = 0;
    }
    decisionTrendReferenceSpeed = speed;
    decisionTrendSampleMs = nowMs;
  }

  bool fallingConfirmed =
      decisionFallingStartedMs != 0 &&
      nowMs - decisionFallingStartedMs >= DECISION_FALLING_CONFIRM_MS;
  if (!fallingConfirmed) return 1000.0f;
  return speed + DECISION_PEAK_DROP_REV_S;
}

static bool probeStartIsSafeV4() {
  if (!zeroCalibrated) return false;
  int wedge = currentWedge();
  if (isDare(wedge) || isDare(wedge - 1) || isDare(wedge + 1)) return false;
  float within = positiveFmod(wheelAngleDeg(), WEDGE_DEG);
  return fminf(within, WEDGE_DEG - within) >= 11.0f;
}

static void failDirectionProbe(const char* reason) {
  if (stepper) stepper->forceStopAndNewPosition(0);
  motorDirectionCalibrated = false;
  motorPositiveEncoderSign = 0;
  preferences.putBool("dir_ok", false);
  preferences.putBool("bidir_ok", false);
  driverFreewheel();
  directionProbePhase = DIR_PROBE_IDLE_PHASE;
  mode = IDLE;
  Serial.printf("# BIDIR_PROBE FAIL reason=%s fwdDeg=%.2f revDeg=%.2f\n",
                reason, directionProbeForwardDeg, directionProbeReverseDeg);
}

static bool startProbeContinuousRun(bool forward) {
  if (!stepper) return false;
  stepper->forceStopAndNewPosition(0);
  stepper->setSpeedInHz(BIDIR_PROBE_SPEED_HZ);
  stepper->setAcceleration(BIDIR_PROBE_ACCEL_SPS2);
  stepper->setJumpStart(0);
  MoveResultCode result = forward ? stepper->runForward()
                                  : stepper->runBackward();
  return result == MOVE_OK;
}

static void startDirectionProbe() {
  if (!tmcHealthy || !stepper || !encoderMotionReady()) {
    Serial.println(F("# BIDIR_PROBE refused: TMC/stepper/encoder unavailable"));
    return;
  }
  if (mode != IDLE) {
    Serial.println(F("# BIDIR_PROBE refused: wheel is busy"));
    return;
  }
  if (!probeStartIsSafeV4()) {
    Serial.println(F("# BIDIR_PROBE refused: use center of safe wedge 3 or 7-11"));
    return;
  }

  motorDirectionCalibrated = false;
  motorPositiveEncoderSign = 0;
  preferences.putBool("dir_ok", false);
  preferences.putBool("bidir_ok", false);

  directionProbeOriginCounts = encoderCountsMT;
  directionProbePhaseStartCounts = encoderCountsMT;
  directionProbeForwardDeg = 0.0f;
  directionProbeReverseDeg = 0.0f;
  driverActive(BIDIR_PROBE_CURRENT_MA);

  if (!startProbeContinuousRun(true)) {
    failDirectionProbe("runForward-queue");
    return;
  }

  directionProbePhaseStartedMs = millis();
  directionProbePhase = DIR_PROBE_FORWARD_RUNNING;
  mode = DIR_PROBE;
  Serial.printf("# BIDIR_PROBE START currentMa=%u speedHz=%lu runMs=%u; keep hands clear\n",
                BIDIR_PROBE_CURRENT_MA,
                (unsigned long)BIDIR_PROBE_SPEED_HZ,
                BIDIR_PROBE_RUN_MS);
  Serial.println(F("# BIDIR_PROBE FWD_START command=runForward"));
}

static void serviceDirectionProbe() {
  if (!stepper || !encoderPositionFresh()) {
    failDirectionProbe("sensor-or-stepper");
    return;
  }

  uint32_t nowMs = millis();
  switch (directionProbePhase) {
    case DIR_PROBE_FORWARD_RUNNING:
      if (nowMs - directionProbePhaseStartedMs < BIDIR_PROBE_RUN_MS) return;
      stepper->stopMove();
      directionProbePhaseStartedMs = nowMs;
      directionProbePhase = DIR_PROBE_FORWARD_STOPPING;
      return;

    case DIR_PROBE_FORWARD_STOPPING:
      if (stepper->isRunning()) {
        if (nowMs - directionProbePhaseStartedMs >
            BIDIR_PROBE_STOP_TIMEOUT_MS) {
          failDirectionProbe("runForward-stop-timeout");
        }
        return;
      }
      directionProbeForwardDeg =
          (encoderCountsMT - directionProbePhaseStartCounts) *
          360.0f / 4096.0f;
      Serial.printf("# BIDIR_PROBE FWD_RESULT movedDeg=%.2f encoderSign=%+d\n",
                    directionProbeForwardDeg,
                    directionProbeForwardDeg >= 0.0f ? 1 : -1);
      if (fabsf(directionProbeForwardDeg) < BIDIR_PROBE_MIN_DEG) {
        failDirectionProbe("runForward-too-little-motion");
        return;
      }
      directionProbePhaseStartedMs = nowMs;
      directionProbePhase = DIR_PROBE_FORWARD_SETTLE;
      return;

    case DIR_PROBE_FORWARD_SETTLE:
      if (nowMs - directionProbePhaseStartedMs <
          BIDIR_PROBE_SETTLE_MS) return;
      directionProbePhaseStartCounts = encoderCountsMT;
      if (!startProbeContinuousRun(false)) {
        failDirectionProbe("runBackward-queue");
        return;
      }
      directionProbePhaseStartedMs = nowMs;
      directionProbePhase = DIR_PROBE_REVERSE_RUNNING;
      Serial.println(F("# BIDIR_PROBE REV_START command=runBackward"));
      return;

    case DIR_PROBE_REVERSE_RUNNING:
      if (nowMs - directionProbePhaseStartedMs < BIDIR_PROBE_RUN_MS) return;
      stepper->stopMove();
      directionProbePhaseStartedMs = nowMs;
      directionProbePhase = DIR_PROBE_REVERSE_STOPPING;
      return;

    case DIR_PROBE_REVERSE_STOPPING:
      if (stepper->isRunning()) {
        if (nowMs - directionProbePhaseStartedMs >
            BIDIR_PROBE_STOP_TIMEOUT_MS) {
          failDirectionProbe("runBackward-stop-timeout");
        }
        return;
      }
      directionProbeReverseDeg =
          (encoderCountsMT - directionProbePhaseStartCounts) *
          360.0f / 4096.0f;
      Serial.printf("# BIDIR_PROBE REV_RESULT movedDeg=%.2f encoderSign=%+d\n",
                    directionProbeReverseDeg,
                    directionProbeReverseDeg >= 0.0f ? 1 : -1);
      if (fabsf(directionProbeReverseDeg) < BIDIR_PROBE_MIN_DEG) {
        failDirectionProbe("runBackward-too-little-motion");
        return;
      }
      if ((directionProbeForwardDeg > 0.0f) ==
          (directionProbeReverseDeg > 0.0f)) {
        failDirectionProbe("directions-not-opposite");
        return;
      }

      motorPositiveEncoderSign =
          directionProbeForwardDeg > 0.0f ? 1 : -1;
      motorDirectionCalibrated = true;
      preferences.putInt("pos_sign", motorPositiveEncoderSign);
      preferences.putBool("dir_ok", true);
      preferences.putBool("bidir_ok", true);
      {
        float returnErrorDeg =
            (encoderCountsMT - directionProbeOriginCounts) *
            360.0f / 4096.0f;
        driverFreewheel();
        directionProbePhase = DIR_PROBE_IDLE_PHASE;
        mode = IDLE;
        Serial.printf("# BIDIR_PROBE PASS runForwardEncoder=%+d runBackwardEncoder=%+d returnErrorDeg=%.2f\n",
                      motorPositiveEncoderSign,
                      -motorPositiveEncoderSign,
                      returnErrorDeg);
      }
      return;

    case DIR_PROBE_IDLE_PHASE:
    default:
      failDirectionProbe("invalid-phase");
      return;
  }
}

static void printStatus() {
  bool bidirOk = preferences.getBool("bidir_ok", false) &&
      motorDirectionCalibrated;
  Serial.printf("# STATUS mode=%s angle=%.2f wedge=%d omega=%.4f posFresh=%d velocityValid=%d zero=%d dir=%d bidir=%d tmc=%d observeOnly=%d recovery=%d predAngle=%.1f predWedge=%d\n",
                modeName(), wheelAngleDeg(), currentWedge(), omega,
                encoderPositionFresh() ? 1 : 0,
                encoderVelocityValid ? 1 : 0,
                zeroCalibrated ? 1 : 0,
                bidirOk ? motorPositiveEncoderSign : 0,
                bidirOk ? 1 : 0,
                tmcHealthy ? 1 : 0, observeOnly ? 1 : 0,
                emergencyRecoveryEnabled ? 1 : 0,
                predictStopAngle(), wedgeAtAngle(predictStopAngle()));
}

static void printHelp() {
  Serial.println(F(
      "\n=== PRIZE WHEEL POWERED V4 ===\n"
      " z  persist current pointer position as wedge-0 boundary\n"
      " p  attended bidirectional continuous-run probe\n"
      " s  status\n"
      " d  arm RAM encoder capture\n"
      " D  stop and dump RAM encoder capture\n"
      " o  toggle observe-only mode\n"
      " e  toggle emergency post-stop recovery\n"
      " v  toggle live powered-control logging\n"
      " r  release FAULT_HOLD and freewheel\n"
      " x  clear persisted calibrations\n"
      " ?  help"));
}

static void handleSerial() {
  if (!Serial.available()) return;
  char command = (char)Serial.read();

  switch (command) {
    case 'z':
      if (!encoderPositionFresh()) {
        Serial.println(F("# ZERO refused: encoder position stale"));
      } else if (mode != IDLE) {
        Serial.println(F("# ZERO refused: wheel is busy"));
      } else {
        wedgeZeroLogicalCounts = positiveMod(encoderCountsMT, 4096);
        zeroCalibrated = true;
        preferences.putInt("zero_count", wedgeZeroLogicalCounts);
        preferences.putBool("zero_ok", true);
        Serial.printf("# ZERO saved logicalCount=%ld\n",
                      (long)wedgeZeroLogicalCounts);
      }
      break;

    case 'p': startDirectionProbe(); break;
    case 's': printStatus(); break;
    case 'd': armDiagnostics(); break;
    case 'D': diagnosticCapture = false; dumpDiagnostics(); break;

    case 'o':
      observeOnly = !observeOnly;
      Serial.printf("# observeOnly=%d\n", observeOnly ? 1 : 0);
      break;

    case 'e':
      emergencyRecoveryEnabled = !emergencyRecoveryEnabled;
      Serial.printf("# emergencyRecovery=%d\n",
                    emergencyRecoveryEnabled ? 1 : 0);
      break;

    case 'v':
      verboseControl = !verboseControl;
      Serial.printf("# verboseControl=%d\n", verboseControl ? 1 : 0);
      break;

    case 'r':
      if (stepper) stepper->forceStopAndNewPosition(0);
      driverFreewheel();
      directionProbePhase = DIR_PROBE_IDLE_PHASE;
      mode = IDLE;
      spinCandidateStartedMs = 0;
      Serial.println(F("# released to IDLE/freewheel"));
      break;

    case 'x':
      preferences.clear();
      zeroCalibrated = false;
      motorDirectionCalibrated = false;
      motorPositiveEncoderSign = 0;
      Serial.println(F("# calibrations cleared; run z then p"));
      break;

    case '?': printHelp(); break;
    default: break;
  }
}
