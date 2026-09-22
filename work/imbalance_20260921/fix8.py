p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('recovery state enum',
     '  ST_CAPTURE_ARMING // append: observe STEP with EN high before torque\n',
     '  ST_CAPTURE_ARMING, // append: observe STEP with EN high before torque\n'
     '  ST_RECOVERY_NUDGE  // 2026-09-21: powered step off a dare into the adjacent safe wedge\n')

edit('recovery state name',
     '    case ST_CAPTURE_ARMING: return "CAPTURE_ARMING";\n',
     '    case ST_CAPTURE_ARMING: return "CAPTURE_ARMING";\n'
     '    case ST_RECOVERY_NUDGE: return "RECOVERY_NUDGE";\n')

edit('recovery globals',
     'uint8_t recoveryGuardRaw = 0;\n',
     'uint8_t recoveryGuardRaw = 0;\n'
     '// Dare recovery (2026-09-21). On this wheel a dying coast balances on the\n'
     '// crest (wedge 8, the hard dare) or settles in the rest cone that overlaps\n'
     '// wedge 16. A resting wheel on a dare is stepped, energized, into the centre\n'
     '// of the adjacent safe wedge in the downhill direction: it reads as the\n'
     '// wheel teetering and settling, and it ends in a held safe wedge.\n'
     'const uint32_t RECOVERY_SPEED_HZ  = 220;   // ~0.07 rev/s\n'
     'const uint32_t RECOVERY_ACCEL_SPS2 = 400;\n'
     'const uint16_t RECOVERY_CURRENT_MA = 2200;\n'
     'const uint32_t RECOVERY_TIMEOUT_MS = 8000;\n'
     'uint8_t dareRecoveryAttempts = 0;\n'
     'uint32_t recoveryStillSinceMs = 0;\n'
     'bool startDareRecovery(const char* why);\n')

edit('recovery reset per spin',
     '  takeoverDir = spinDir;\n',
     '  takeoverDir = spinDir;\n'
     '  dareRecoveryAttempts = 0;\n')

edit('recovery in verdict',
     '  if (isDare(wedge)) {\n'
     '    // A controlled attempt settled on a dare: hardware/model failure.  Be\n'
     '    // honest, latch, and lock automatic control until inspected.\n'
     '    enterFault(FC_LANDING_UNSAFE, "settled on dare after control");\n'
     '    return;\n'
     '  }\n'
     '  if (dareDistanceDeg(angle) < DARE_PROXIMITY_FAULT_DEG) {\n',
     '  if (isDare(wedge) || dareDistanceDeg(angle) < DARE_PROXIMITY_FAULT_DEG) {\n'
     '    if (dareRecoveryAttempts < 2 && startDareRecovery("after control")) return;\n'
     '  }\n'
     '  if (isDare(wedge)) {\n'
     '    // A controlled attempt settled on a dare: hardware/model failure.  Be\n'
     '    // honest, latch, and lock automatic control until inspected.\n'
     '    enterFault(FC_LANDING_UNSAFE, "settled on dare after control");\n'
     '    return;\n'
     '  }\n'
     '  if (dareDistanceDeg(angle) < DARE_PROXIMITY_FAULT_DEG) {\n')

edit('recovery after free coast',
     '          SpinResult res = !controlAvailable() ? RES_CONTROL_LOCKED\n'
     '                          : (spin.hadContact ? RES_GUEST_STOPPED\n'
     '                                             : RES_NO_REACHABLE_SAFE);\n'
     '          closeSpin(res);\n'
     '          state = ST_IDLE_STOPPED;\n'
     '          stateEnteredMs = nowMs;\n',
     '          SpinResult res = !controlAvailable() ? RES_CONTROL_LOCKED\n'
     '                          : (spin.hadContact ? RES_GUEST_STOPPED\n'
     '                                             : RES_NO_REACHABLE_SAFE);\n'
     '          if (controlAvailable() && takeoverEnabled &&\n'
     '              (isDare(currentWedge()) || dareDistanceDeg(wheelAngleDeg()) < DARE_PROXIMITY_FAULT_DEG)) {\n'
     '            dareRecoveryAttempts = 0;\n'
     '            spin.targetWedge = -1;\n'
     '            if (startDareRecovery("free coast")) break;\n'
     '          }\n'
     '          closeSpin(res);\n'
     '          state = ST_IDLE_STOPPED;\n'
     '          stateEnteredMs = nowMs;\n')

