"""Build work/imbalance_20260921/prize_wheel_gpt from the 2026-09-20 production-review
candidate plus the gravity-imbalance / AS5600-INL integration.  Every anchor must match
exactly once; nothing is written unless all of them do."""
import hashlib, json, shutil, sys
from pathlib import Path

ROOT = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
SRC = ROOT / 'production_review_20260920' / 'prize_wheel_gpt'
OUT = ROOT / 'imbalance_20260921'
DST = OUT / 'prize_wheel_gpt'

EDITS = []
def edit(name, old, new):
    EDITS.append((name, old, new))

edit('build string',
     '# build: plywood-capture-review-20260920; takeover defaults OFF',
     '# build: imbalance-model-20260921 PARTY; based on plywood-capture-review-20260920; takeover defaults OFF')

edit('include',
     '#include "pw_friction_store.h"\n',
     '#include "pw_friction_store.h"\n#include "pw_imbalance.h"\n')

edit('globals',
     'int32_t encoderCountsMT = 0;\n',
     'int32_t encoderCountsMT = 0;\n'
     '// encoderCountsMT = uncorrected (rawZero - raw) accumulator + AS5600 INL offset.\n'
     '// With no stored model the offset is 0 and both are identical (legacy behaviour).\n'
     'int32_t encoderRawCountsMT = 0;\n'
     'static_assert(ENCODER_DIR_SIGN == -1, "INL offset sign assumes counts = rawZero - raw");\n'
     'PwEncoderInl encoderInl;\n'
     'PwGravity wheelGravity;\n'
     'PwImbalanceLine imbalanceLine;\n'
     'bool imbalanceLoaded = false;\n'
     '// Reach is judged at 96% speed: with gravity > friction the last crest is a\n'
     '// cliff (a 1% speed error can move the natural stop by most of a revolution).\n'
     'const float IMB_NATURAL_SPEED_MARGIN = 0.96f;\n'
     '// Mechanical-energy-equivalent speed at hand release; 0 = unknown (legacy re-push test).\n'
     'float releaseCompRevS = 0.0f;\n')

edit('fit sample struct',
     'struct FitSample { uint32_t ms; float omegaRadS; };',
     'struct FitSample { uint32_t ms; float omegaRadS; float angleRad; };')

edit('prime encoder',
     '  if (!encoderPrimed || !preserveNearestTurn) {\n'
     '    encoderCountsMT = staticCounts;\n'
     '  } else {\n'
     '    int32_t diff = encoderCountsMT - staticCounts;\n'
     '    int32_t turns = (int32_t)lroundf((float)diff / 4096.0f);\n'
     '    encoderCountsMT = staticCounts + turns * 4096;\n'
     '  }\n',
     '  if (!encoderPrimed || !preserveNearestTurn) {\n'
     '    encoderRawCountsMT = staticCounts;\n'
     '  } else {\n'
     '    int32_t diff = encoderRawCountsMT - staticCounts;\n'
     '    int32_t turns = (int32_t)lroundf((float)diff / 4096.0f);\n'
     '    encoderRawCountsMT = staticCounts + turns * 4096;\n'
     '  }\n'
     '  encoderCountsMT = encoderRawCountsMT + encoderInl.labelOffsetCounts(raw, rawZero);\n')

edit('update encoder',
     '  encoderCountsMT += ENCODER_DIR_SIGN * delta;   // invert to count clockwise\n',
     '  encoderRawCountsMT += ENCODER_DIR_SIGN * delta;   // invert to count clockwise\n'
     '  encoderCountsMT = encoderRawCountsMT + encoderInl.labelOffsetCounts(read.raw, rawZero);\n')

edit('natural stop',
     'float naturalStopDistanceDeg(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n',
     'float naturalStopDistanceDeg(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n'
     '  if (wheelGravity.active())  // from the CURRENT angle, conservative speed\n'
     '    return wheelGravity.stopTravelRad(speedRevS * IMB_NATURAL_SPEED_MARGIN, dir,\n'
     '        wheelAngleDeg() * DEG_TO_RAD, c, b, millis()) * RAD_TO_DEG;\n')

