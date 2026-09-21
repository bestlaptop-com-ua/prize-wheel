from pathlib import Path
import csv,re,collections,json
p=Path(__file__).parent
text=(p/'brake2200_serial.log').read_text();blocks=text.split('# DIAG columns:')[1:]
print('blocks',len(blocks))
if not blocks:raise SystemExit()
block=blocks[0];header=block.splitlines()[0].strip().split(',');rows=[];bad=0
for line in block.splitlines()[1:]:
 if not line.startswith('D,'):continue
 vals=line.strip().split(',')[1:]
 try:
  if len(vals)!=len(header):raise ValueError()
  rows.append({k:int(v,16 if k.endswith('_hex') else 10) for k,v in zip(header,vals)})
 except ValueError:bad+=1
end=re.search(r'# DIAG n=(\d+)',block)
print('rows',len(rows),'complete',bool(end),'bad',bad,'states',dict(collections.Counter(r['state'] for r in rows)))
if not end:raise SystemExit()
assert len(rows)==int(end.group(1)) and bad==0
(p/'brake2200_spin1_rattle.log').write_text(text)
with (p/'brake2200_spin1.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=header);w.writeheader();w.writerows(rows)
first=next(r for r in rows if r['state']==5);start=first['done_us'];post=[r for r in rows if r['done_us']>=start-300000]
print('gstat',dict(collections.Counter(hex(r['gstat_hex']) for r in post)))
print('thermal_short',sum(bool(r['drv_status_hex']&0x1e000000) for r in post))
print('flags',dict(collections.Counter(hex(r['flags_hex']) for r in post)))
lastkey=None
for r in post:
 key=(r['state'],r['stage'])
 if key!=lastkey:
  print('transition',json.dumps(dict(ms=round((r['done_us']-start)/1000,1),state=r['state'],stage=r['stage'],encoder=r['omega_mrev']/1000,window=r['window_mrev']/1000,fas=r['fas_mrev']/1000,steps=r['step_count'],counts=r['counts'],current=r['current_ma'],remain=r['remain_ddeg']/10)));lastkey=key
print('20/100ms intervals: ms, raw encoderRPS, pulseRPS, filtered, window, FAS, deltaAngle, current')
last=post[0]
for r in post:
 interval=20000 if r['done_us']<start+350000 else 100000
 if r['done_us']-last['done_us']<interval:continue
 dt=(r['done_us']-last['done_us'])/1e6;sd=(r['step_us']-last['step_us'])/1e6
 print(round((r['done_us']-start)/1000,1),round((r['counts']-last['counts'])/4096/dt,3),round((r['step_count']-last['step_count'])/3200/sd,3) if sd else None,r['omega_mrev']/1000,r['window_mrev']/1000,r['fas_mrev']/1000,round((r['counts']-first['counts'])*360/4096,3),r['current_ma'])
 last=r
print('largest dt_us',max(r['dt_good_us'] for r in post),'largest_abs_delta',max(abs(r['delta']) for r in post))
print('saved brake2200_spin1.csv')