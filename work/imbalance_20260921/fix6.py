p=open('apply_imbalance.py').read()
i=p.index("edit('engage window for uphill arcs'"); j=p.index("edit('friction seeds")
p=p[:i]+p[j:]
p=p.replace("'const float CAPTURE_MAX_CMD_REV_S     = 0.68f;  // 0.95 x engage ceiling\\n'\n     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.72f;\\n'", "'const float CAPTURE_MAX_CMD_REV_S     = 0.38f;  // 0.95 x engage ceiling\\n'\n     'const float CAPTURE_MAX_WHEEL_REV_S   = 0.40f;  // brake arcs must fit the 160 deg uphill half\\n'")
open('apply_imbalance.py','w').write(p)
