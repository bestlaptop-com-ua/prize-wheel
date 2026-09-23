"""Flash 4 (2026-09-23 evening): vary the catch speed per spin so identical gentle
pushes stop on different wedges. Exact-once anchors; aborts if any anchor drifts."""
from pathlib import Path
p = Path(__file__).resolve().parent / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
s = p.read_text(encoding='utf-8')

def once(old, new):
    global s
    n = s.count(old)
    assert n == 1, f'anchor count {n}: {old[:70]!r}'
    s = s.replace(old, new)

# 1. per-spin catch speed set + state, right after the fixed ceiling
once('''const float CAPTURE_MAX_WHEEL_REV_S   = 0.40f;  // brake arcs must fit the 160 deg uphill half
''', '''const float CAPTURE_MAX_WHEEL_REV_S   = 0.40f;  // brake arcs must fit the 160 deg uphill half
// 2026-09-23 flash 4 (owner: "Now it stops on 11 constantly" / "I don't insist
// on randomness. But they should differ"): gentle pushes all reached the fixed
// 0.40 catch speed at the same angle, and the planner then sent them the same
// distance (9 of 16 spins on label 11).  The catch speed now changes every spin
// and never repeats the previous value.  All values are <= the 0.40 ceiling and
// inside the band weak spins were already caught at on 9/23 (tkSpeed 0.11-0.40);
// targeting, dare checks, braking and carry are unchanged.
const float ENGAGE_SET_REV_S[] = {0.40f, 0.35f, 0.30f, 0.25f};
const uint8_t ENGAGE_SET_N = sizeof(ENGAGE_SET_REV_S) / sizeof(ENGAGE_SET_REV_S[0]);
uint8_t engageIdx = 0;
float spinEngageRevS = CAPTURE_MAX_WHEEL_REV_S;   // this spin's catch speed
''')

# 2. pick the next catch speed at every spin start (never the previous one)
once('''  ++spinCounter;
  memset(&spin, 0, sizeof(spin));
  spin.number = spinCounter;
''', '''  ++spinCounter;
  memset(&spin, 0, sizeof(spin));
  spin.number = spinCounter;
  engageIdx = (uint8_t)((engageIdx + 1 + (uint8_t)random((long)(ENGAGE_SET_N - 1))) % ENGAGE_SET_N);
  spinEngageRevS = ENGAGE_SET_REV_S[engageIdx];
''')

once('''  Serial.printf("SPIN#%lu START dir=%+d omega=%.3f wedge=%d\\n",
                (unsigned long)spin.number, spinDir, omega, currentWedge());
''', '''  Serial.printf("SPIN#%lu START dir=%+d omega=%.3f wedge=%d engage=%.2f\\n",
                (unsigned long)spin.number, spinDir, omega, currentWedge(), spinEngageRevS);
''')

# 3. reserve only at/below this spin's catch speed
once('''  if (!isfinite(speed) || speed < 0.02f || speed > CAPTURE_MAX_WHEEL_REV_S) return false;
''', '''  if (!isfinite(speed) || speed < 0.02f || speed > spinEngageRevS) return false;  // flash 4: per-spin catch speed
''')

# 4. banner
once('''# build: party3-20260923 (flash 3: 2.7 A braking, holds through settle drags, SLIP log) on party-20260923''',
     '''# build: party4-20260923 (flash 4: catch speed varies per spin 0.40/0.35/0.30/0.25) on party3-20260923 (flash 3: 2.7 A braking, holds through settle drags, SLIP log) on party-20260923''')

p.write_text(s, encoding='utf-8')
print('applied 5 edits')

# 6. review SHOULD-FIX 1: keep the friction fit on the same inputs as flash 3
#    (samples only above the fixed 0.40 ceiling; slower coast is gravity-dominated
#    and would now be sampled on 3 of 4 spins because the catch comes later).
s = p.read_text(encoding='utf-8')
once('''  float forward = omega * (float)fitDir;
  if (forward < FIT_MIN_SPEED_REV_S || forward > FIT_MAX_SPEED_REV_S) return;
''', '''  float forward = omega * (float)fitDir;
  if (forward < FIT_MIN_SPEED_REV_S || forward > FIT_MAX_SPEED_REV_S) return;
  if (forward < CAPTURE_MAX_WHEEL_REV_S) return;  // flash 4: same fit inputs as flash 3
''')
p.write_text(s, encoding='utf-8')
print('applied fit guard')
