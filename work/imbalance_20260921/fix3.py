p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('party engagement window',
     'const float CAPTURE_MAX_CMD_REV_S     = 0.20f;  // bounded integration, at most 640 Hz\n'
     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.20f;  // faster spins coast into the tested window\n'
     'const uint32_t DECEL_CEILING_SPS2     = 320;    // diagnostic envelope: total profile deceleration\n'
     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 320;    // no aggressive fallback\n',
     '// 2026-09-21 party policy: engage in the 0.72 rev/s window like the certified\n'
     '// 24in build, not a last-second grab at 0.20 (coupling slip is fixed).\n'
     'const float CAPTURE_MAX_CMD_REV_S     = 0.68f;  // 0.95 x engage ceiling\n'
     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.72f;\n'
     'const uint32_t DECEL_CEILING_SPS2     = 500;    // total profile decel; motor share = this - friction -/+ gravity\n'
     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 800;\n')

edit('plan hz ceiling',
     '      hz < 40 || hz > 640 || !isfinite(remaining) || remaining < 7.0f ||\n',
     '      hz < 40 || hz > 2400 || !isfinite(remaining) || remaining < 7.0f ||\n')

edit('persistent hold current',
     'const uint16_t CUR_HOLD1_MA     = 550;  // fade...\n',
     'const uint16_t CUR_HOLD1_MA     = 1650; // continuous hold: gravity (0.57 rad/s2) beats friction 3:1\n')

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


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
