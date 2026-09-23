from pathlib import Path
p=Path(r'C:\Users\Mill\Desktop\prize-wheel\work\certified_v2_20260922\prize_wheel_gpt\prize_wheel_gpt.ino')
raw=p.read_bytes(); crlf=b'\r\n' in raw; t=raw.decode().replace('\r\n','\n')
E=[('const uint32_t DECEL_CEILING_SPS2     = 650;    // natural-motion decel ceiling\n',
    'const uint32_t DECEL_CEILING_SPS2     = 350;    // 2026-09-22: direct-drive NEMA23 slips above ~400 on this wheel (618 slipped)\n'),
   ('const uint32_t ASSIST_DECEL_MAX_SPS2  = 1100;   // weak-spin nearest-target cap\n',
    'const uint32_t ASSIST_DECEL_MAX_SPS2  = 450;    // weak-spin nearest-target cap\n'),
   ('  cw_c = 0.55f; cw_b = 0.28f;\n  ccw_c = 0.55f; ccw_b = 0.28f;\n',
    '  cw_c = 0.25f; cw_b = 0.05f;   // 2026-09-22 measured on the balanced 36in wheel (hand-spin fits)\n  ccw_c = 0.25f; ccw_b = 0.05f;\n')]
for a,b in E:
    assert t.count(a)==1, a[:60]
    t=t.replace(a,b)
p.write_bytes((t.replace('\n','\r\n') if crlf else t).encode()); print('ok')
