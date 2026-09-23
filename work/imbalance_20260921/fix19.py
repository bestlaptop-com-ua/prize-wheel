p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('hold entry force-stops pulses',
     '  holdAnchorValid = false; holdDeflectSinceMs = 0;\n'
     '  setCurrentStage(CS_HOLD1);\n',
     '  holdAnchorValid = false; holdDeflectSinceMs = 0;\n'
     '  if (stepper) stepper->forceStop();   // 2026-09-22: no pulse generator activity in hold, ever\n'
     '  setCurrentStage(CS_HOLD1);\n')

edit('hold tick force-stops pulses',
     '      // Persistent hold (2026-09-21): the unbalanced wheel rolls off any landing\n',
     '      if (stepper && stepper->isRunning()) {\n'
     '        stepper->forceStop();\n'
     '        Serial.println(F("# HOLD: pulse generator was running; force-stopped"));\n'
     '      }\n'
     '      // Persistent hold (2026-09-21): the unbalanced wheel rolls off any landing\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
