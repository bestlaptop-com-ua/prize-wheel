p=open('apply_imbalance.py').read()
p=p.replace("'const uint16_t CUR_CAPTURE_MA   = 2800; // 2026-09-21: owner asked for more torque margin (motor 3 A rated)\\n'\n     'const uint16_t CUR_BRAKE_MA     = 2800; // retain capture torque through braking/settling\\n')",
"'const uint16_t CUR_CAPTURE_MA   = 2240; // HEADROOM TEST 2026-09-22: 2800 - 20%\\n'\n     'const uint16_t CUR_BRAKE_MA     = 2240; // retain capture torque through braking/settling\\n')")
p=p.replace("'# build: imbalance-model-20260921; based on plywood-capture-review-20260920; takeover defaults OFF')", "'# build: imbalance-model-20260921 HEADROOM-2240mA; based on plywood-capture-review-20260920; takeover defaults OFF')")
open('apply_imbalance.py','w').write(p)