edit('braked stop',
     'float brakedStopDistanceDeg(float speedRevS, int dir, float extraRadS2) {\n'
     '  float c = ((dir > 0) ? cw_c : ccw_c) + extraRadS2;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n',
     'float brakedStopDistanceDeg(float speedRevS, int dir, float extraRadS2) {\n'
     '  float c = ((dir > 0) ? cw_c : ccw_c) + extraRadS2;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (speedRevS < 1e-3f) return 0.0f;\n'
     '  if (wheelGravity.active())\n'
     '    return wheelGravity.stopTravelRad(speedRevS, dir, wheelAngleDeg() * DEG_TO_RAD,\n'
     '        c, b, millis()) * RAD_TO_DEG;\n')

edit('natural decel',
     'float naturalDecelRevS2(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n',
     'float naturalDecelRevS2(float speedRevS, int dir) {\n'
     '  float c = (dir > 0) ? cw_c : ccw_c;\n'
     '  float b = (dir > 0) ? cw_b : ccw_b;\n'
     '  if (wheelGravity.active() && speedRevS >= 1e-3f) {\n'
     '    // The instantaneous value swings through zero once per revolution, so use\n'
     '    // the energy-average over the natural runway, floored at 25% of friction.\n'
     '    float w = speedRevS * IMB_NATURAL_SPEED_MARGIN * TWO_PI;\n'
     '    float travel = wheelGravity.stopTravelRad(speedRevS * IMB_NATURAL_SPEED_MARGIN, dir,\n'
     '        wheelAngleDeg() * DEG_TO_RAD, c, b, millis());\n'
     '    float friction = c + b * speedRevS * TWO_PI;\n'
     '    float average = travel > 1e-3f ? (w * w) / (2.0f * travel) : friction;\n'
     '    return fmaxf(average, 0.25f * friction) / TWO_PI;\n'
     '  }\n')

edit('fit sample angle',
     '  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;\n',
     '  fitSamples[fitSampleCount].omegaRadS = forward * TWO_PI;\n'
     '  fitSamples[fitSampleCount].angleRad = wheelAngleDeg() * DEG_TO_RAD;\n')

edit('fit gravity removal',
     '    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;\n'
     '    if (dropRadS <= 0.0f) continue;          // reversal / speed-up: reject\n',
     '    float dropRadS = fitSamples[i].omegaRadS - fitSamples[j].omegaRadS;\n'
     '    if (wheelGravity.active()) {\n'
     '      // Give back what gravity did to the speed over this pair, so c/b stay\n'
     '      // friction-only.  Segments spanning > 1 rad are left alone: the trapezoid\n'
     '      // is meaningless there and whole revolutions cancel anyway.\n'
     '      for (uint8_t k = i; k < j; ++k) {\n'
     '        float segS = (float)(fitSamples[k + 1].ms - fitSamples[k].ms) / 1000.0f;\n'
     '        float segRad = 0.5f * (fitSamples[k].omegaRadS + fitSamples[k + 1].omegaRadS) * segS;\n'
     '        if (segRad > 1.0f) continue;\n'
     '        dropRadS += 0.5f * segS *\n'
     '            (wheelGravity.forwardAccel(fitSamples[k].angleRad, fitDir) +\n'
     '             wheelGravity.forwardAccel(fitSamples[k + 1].angleRad, fitDir));\n'
     '      }\n'
     '    }\n'
     '    if (dropRadS <= 0.0f) continue;          // reversal / speed-up: reject\n')

edit('abandon capture energy',
     '  fitLifecycle.discard();\n'
     '  if (faultCode == FC_NONE) {\n'
     '    state = ST_SPIN_RELEASED;\n',
     '  fitLifecycle.discard();\n'
     '  releaseCompRevS = 0.0f;  // the motor may have changed the energy: legacy re-push test\n'
     '  if (faultCode == FC_NONE) {\n'
     '    state = ST_SPIN_RELEASED;\n')

