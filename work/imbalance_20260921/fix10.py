p=open('apply_imbalance.py').read()
p=p.replace("'const uint32_t DECEL_CEILING_SPS2     = 400;    // total profile decel; motor share = this - friction -/+ gravity\\n'\n     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 550;    // a=610 slipped poles at 2.8 A (18:09)\\n')",
"'const uint32_t DECEL_CEILING_SPS2     = 600;    // uphill arcs only: gravity supplies most of this\\n'\n     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 600;    // pass-3 braking to nearest safe interior\\n'\n     'const uint32_t SHADOW_DECEL_MAX_SPS2  = 1000;   // pass-2 shadow capture: plan ~= natural decel, motor only corrects\\n')")
j=p.index("def main():")
new=r'''edit('shadow min',
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
     '      out.runwayDeg = naturalDeg - 2.0f;\n'
     '      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;\n'
     '      out.quality = 3;\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
