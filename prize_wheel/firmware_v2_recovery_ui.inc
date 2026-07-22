/* -------------------------- Emergency recovery -------------------------- */
int recoveryDirection = 1;
float recoveryTargetAngle = 0.0f;
int32_t recoveryStartCounts = 0;
int32_t recoveryTravelCounts = 0;
uint32_t recoveryStartedMs = 0;
bool recoveryStopRequested = false;

static bool chooseRecoveryTarget(int direction, float& targetAngle,
                                 float& runwayDeg) {
  return chooseNearestSafeTarget(direction, wheelAngleDeg(), 0.0f,
                                 targetAngle, runwayDeg);
}

static void beginRecovery() {
  if (observeOnly || !emergencyRecoveryEnabled || !automaticControlReady() ||
      spinRecord.recoveryAttempts >= RECOVERY_MAX_ATTEMPTS) {
    driverActive(HOLD_CURRENT_MA);
    mode = FAULT_HOLD;
    Serial.printf("SPIN#%lu FAULT code=DARE_STOP_RECOVERY_UNAVAILABLE wedge=%d attempts=%u\n",
                  (unsigned long)spinRecord.number, currentWedge(),
                  spinRecord.recoveryAttempts);
    return;
  }

  float runwayDeg = 0.0f;
  recoveryDirection = spinRecord.direction;
  if (!chooseRecoveryTarget(recoveryDirection, recoveryTargetAngle, runwayDeg)) {
    driverActive(HOLD_CURRENT_MA);
    mode = FAULT_HOLD;
    Serial.printf("SPIN#%lu FAULT code=NO_FORWARD_SAFE_RECOVERY_TARGET\n",
                  (unsigned long)spinRecord.number);
    return;
  }

  ++spinRecord.recoveryAttempts;
  spinRecord.recoveryUsed = true;
  recoveryStartCounts = encoderCountsMT;
  recoveryTravelCounts = countsForDegrees(runwayDeg);
  recoveryStartedMs = millis();
  recoveryStopRequested = false;
  driverActive(PRECHARGE_CURRENT_MA);
  mode = RECOVERY_PRECHARGE;

  Serial.printf("SPIN#%lu RECOVERY_PRECHARGE dir=%+d fromWedge=%d targetWedge=%d targetAngle=%.1f runwayDeg=%.1f attempt=%u\n",
                (unsigned long)spinRecord.number, recoveryDirection,
                currentWedge(), wedgeAtAngle(recoveryTargetAngle),
                recoveryTargetAngle, runwayDeg, spinRecord.recoveryAttempts);
}

static bool launchRecovery() {
  int fasSign = fasSignForEncoderDirection(recoveryDirection);
  if (fasSign == 0 || !stepper) return false;

  float runwayDeg = forwardDistanceDeg(recoveryDirection, wheelAngleDeg(),
                                       recoveryTargetAngle);
  recoveryStartCounts = encoderCountsMT;
  recoveryTravelCounts = countsForDegrees(runwayDeg);
  int32_t moveUsteps = ustepsForDegrees(runwayDeg);
  if (moveUsteps <= 0 || recoveryTravelCounts <= 0) return false;

  stepper->forceStopAndNewPosition(0);
  driverActive(RECOVERY_CURRENT_MA);
  stepper->setSpeedInHz(RECOVERY_SPEED_HZ);
  stepper->setAcceleration(RECOVERY_ACCEL_SPS2);
  stepper->setJumpStart(0);
  MoveResultCode result = stepper->move(fasSign * moveUsteps);
  if (result != MOVE_OK) return false;

  recoveryStartedMs = millis();
  recoveryStopRequested = false;
  mode = RECOVERY_ACTIVE;
  Serial.printf("SPIN#%lu RECOVERY_START dir=%+d fasDir=%+d targetWedge=%d runwayDeg=%.1f\n",
                (unsigned long)spinRecord.number, recoveryDirection, fasSign,
                wedgeAtAngle(recoveryTargetAngle), runwayDeg);
  return true;
}

