from pathlib import Path
import csv,json
p=Path(__file__).parent
rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader((p/'brake2200_spin1.csv').open())]
f=next(r for r in rows if r['state']==5);t=f['done_us']
near=[r for r in rows if -10000<=r['done_us']-t<=100000]
runs=[];a=near[0];b=a
for r in near[1:]:
 if r['raw']==b['raw']:b=r
 else:
  if b['done_us']-a['done_us']>=3000:runs.append((round((a['done_us']-t)/1000,3),round((b['done_us']-t)/1000,3),b['raw'],b['counts']))
  a=b=r
if b['done_us']-a['done_us']>=3000:runs.append((round((a['done_us']-t)/1000,3),round((b['done_us']-t)/1000,3),b['raw'],b['counts']))
print('constant raw runs >=3ms from precharge:',runs)
print('first 35ms samples: ms, raw, delta, counts, filtered, window, current, steps')
for r in near:
 if 0<=r['done_us']-t<=35000:print(round((r['done_us']-t)/1000,3),r['raw'],r['delta'],r['counts'],r['omega_mrev'],r['window_mrev'],r['current_ma'],r['step_count'])