p=open('apply_imbalance.py').read()
i=p.index("edit('lower decel caps after 18:09 pole slip'"); j=p.index("def main():")
p=p[:i]+p[j:]
p=p.replace("'const uint32_t DECEL_CEILING_SPS2     = 500;    // total profile decel; motor share = this - friction -/+ gravity\\n'\n     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 800;\\n')", "'const uint32_t DECEL_CEILING_SPS2     = 400;    // total profile decel; motor share = this - friction -/+ gravity\\n'\n     'const uint32_t ASSIST_DECEL_MAX_SPS2  = 550;    // a=610 slipped poles at 2.8 A (18:09)\\n')")
open('apply_imbalance.py','w').write(p)