static void serviceRecovery() {
  if (!stepper || !encoderPositionFresh()) {
    if (stepper) stepper->forceStopAndNewPosition(0);
    driverActive(HOLD_CURRENT_MA);
    mode = FAULT_HOLD;
    Serial.printf("SPIN#%lu FAULT code=RECOVERY_SENSOR_OR_STEPPER\n",
                  (unsigned long)spinRecord.number);
    return;
  }

  int32_t travelled = recoveryDirection *
      (encoderCountsMT - recoveryStartCounts);
  int32_t stopLead = countsForDegrees(RECOVERY_STOP_LEAD_DEG);
  int32_t oppositeTolerance = countsForDegrees(1.5f);

  if (travelled < -oppositeTolerance) {
    stepper->forceStopAndNewPosition(0);
    driverActive(HOLD_CURRENT_MA);
    mode = FAULT_HOLD;
    Serial.printf("SPIN#%lu FAULT code=RECOVERY_OPPOSITE_MOTION deg=%.1f\n",
                  (unsigned long)spinRecord.number,
                  travelled * 360.0f / 4096.0f);
    return;
  }

  if (!recoveryStopRequested &&
      travelled >= recoveryTravelCounts - stopLead) {
    recoveryStopRequested = true;
    stepper->stopMove();
  }

  if (stepper->isRunning() &&
      millis() - recoveryStartedMs <= RECOVERY_TIMEOUT_MS) return;

  if (stepper->isRunning()) stepper->forceStopAndNewPosition(0);
  driverActive(HOLD_CURRENT_MA);
  settleStartedMs = 0;
  mode = RECOVERY_SETTLE;
}

/* --------------------------- Direction probe ---------------------------- */
int32_t directionProbeStartCounts = 0;
uint32_t directionProbeStartedMs = 0;

static bool probeStartIsSafe() {
  if (!zeroCalibrated) return false;
  int wedge = currentWedge();
  if (isDare(wedge) || isDare(wedge - 1) || isDare(wedge + 1)) return false;
  float within = positiveFmod(wheelAngleDeg(), WEDGE_DEG);
  return fminf(within, WEDGE_DEG - within) >= 11.0f;
}

static void startDirectionProbe() {
  if (!tmcHealthy || !stepper || !encoderMotionReady()) {
    Serial.println(F("# DIR_PROBE refused: TMC/stepper/encoder unavailable"));
    return;
  }
  if (mode != IDLE) {
    Serial.println(F("# DIR_PROBE refused: wheel is busy"));
    return;
  }
  if (!probeStartIsSafe()) {
    Serial.println(F("# DIR_PROBE refused: calibrate z, then use center of safe wedge 3 or 7-11"));
    return;
  }

  stepper->forceStopAndNewPosition(0);
  driverActive(DIR_PROBE_CURRENT_MA);
  stepper->setSpeedInHz(DIR_PROBE_SPEED_HZ);
  stepper->setAcceleration(DIR_PROBE_ACCEL_SPS2);
  stepper->setJumpStart(0);
  directionProbeStartCounts = encoderCountsMT;
  directionProbeStartedMs = millis();
  if (stepper->move(DIR_PROBE_USTEPS) != MOVE_OK) {
    driverFreewheel();
    Serial.println(F("# DIR_PROBE failed to queue move"));
    return;
  }
  mode = DIR_PROBE;
  Serial.println(F("# DIR_PROBE moving FAS+ by 9 wheel degrees; keep hands clear"));
}

static void serviceDirectionProbe() {
  if (stepper && stepper->isRunning() &&
      millis() - directionProbeStartedMs < DIR_PROBE_TIMEOUT_MS) return;

  if (stepper && stepper->isRunning()) stepper->forceStopAndNewPosition(0);
  float movedDeg = (encoderCountsMT - directionProbeStartCounts) *
                   360.0f / 4096.0f;
  if (fabsf(movedDeg) < DIR_PROBE_MIN_DEG) {
    motorDirectionCalibrated = false;
    motorPositiveEncoderSign = 0;
    preferences.putBool("dir_ok", false);
    driverFreewheel();
    mode = IDLE;
    Serial.printf("# DIR_PROBE FAIL movedDeg=%.2f\n", movedDeg);
    return;
  }

  motorPositiveEncoderSign = movedDeg > 0.0f ? 1 : -1;
  motorDirectionCalibrated = true;
  preferences.putInt("pos_sign", motorPositiveEncoderSign);
  preferences.putBool("dir_ok", true);
  driverFreewheel();
  mode = IDLE;
  Serial.printf("# DIR_PROBE PASS FAS+ encoderDirection=%+d\n",
                motorPositiveEncoderSign);
}

