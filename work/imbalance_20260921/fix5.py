p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('uphill gate helper',
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

edit('engage window for uphill arcs',
     'const float CAPTURE_MAX_CMD_REV_S     = 0.68f;  // 0.95 x engage ceiling\n'
     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.72f;\n',
     'const float CAPTURE_MAX_CMD_REV_S     = 0.38f;  // 0.95 x engage ceiling\n'
     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.40f;  // brake arcs must fit the 160 deg uphill half\n')

edit('friction seeds from 2026-09-21 coasts',
     'const PwFrictionModel FRICTION_SEED = {0.55f, 0.28f, 0}; // existing v2 seeds, still require calibration\n',
     'const PwFrictionModel FRICTION_SEED = {0.17f, 0.05f, 0}; // 2026-09-21 hand-spin fits (<0.7 rev/s), gravity separated\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
