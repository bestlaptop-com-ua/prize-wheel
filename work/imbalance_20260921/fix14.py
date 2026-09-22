p=open('apply_imbalance.py').read()
i=p.index("edit('restore takeover at boot'"); j=p.index("def main():")
p=p[:i]+p[j:]
p=p.replace("     '  if (preferencesAvailable) {\\n'\n     '    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);\\n'\n     '    encoderInl.set(imbalance.inl);\\n'",
"     '  if (preferencesAvailable) {\\n'\n     '    // Party 2026-09-22: a power blip must not silently disarm the wheel.\\n'\n     '    takeoverEnabled = preferences.getBool(\"takeover\", false);\\n'\n     '    Serial.printf(\"# takeoverEnabled=%d (from NVS)\\\\n\", takeoverEnabled ? 1 : 0);\\n'\n     '    PwImbalanceConfig imbalance = pwLoadImbalance(preferences, &imbalanceLoaded);\\n'\n     '    encoderInl.set(imbalance.inl);\\n'")
open('apply_imbalance.py','w').write(p)
