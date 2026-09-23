p=open('apply_imbalance.py').read()
i=p.index("edit('hold entry force-stops pulses'"); j=p.index("def main():")
p=p[:i]+p[j:]
j=p.index("def main():")
new=r'''edit('hold entry force-stops pulses',
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


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