/* ----------------------------- Diagnostics ------------------------------ */
static void dumpDiagnostics() {
  Serial.printf("# DIAG count=%u columns=done_us,dt_us,raw,delta,counts,i2c_us,omega_mrev_s,flags,state\n",
                diagnosticCount);
  uint16_t first = (diagnosticHead + DIAG_CAPACITY - diagnosticCount) %
                   DIAG_CAPACITY;
  for (uint16_t i = 0; i < diagnosticCount; ++i) {
    const DiagnosticSample& sample =
        diagnosticBuffer[(first + i) % DIAG_CAPACITY];
    Serial.printf("D,%lu,%lu,%u,%d,%ld,%u,%d,%02X,%u\n",
                  (unsigned long)sample.doneUs,
                  (unsigned long)sample.dtGoodUs,
                  sample.raw, sample.delta, (long)sample.counts,
                  sample.i2cUs, sample.omegaMilli,
                  sample.flags, sample.state);
  }
}

static void armDiagnostics() {
  diagnosticHead = 0;
  diagnosticCount = 0;
  diagnosticSawMotion = false;
  diagnosticCapture = true;
  Serial.println(F("# DIAG armed: RAM-only capture; use D after the wheel stops to dump"));
}

/* ------------------------------- Serial --------------------------------- */
static const char* modeName() {
  switch (mode) {
    case IDLE: return "IDLE";
    case FREE_SPIN: return "FREE_SPIN";
    case DRAG_PRECHARGE: return "DRAG_PRECHARGE";
    case DRAG_ACTIVE: return "DRAG_ACTIVE";
    case DRAG_STOPPING: return "DRAG_STOPPING";
    case SETTLE: return "SETTLE";
    case RECOVERY_PRECHARGE: return "RECOVERY_PRECHARGE";
    case RECOVERY_ACTIVE: return "RECOVERY_ACTIVE";
    case RECOVERY_SETTLE: return "RECOVERY_SETTLE";
    case SAFE_HOLD: return "SAFE_HOLD";
    case DIR_PROBE: return "DIR_PROBE";
    case FAULT_HOLD: return "FAULT_HOLD";
  }
  return "UNKNOWN";
}

static void printStatus() {
  Serial.printf("# STATUS mode=%s angle=%.2f wedge=%d omega=%.4f posFresh=%d velocityValid=%d zero=%d dir=%d tmc=%d observeOnly=%d recovery=%d predAngle=%.1f predWedge=%d\n",
                modeName(), wheelAngleDeg(), currentWedge(), omega,
                encoderPositionFresh() ? 1 : 0,
                encoderVelocityValid ? 1 : 0,
                zeroCalibrated ? 1 : 0,
                motorDirectionCalibrated ? motorPositiveEncoderSign : 0,
                tmcHealthy ? 1 : 0, observeOnly ? 1 : 0,
                emergencyRecoveryEnabled ? 1 : 0,
                predictStopAngle(), wedgeAtAngle(predictStopAngle()));
}

static void printHelp() {
  Serial.println(F(
      "\n=== PRIZE WHEEL v2 ===\n"
      " z  persist current pointer position as wedge-0 boundary\n"
      " p  attended 9-degree motor direction probe\n"
      " s  status\n"
      " d  arm RAM encoder capture\n"
      " D  stop and dump RAM encoder capture\n"
      " o  toggle observe-only mode\n"
      " e  toggle emergency post-stop recovery\n"
      " v  toggle live drag logging\n"
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

    case 'p':
      startDirectionProbe();
      break;

    case 's':
      printStatus();
      break;

    case 'd':
      armDiagnostics();
      break;

    case 'D':
      diagnosticCapture = false;
      dumpDiagnostics();
      break;

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
      driverFreewheel();
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

    case '?':
      printHelp();
      break;

    default:
      break;
  }
}

