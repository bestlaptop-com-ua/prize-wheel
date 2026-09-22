p=open('apply_imbalance.py').read()
i=p.index("edit('shadow runway margin'"); j=p.index("edit('plan natural tolerance'")
p=p[:i]+p[j:]
p=p.replace("     '      out.runwayDeg = naturalDeg - 2.0f;\\n'\n     '      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;\\n'\n     '      out.quality = 3;\\n')",
"     '      out.runwayDeg = naturalDeg - 6.0f;   // 2026-09-22: 2 deg was inside prediction noise (plan refused)\\n'\n     '      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;\\n'\n     '      out.quality = 3;\\n')")
open('apply_imbalance.py','w').write(p)