edit('release energy',
     '        spin.releaseRevS = speed;\n'
     '        resetFrictionCapture(spinDir);\n',
     '        spin.releaseRevS = speed;\n'
     '        releaseCompRevS = wheelGravity.compensatedRevS(speed, wheelAngleDeg() * DEG_TO_RAD);\n'
     '        resetFrictionCapture(spinDir);\n')

edit('re-push test',
     '      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S) {\n',
     '      // A free coast can out-run its own peak on the heavy side coming down, but\n'
     '      // it can never gain mechanical energy: require both before calling it a push.\n'
     '      bool energyRose = !wheelGravity.active() || releaseCompRevS <= 0.0f ||\n'
     '          wheelGravity.compensatedRevS(speed, wheelAngleDeg() * DEG_TO_RAD) >\n'
     '              releaseCompRevS * 1.02f;\n'
     '      if (speed > spin.peakRevS * 1.02f && speed > SPIN_DETECT_REV_S && energyRose) {\n')

edit('help',
     '    " F  reset friction model to seeds\\n"\n',
     '    " F  reset friction model to seeds\\n"\n'
     '    " g  print imbalance + encoder INL model\\n"\n'
     '    " G<g>,<phiDeg>,<a1>,<b1>,<a2>,<b2>+Enter  set it (at rest; all zero clears)\\n"\n')

edit('serial line',
     '  char command = (char)Serial.read();\n'
     '  if (pwPartyCommandChar(command)) return;   // party commands: t/a/l/w/V<n>\n',
     '  char command = (char)Serial.read();\n'
     '  if (imbalanceLine.open()) {\n'
     '    PwImbalanceConfig parsed;\n'
     '    bool consumed = false, rejected = false;\n'
     '    bool complete = imbalanceLine.feed(command, millis(), &parsed, &consumed, &rejected);\n'
     '    if (rejected) Serial.println(F("# G rejected: need six finite in-range numbers; nothing changed"));\n'
     '    if (complete) applyImbalanceConfig(parsed);\n'
     '    if (consumed) return;\n'
     '  }\n'
     '  if (pwPartyCommandChar(command)) return;   // party commands: t/a/l/w/V<n>\n')

