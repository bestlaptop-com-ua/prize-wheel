from pathlib import Path
import shutil,difflib
p=Path(__file__).parent;src=p.parent/'prize_wheel_gpt/prize_wheel_gpt.ino';old=p/'before-gentle320'/src.name
candidate=p/'gentle320_candidate.ino';shutil.copy2(src,candidate)
a=old.read_text();b=src.read_text()
print(''.join(difflib.unified_diff(a.splitlines(True),b.splitlines(True),fromfile='deployed brake2200',tofile='UNDEPLOYED gentle320')))
assert a.replace('= 650;    // natural-motion decel ceiling','= 320;    // direct drive: 0.10 rev/s^2 maximum').replace('= 1100;   // weak-spin nearest-target cap','= 320;    // fallbacks must obey the same gentle limit').replace('v2-brake2200-20260918; smooth ramp; brake2200; hold1650; tracking-loss latch','v2-gentle320-20260918; decel320; brake2200; hold1650; tracking-loss latch')==b
shutil.copy2(old,src)
(p/'gentle320_DO_NOT_UPLOAD.txt').write_text('Not deployable with existing friction seeds: no feasible target across 0.02..0.72rev/s at320sps2. Await measured free coast and planning correction. Main source restored to deployed brake2200. Candidate preserved separately.')