edit('recovery implementation',
     'void probeFail(const char* why) {\n',
     '// Energized step from a resting dare into the adjacent safe wedge centre,\n'
     '// downhill (gravity direction) when that neighbour is safe, else the other\n'
     '// side. Returns false (nothing energized) when it cannot be done safely.\n'
     'bool startDareRecovery(const char* why) {\n'
     '  if (!stepper || !encoderPositionFresh() || stepper->isRunning()) return false;\n'
     '  if (encoderVelocityValid && fabsf(omega) > 0.05f) return false;\n'
     '  float angle = wheelAngleDeg();\n'
     '  int wedge = currentWedge();\n'
     '  float g = wheelGravity.forwardAccel(angle * DEG_TO_RAD, 1);\n'
     '  int dir = (fabsf(g) < 0.08f) ? (spinDir != 0 ? spinDir : 1) : (g > 0.0f ? 1 : -1);\n'
     '  int wt = wedge + dir;\n'
     '  if (isDare(wt)) { dir = -dir; wt = wedge + dir; }\n'
     '  if (isDare(wt)) return false;\n'
     '  wt %= NUM_WEDGES; if (wt < 0) wt += NUM_WEDGES;\n'
     '  float center = wt * WEDGE_DEG + 0.5f * WEDGE_DEG;\n'
     '  float dist = forwardDistanceDeg(dir, angle, center);\n'
     '  if (dist < 3.0f || dist > 2.0f * WEDGE_DEG) return false;\n'
     '  int fasSign = fasSignForEncoderDirection(dir);\n'
     '  if (!fasSign) return false;\n'
     '  int32_t steps = (int32_t)lroundf(dist * WHEEL_USTEPS_PER_REV / 360.0f);\n'
     '  bool wasEnergized = digitalRead(PIN_EN) == LOW;\n'
     '  if (!wasEnergized) setCurrentStage(CS_PRECHARGE);\n'
     '  driver.rms_current(RECOVERY_CURRENT_MA, 1.0);\n'
     '  g_currentMa = RECOVERY_CURRENT_MA;\n'
     '  stepper->setDirectionPin(PIN_DIR, fasSign > 0 ? INVERT_DIR : !INVERT_DIR);\n'
     '  stepper->setCurrentPosition(0);\n'
     '  stepper->setJumpStart(0);\n'
     '  if (!fasSetSpeedHz(RECOVERY_SPEED_HZ) || !fasSetAcceleration(RECOVERY_ACCEL_SPS2) ||\n'
     '      stepper->move(steps) != MoveResultCode::OK) {\n'
     '    if (!wasEnergized) driverFreewheel();\n'
     '    return false;\n'
     '  }\n'
     '  ++dareRecoveryAttempts;\n'
     '  recoveryStillSinceMs = 0;\n'
     '  state = ST_RECOVERY_NUDGE;\n'
     '  stateEnteredMs = millis();\n'
     '  Serial.printf("# DARE_RECOVERY (%s) attempt=%u from wedge %d angle=%.1f -> wedge %d dir=%+d %.1f deg\\n",\n'
     '                why, (unsigned)dareRecoveryAttempts, wedge, angle, wt, dir, dist);\n'
     '  return true;\n'
     '}\n'
     '\n'
     'void probeFail(const char* why) {\n')

edit('recovery service',
     '    case ST_DIR_PROBE:\n'
     '      serviceDirectionProbe();\n'
     '      break;\n',
     '    case ST_RECOVERY_NUDGE: {\n'
     '      if (!controlDriverSafe(nowMs)) break;\n'
     '      if (!encoderPositionFresh()) { enterFault(FC_ENCODER_STALE, "stale during recovery"); break; }\n'
     '      bool still = !stepper->isRunning() && encoderMotionReady() && fabsf(omega) <= STILL_REV_S;\n'
     '      if (still) { if (recoveryStillSinceMs == 0) recoveryStillSinceMs = nowMs; }\n'
     '      else recoveryStillSinceMs = 0;\n'
     '      if (nowMs - stateEnteredMs > RECOVERY_TIMEOUT_MS) {\n'
     '        stepper->forceStop();\n'
     '        enterFault(FC_LANDING_UNSAFE, "dare recovery timed out");\n'
     '        break;\n'
     '      }\n'
     '      if (recoveryStillSinceMs != 0 && nowMs - recoveryStillSinceMs >= SETTLE_MS) {\n'
     '        Serial.printf("# DARE_RECOVERY settled angle=%.1f wedge=%d\\n", wheelAngleDeg(), currentWedge());\n'
     '        setCurrentStage(CS_BRAKE);\n'
     '        state = ST_LANDING_SETTLE;   // verdict re-runs: safe -> hold, dare -> one more attempt, then latch\n'
     '        settleStillSinceMs = nowMs - SETTLE_MS;\n'
     '        settleWindowCounts = encoderCountsMT;\n'
     '        landingVerdict();\n'
     '      }\n'
     '      break;\n'
     '    }\n'
     '    case ST_DIR_PROBE:\n'
     '      serviceDirectionProbe();\n'
     '      break;\n')

edit('gentler engagement and caps',
     'const float TRAIL_FRACTION            = 0.95f;\n',
     'const float TRAIL_FRACTION            = 0.98f;  // 2026-09-21: 5% field lag snapped the wheel at torque-on\n')

edit('lower decel caps after 18:09 pole slip',
     'const uint32_t DECEL_CEILING_SPS2     = 500;    // total profile decel; motor share = this - friction -/+ gravity\n'
     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 800;\n',
     'const uint32_t DECEL_CEILING_SPS2     = 400;    // total profile decel; motor share = this - friction -/+ gravity\n'
     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 550;    // a=610 slipped poles at 2.8 A (18:09)\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