edit('commands',
     "    case 'F':\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\n',
     "    case 'g':\n"
     '      printImbalance();\n'
     '      break;\n'
     "    case 'G':\n"
     '      if (state != ST_IDLE_STOPPED && state != ST_FAULT_LATCHED)\n'
     '        Serial.println(F("# G ignored: controller busy"));\n'
     '      else if (encoderVelocityValid && fabsf(omega) > STILL_REV_S)\n'
     '        Serial.println(F("# G ignored: wheel must be at rest"));\n'
     '      else imbalanceLine.begin(millis());\n'
     '      break;\n'
     "    case 'F':\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\n')

edit('command helpers',
     'void handleSerial() {\n',
     'void printImbalance() {\n'
     '  const PwGravityModel& gm = wheelGravity.model();\n'
     '  const PwInlModel& im = encoderInl.model();\n'
     '  Serial.printf("# imbalance stored=%d g=%.4f rad/s2 phi=%.2f deg rest=%.1f deg | "\n'
     '                "inl a1=%.2f b1=%.2f a2=%.2f b2=%.2f counts | raw=%u inl_now=%.2f\\n",\n'
     '                imbalanceLoaded ? 1 : 0, gm.g, gm.phiRad * RAD_TO_DEG,\n'
     '                wheelGravity.active() ? wheelGravity.restAngleDeg() : 0.0f,\n'
     '                im.a1, im.b1, im.a2, im.b2, (unsigned)lastGoodRaw,\n'
     '                encoderInl.errorCounts(lastGoodRaw));\n'
     '}\n'
     '\n'
     '// Only reached at rest in IDLE/FAULT (the `G` gate), with a validated config.\n'
     'void applyImbalanceConfig(const PwImbalanceConfig& config) {\n'
     '  bool clear = config.gravity.g == 0.0f && config.inl.a1 == 0.0f && config.inl.b1 == 0.0f &&\n'
     '      config.inl.a2 == 0.0f && config.inl.b2 == 0.0f;\n'
     '  bool saved = preferencesAvailable &&\n'
     '      (clear ? pwClearImbalance(preferences) : pwSaveImbalance(preferences, config));\n'
     '  encoderInl.set(config.inl);\n'
     '  wheelGravity.set(config.gravity);\n'
     '  imbalanceLoaded = saved && !clear;\n'
     '  if (encoderPrimed) primeEncoder(lastGoodRaw, micros(), true);\n'
     '  Serial.printf("# imbalance model %s; persisted=%d\\n", clear ? "cleared" : "set", saved ? 1 : 0);\n'
     '  printImbalance();\n'
     '}\n'
     '\n'
     'void handleSerial() {\n')

edit('setup load',
     '  // rawZero/dir_ok/pos_sign are intentionally untouched: these legacy keys\n',
     '  if (preferencesAvailable) {\n'
     '    // Party 2026-09-22: a power blip must not silently disarm the wheel.\n'
     '    takeoverEnabled = preferences.getBool("takeover", false);\n'
     '    Serial.printf("# takeoverEnabled=%d (from NVS)\\n", takeoverEnabled ? 1 : 0);\n'
     '    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);\n'
     '    encoderInl.set(imbalance.inl);\n'
     '    wheelGravity.set(imbalance.gravity);\n'
     '    printImbalance();\n'
     '  }\n'
     '  // rawZero/dir_ok/pos_sign are intentionally untouched: these legacy keys\n')

edit('18 wedges',
     '#define NUM_WEDGES 12\n',
     '#define NUM_WEDGES 18   // 2026-09-21 wheel: 20 deg wedges, labels 0-17 clockwise\n')

edit('dare mask',
     'uint16_t dare_mask = (1 << 1) | (1 << 5);  // wedges 1 and 5 are never targets\n',
     '// Wheel labels are 1-18; firmware indices are label-1 (index 0 = label 1, at the 18|1 line).\n'
     '// Owner dares by LABEL: 3, 8, 13, 16 (8 is the hard one) -> indices 2, 7, 12, 15.\n'
     'uint32_t dare_mask = (1UL << 2) | (1UL << 7) | (1UL << 12) | (1UL << 15);\n')

edit('dare print',
     'Serial.printf("# dare_mask=0x%03X; dare wedges: 1 5\\n", dare_mask);\n',
     'Serial.printf("# dare_mask=0x%05lX; dare indices 2 7 12 15 = labels 3 8 13 16 (label = index+1)\\n", (unsigned long)dare_mask);\n')

edit('z help',
     '" z  set current raw as wedge-0 anchor (wheel at rest, pointer on 11|0 line)\\n"\n',
     '" z  set current raw as wedge-0 anchor (wheel at rest, pointer on 17|0 line)\\n"\n')
edit('probe margin for 20 deg wedges',
     '  return edgeMargin >= 11.0f;\n',
     '  return edgeMargin >= 0.3f * WEDGE_DEG;   // 6 deg at 18 wedges (was 11 with 30 deg wedges)\n')

edit('driver readout in status',
     'void printStatus() {\n',
     'void printDriver() {\n'
     '  // Read-only. GSTAT is clear-on-read: a flag that comes back on the next\n'
     '  // print is being re-asserted by hardware (uv_cp = VM missing/low).\n'
     '  uint32_t drv = driver.DRV_STATUS();\n'
     '  uint8_t gst = (uint8_t)driver.GSTAT();\n'
     '  Serial.printf("# drv DRV_STATUS=%08lX GSTAT=%02X IOIN=%08lX version=%02X usteps=%u healthy=%d\\n",\n'
     '      (unsigned long)drv, (unsigned)gst, (unsigned long)driver.IOIN(), (unsigned)driver.version(),\n'
     '      (unsigned)driver.microsteps(),\n'
     '      (drv != 0 && drv != 0xFFFFFFFFUL && !(drv & 0x1E000000UL) && gst == 0 &&\n'
     '       driver.version() == 0x30 && driver.microsteps() == MICROSTEPS) ? 1 : 0);\n'
     '}\n'
     '\n'
     'void printStatus() {\n')

edit('D command',
     "    case 'F':\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\n',
     "    case 'D':\n"
     '      printDriver();\n'
     '      break;\n'
     "    case 'F':\n"
     '      if (state == ST_IDLE_STOPPED || state == ST_FAULT_LATCHED) {\n')

edit('D help',
     '    " F  reset friction model to seeds\\n"\n',
     '    " F  reset friction model to seeds\\n"\n'
     '    " D  print driver registers (read-only)\\n"\n')


edit('clear GSTAT after config',
     '  driver.iholddelay(0);\n'
     '  driver.TPOWERDOWN(255);\n'
     '}\n',
     '  driver.iholddelay(0);\n'
     '  driver.TPOWERDOWN(255);\n'
     '  // TMC5160 GSTAT is write-to-clear: the power-up reset flag (and any uv_cp)\n'
     '  // stays latched forever otherwise, and captureDriverHealthy() requires 0.\n'
     '  driver.GSTAT(0x07);\n'
     '}\n')


edit('party engagement window',
     'const float CAPTURE_MAX_CMD_REV_S     = 0.20f;  // bounded integration, at most 640 Hz\n'
     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.20f;  // faster spins coast into the tested window\n'
     'const uint32_t DECEL_CEILING_SPS2     = 320;    // diagnostic envelope: total profile deceleration\n'
     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 320;    // no aggressive fallback\n',
     '// 2026-09-21 party policy: engage in the 0.72 rev/s window like the certified\n'
     '// 24in build, not a last-second grab at 0.20 (coupling slip is fixed).\n'
     'const float CAPTURE_MAX_CMD_REV_S     = 0.38f;  // 0.95 x engage ceiling\n'
     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.40f;  // brake arcs must fit the 160 deg uphill half\n'
     'const uint32_t DECEL_CEILING_SPS2     = 600;    // uphill arcs only: gravity supplies most of this\n'
     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 600;    // pass-3 braking to nearest safe interior\n'
     'const uint32_t SHADOW_DECEL_MAX_SPS2  = 1000;   // pass-2 shadow capture: plan ~= natural decel, motor only corrects\n')

edit('plan hz ceiling',
     '      hz < 40 || hz > 640 || !isfinite(remaining) || remaining < 7.0f ||\n',
     '      hz < 40 || hz > 2400 || !isfinite(remaining) || remaining < 7.0f ||\n')

edit('persistent hold current',
     'const uint16_t CUR_HOLD1_MA     = 550;  // fade...\n',
     'const uint16_t CUR_HOLD1_MA     = 800;  // continuous hold; balanced wheel needs little, driver stays cool\n')

edit('persistent hold state',
     '      // Fade the hold torque so release is imperceptible, then float.\n'
     '      uint32_t age = nowMs - stateEnteredMs;\n'
     '      if (currentStage == CS_HOLD1 && age >= HOLD1_MS) setCurrentStage(CS_HOLD2);\n'
     '      else if (currentStage == CS_HOLD2 && age >= HOLD1_MS + HOLD2_MS) {\n'
     '        driverFreewheel();\n'
     '        // Between-spins driver health check (wheel at rest, timing harmless).\n'
     '        checkTmcUartOrFault();\n'
     '        if (state == ST_FAULT_LATCHED) break;\n'
     '        captureCycle.holdReleased(faultCode == FC_NONE && currentStage == CS_FREEWHEEL &&\n'
     '            digitalRead(PIN_EN) == HIGH && stepper && !stepper->isRunning());\n'
     '        state = ST_IDLE_STOPPED;\n'
     '        stateEnteredMs = nowMs;\n'
     '        break;\n'
     '      }\n'
     '      // A new hand motion during the fade releases the wheel immediately.\n'
     '      if (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S) {\n',
     '      // Persistent hold (2026-09-21): the unbalanced wheel rolls off any landing\n'
     '      // on a slope once freewheeled, so holding torque stays on until the next\n'
     '      // guest push. Release on sustained motion OR on a deflection from the\n'
     '      // settled hold angle (a hand pushing against the field), before pole slip.\n'
     '      uint32_t age = nowMs - stateEnteredMs;\n'
     '      if (age >= 300 && !holdAnchorValid && encoderPositionFresh()) {\n'
     '        holdAnchorDeg = wheelAngleDeg(); holdAnchorValid = true;\n'
     '      }\n'
     '      bool pushed = false;\n'
     '      if (holdAnchorValid && encoderPositionFresh()) {\n'
     '        float dev = fabsf(wheelAngleDeg() - holdAnchorDeg);\n'
     '        if (dev > 180.0f) dev = 360.0f - dev;\n'
     '        if (dev >= HOLD_RELEASE_DEFLECT_DEG) {\n'
     '          if (holdDeflectSinceMs == 0) holdDeflectSinceMs = nowMs;\n'
     '          else if (nowMs - holdDeflectSinceMs >= HOLD_RELEASE_CONFIRM_MS) pushed = true;\n'
     '        } else holdDeflectSinceMs = 0;\n'
     '      }\n'
     '      if (pushed) Serial.printf("# HOLD released: pushed %.2f deg off anchor after %lu ms\\n",\n'
     '                                fabsf(wheelAngleDeg() - holdAnchorDeg), (unsigned long)age);\n'
     '      if (pushed || (encoderMotionReady() && fabsf(omega) >= SPIN_DETECT_REV_S)) {\n'
     '        holdAnchorValid = false; holdDeflectSinceMs = 0;\n')

edit('persistent hold globals',
     'const uint16_t HOLD1_MS               = 1500;\n',
     'const uint16_t HOLD1_MS               = 1500;   // unused: hold is persistent\n'
     'const float HOLD_RELEASE_DEFLECT_DEG  = 1.2f;   // below the 1.8 deg pole-slip load angle\n'
     'const uint16_t HOLD_RELEASE_CONFIRM_MS = 30;\n'
     'float holdAnchorDeg = 0.0f;\n'
     'bool holdAnchorValid = false;\n'
     'uint32_t holdDeflectSinceMs = 0;\n')

edit('hold entry resets anchor',
     '  setCurrentStage(CS_HOLD1);\n',
     '  holdAnchorValid = false; holdDeflectSinceMs = 0;\n'
     '  setCurrentStage(CS_HOLD1);\n')



HEADER_EDITS = [
 ('arm hz ceiling', 'started = hz >= 40 && hz <= 640; // diagnostic .20 rps ceiling, never > spin-up',
                    'started = hz >= 40 && hz <= 2400; // party window: up to 0.72 rev/s at 3200 usteps/rev'),
 ('arm wheel ceiling', '        forwardRevS > 0.20f) return reject(ENCODER);',
                       '        forwardRevS > 0.75f) return reject(ENCODER);'),
 ('arm pulse window', '    if (position < previousPosition || total < 0 || total > 128)',
                      '    if (position < previousPosition || total < 0 || total > 512)'),
 ('overspeed ceiling', '  return isfinite(wheelRevS) && fabsf(wheelRevS) > 0.30f;',
                       '  return isfinite(wheelRevS) && fabsf(wheelRevS) > 0.80f;'),
]

edit('uphill gate helper',
     'bool tryReserveTarget(uint32_t nowMs) {\n',
     '// Gravity beats friction 3:1 on this wheel. Braking on the DOWNHILL half means\n'
     '// the motor must fight gravity plus the plan (pole slip / overspeed, 17:52 fault);\n'
     '// on the UPHILL half gravity is the brake and the motor only shapes it. The\n'
     '// whole brake arc [now, now + runway] must sit inside the uphill half\n'
     '// (rest -> crest in the direction of travel), with margin at both ends.\n'
     'const float UPHILL_MARGIN_DEG = 10.0f;\n'
     'bool brakeArcUphill(int dir, float curAngle, float runwayDeg) {\n'
     '  if (!wheelGravity.active()) return true;\n'
     '  float s0 = fmodf((float)dir * (curAngle - wheelGravity.restAngleDeg()), 360.0f);\n'
     '  if (s0 < 0.0f) s0 += 360.0f;\n'
     '  return s0 >= UPHILL_MARGIN_DEG && s0 + runwayDeg <= 180.0f - UPHILL_MARGIN_DEG;\n'
     '}\n'
     '\n'
     'bool tryReserveTarget(uint32_t nowMs) {\n')

edit('uphill gate',
     '  TargetChoice choice = chooseSafeTarget(spinDir, wheelAngleDeg(), speed);\n'
     '  if (!choice.found) return false;\n',
     '  TargetChoice choice = chooseSafeTarget(spinDir, wheelAngleDeg(), speed);\n'
     '  if (!choice.found) return false;\n'
     '  if (!brakeArcUphill(spinDir, wheelAngleDeg(), choice.runwayDeg)) return false;  // try again next tick\n')

edit('friction seeds from 2026-09-21 coasts',
     'const PwFrictionModel FRICTION_SEED = {0.55f, 0.28f, 0}; // existing v2 seeds, still require calibration\n',
     'const PwFrictionModel FRICTION_SEED = {0.17f, 0.05f, 0}; // 2026-09-21 hand-spin fits (<0.7 rev/s), gravity separated\n')


edit('capture/brake current 2800',
     'const uint16_t CUR_CAPTURE_MA   = 2200;\n'
     'const uint16_t CUR_BRAKE_MA     = 2200; // retain capture torque through braking/settling\n',
     'const uint16_t CUR_CAPTURE_MA   = 2200; // 2026-09-22: wheel balanced; 2240 already had margin unbalanced. Less regen/heat.\n'
     'const uint16_t CUR_BRAKE_MA     = 2200; // retain capture torque through braking/settling\n')


edit('recovery state enum',
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

edit('shadow min',
     '  assistMin = fmaxf(assistMin, latencyDeg + pwBrakeDistanceDeg(\n'
     '      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),\n'
     '      (float)ASSIST_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV));\n',
     '  assistMin = fmaxf(assistMin, latencyDeg + pwBrakeDistanceDeg(\n'
     '      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),\n'
     '      (float)ASSIST_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV));\n'
     '  float shadowMin = latencyDeg + pwBrakeDistanceDeg(\n'
     '      fminf(TRAIL_FRACTION * speedRevS, CAPTURE_MAX_CMD_REV_S),\n'
     '      (float)SHADOW_DECEL_MAX_SPS2 / WHEEL_USTEPS_PER_REV);\n')

edit('shadow pass uses its own cap',
     '  if (!out.found && naturalDeg > 10.0f && naturalDeg - 2.0f >= assistMin) {\n',
     '  if (!out.found && naturalDeg > 10.0f && naturalDeg - 2.0f >= shadowMin) {\n')

edit('shadow pass cap',
     '      out.runwayDeg = naturalDeg - 2.0f;\n'
     '      out.decelCapSps2 = ASSIST_DECEL_MAX_SPS2;\n'
     '      out.quality = 3;\n',
     '      out.runwayDeg = naturalDeg - 6.0f;   // 2026-09-22: 2 deg was inside prediction noise (plan refused)\n'
     '      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;\n'
     '      out.quality = 3;\n')


edit('persist takeover toggle',
     "    case 'e':\n"
     '      takeoverEnabled = !takeoverEnabled;\n'
     '      Serial.printf("# takeoverEnabled=%d\\n", takeoverEnabled ? 1 : 0);\n',
     "    case 'e':\n"
     '      takeoverEnabled = !takeoverEnabled;\n'
     '      if (preferencesAvailable) preferences.putBool("takeover", takeoverEnabled);\n'
     '      Serial.printf("# takeoverEnabled=%d (persisted)\\n", takeoverEnabled ? 1 : 0);\n')

edit('plan natural tolerance',
     '  if (!isfinite(naturalRemaining) || remaining > naturalRemaining) return false;\n',
     '  if (!isfinite(naturalRemaining) || remaining > naturalRemaining * 1.10f + 5.0f) return false;  // shadow targets sit at natural-6: a 2.5% speed drop while arming moves natural by 5%\n')


edit('auto-reconfig after driver power blip',
     'bool captureDriverHealthy() {\n'
     '  const uint32_t drv = driver.DRV_STATUS();\n'
     '  const uint8_t gst = (uint8_t)driver.GSTAT();\n',
     'bool captureDriverHealthy() {\n'
     '  uint32_t drv = driver.DRV_STATUS();\n'
     '  uint8_t gst = (uint8_t)driver.GSTAT();\n'
     '  // 2026-09-22: a supply blip resets the driver (GSTAT reset=1, config lost).\n'
     '  // With outputs OFF that is recoverable: re-apply the config and re-read,\n'
     '  // instead of abandoning every capture until someone types r.\n'
     '  if (gst == 0x01 && driver.version() == 0x30 && digitalRead(PIN_EN) == HIGH) {\n'
     '    driverConfig();\n'
     '    Serial.println(F("# driver reset flag seen with outputs off: config re-applied"));\n'
     '    drv = driver.DRV_STATUS();\n'
     '    gst = (uint8_t)driver.GSTAT();\n'
     '  }\n')


edit('hold entry force-stops pulses',
     '  setCurrentStage(CS_HOLD1);\n',
     '  if (stepper) stepper->forceStop();   // 2026-09-22: no pulse generator activity in hold, ever\n'
     '  setCurrentStage(CS_HOLD1);\n')

edit('hold tick force-stops pulses',
     '    case ST_SOFT_HOLD: {\n'
     '      if (!controlDriverSafe(nowMs)) break;\n',
     '    case ST_SOFT_HOLD: {\n'
     '      if (!controlDriverSafe(nowMs)) break;\n'
     '      if (stepper && stepper->isRunning()) {\n'
     '        stepper->forceStop();\n'
     '        Serial.println(F("# HOLD: pulse generator was running; force-stopped"));\n'
     '      }\n')


def main():
    ino = SRC / 'prize_wheel_gpt.ino'
    raw = ino.read_bytes()
    crlf = b'\r\n' in raw
    text = raw.decode('utf-8').replace('\r\n', '\n')
    problems = []
    for name, old, new in EDITS:
        count = text.count(old)
        if count != 1:
            problems.append('%s: anchor found %d times' % (name, count))
    arm = (SRC / 'pw_capture_arm.h').read_bytes().decode('utf-8').replace('\r\n', '\n')
    for name, old, new in HEADER_EDITS:
        if arm.count(old) != 1:
            problems.append('%s: anchor found %d times' % (name, arm.count(old)))
    if problems:
        print('\n'.join(problems))
        return 1
    for name, old, new in EDITS:
        text = text.replace(old, new)
    header = OUT / 'pw_imbalance.h'
    if not header.exists():
        print('missing', header)
        return 1
    if DST.exists():
        shutil.rmtree(DST)
    shutil.copytree(SRC, DST, ignore=shutil.ignore_patterns('build'))
    out = text.replace('\n', '\r\n') if crlf else text
    (DST / 'prize_wheel_gpt.ino').write_bytes(out.encode('utf-8'))
    shutil.copy2(header, DST / 'pw_imbalance.h')
    for name, old, new in HEADER_EDITS:
        arm = arm.replace(old, new)
    (DST / 'pw_capture_arm.h').write_bytes((arm.replace('\n', '\r\n') if crlf else arm).encode('utf-8'))
    lease = (SRC / 'pw_capture_lease.h').read_bytes().decode('utf-8').replace('\r\n', '\n')
    assert lease.count('static constexpr uint64_t ARM_US = 150000;') == 1
    lease = lease.replace('static constexpr uint64_t ARM_US = 150000;', 'static constexpr uint64_t ARM_US = 400000;  // 2026-09-22: 150 ms expired on a loop stall; unpowered stepping while proving sync is harmless')
    (DST / 'pw_capture_lease.h').write_bytes((lease.replace('\n', '\r\n') if crlf else lease).encode('utf-8'))
    manifest = {str(p.relative_to(OUT)).replace('\\', '/'): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in sorted(DST.iterdir()) if p.is_file()}
    (OUT / 'source_manifest.json').write_text(json.dumps(manifest, indent=2))
    print('applied %d edits; crlf=%s; files=%d' % (len(EDITS), crlf, len(manifest)))
    return 0

if __name__ == '__main__':
    sys.exit(main())
