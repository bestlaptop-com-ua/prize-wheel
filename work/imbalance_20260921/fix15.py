p=open('apply_imbalance.py').read()
j=p.index("def main():")
new=r'''edit('shadow runway margin',
     '      out.runwayDeg = naturalDeg - 2.0f;\n'
     '      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;\n',
     '      out.runwayDeg = naturalDeg - 6.0f;   // 2026-09-22: 2 deg was inside prediction noise (plan refused)\n'
     '      out.decelCapSps2 = SHADOW_DECEL_MAX_SPS2;\n')

edit('plan natural tolerance',
     '  if (!isfinite(naturalRemaining) || remaining > naturalRemaining) return false;\n',
     '  if (!isfinite(naturalRemaining) || remaining > naturalRemaining * 1.03f + 3.0f) return false;\n')


'''
p=p[:j]+new+p[j:]
open('apply_imbalance.py','w').write(p)
