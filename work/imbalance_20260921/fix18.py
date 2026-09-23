p=open('apply_imbalance.py').read()
p=p.replace("'  if (!isfinite(naturalRemaining) || remaining > naturalRemaining * 1.03f + 3.0f) return false;\\n'",
"'  if (!isfinite(naturalRemaining) || remaining > naturalRemaining * 1.10f + 5.0f) return false;  // shadow targets sit at natural-6: a 2.5% speed drop while arming moves natural by 5%\\n'")
open('apply_imbalance.py','w').write(p)
