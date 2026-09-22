p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('persist takeover toggle',
     "    case 'e':\n"
     '      takeoverEnabled = !takeoverEnabled;\n'
     '      Serial.printf("# takeoverEnabled=%d\\n", takeoverEnabled ? 1 : 0);\n',
     "    case 'e':\n"
     '      takeoverEnabled = !takeoverEnabled;\n'
     '      if (preferencesAvailable) preferences.putBool("takeover", takeoverEnabled);\n'
     '      Serial.printf("# takeoverEnabled=%d (persisted)\\n", takeoverEnabled ? 1 : 0);\n')

edit('restore takeover at boot',
     '  if (preferencesAvailable) {\n'
     '    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);\n',
     '  if (preferencesAvailable) {\n'
     '    // Party 2026-09-22: a power blip must not silently disarm the wheel.\n'
     '    takeoverEnabled = preferences.getBool("takeover", false);\n'
     '    Serial.printf("# takeoverEnabled=%d (from NVS)\\n", takeoverEnabled ? 1 : 0);\n'
     '    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
