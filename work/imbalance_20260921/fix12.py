p=open('apply_imbalance.py').read()
p=p.replace("'uint32_t dare_mask = (1UL << 3) | (1UL << 8) | (1UL << 13) | (1UL << 16);  // never targets; 8 is the hard one\\n'",
"'// Wheel labels are 1-18; firmware indices are label-1 (index 0 = label 1, at the 18|1 line).\\n'\n     '// Owner dares by LABEL: 3, 8, 13, 16 (8 is the hard one) -> indices 2, 7, 12, 15.\\n'\n     'uint32_t dare_mask = (1UL << 2) | (1UL << 7) | (1UL << 12) | (1UL << 15);\\n'")
p=p.replace("'Serial.printf(\"# dare_mask=0x%05lX; dare wedges: 3 8 13 16\\\\n\", (unsigned long)dare_mask);\\n'",
"'Serial.printf(\"# dare_mask=0x%05lX; dare indices 2 7 12 15 = labels 3 8 13 16 (label = index+1)\\\\n\", (unsigned long)dare_mask);\\n'")
p=p.replace("'const uint16_t CUR_CAPTURE_MA   = 2240; // HEADROOM TEST 2026-09-22: 2800 - 20%\\n'\n     'const uint16_t CUR_BRAKE_MA     = 2240; // retain capture torque through braking/settling\\n')",
"'const uint16_t CUR_CAPTURE_MA   = 2800; // 2026-09-22: 2240 proved 20% headroom; party runs at 2800\\n'\n     'const uint16_t CUR_BRAKE_MA     = 2800; // retain capture torque through braking/settling\\n')")
p=p.replace("HEADROOM-2240mA; based on", "PARTY; based on")
open('apply_imbalance.py','w').write(p)
