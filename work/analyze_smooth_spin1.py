from pathlib import Path
import re,csv,json,collections
p=Path(__file__).parent
text=(p/'smooth_serial.log').read_text(encoding='utf-8',errors='replace')
parts=text.split('# DIAG columns:')
if len(parts)<2: print('Trace transfer not started');raise SystemExit()
block=parts[1];header=block.splitlines()[0].strip().split(',');rows=[];bad=0
for line in block.splitlines()[1:]:
 if not line.startswith('D,'):continue
 v=line.strip().split(',')[1:]
 try:
  if len(v)!=len(header):raise ValueError()
  rows.append({k:int(x,16 if k.endswith('_hex') else 10) for k,x in zip(header,v)})
 except ValueError:bad+=1
end=re.search(r'# DIAG n=(\d+)',block)
print(json.dumps(dict(rows=len(rows),complete=bool(end),bad=bad,states=dict(collections.Counter(r['state'] for r in rows)))))
if not end:raise SystemExit()
assert len(rows)==int(end.group(1)) and bad==0
with (p/'smooth_spin1.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=header);w.writeheader();w.writerows(rows)
first=next(r for r in rows if r['state']==6);start=first['done_us'];post=[r for r in rows if r['done_us']>=start]
direction=-1
print('captured range_ms',(rows[0]['done_us']-start)/1000,(rows[-1]['done_us']-start)/1000)
print('gstat',dict(collections.Counter(hex(r['gstat_hex']) for r in post)))
print('driver',dict(collections.Counter(hex(r['drv_status_hex']) for r in post)))
prev=None
for r in post:
 key=(r['state'],r['stage'])
 if key!=prev:
  print('TRANSITION',json.dumps(dict(ms=(r['done_us']-start)/1000,state=r['state'],stage=r['stage'],omega=r['omega_mrev']/1000,fas=r['fas_mrev']/1000,current=r['current_ma'],raw=r['raw'],counts=r['counts'])));prev=key
lowest=min(post,key=lambda r:direction*r['omega_mrev']);last=post[-1]
print('LOWEST',json.dumps(dict(ms=(lowest['done_us']-start)/1000,forward=direction*lowest['omega_mrev']/1000,fas=lowest['fas_mrev']/1000,current=lowest['current_ma'])))
print('25ms intervals: ms encoderForwardRPS pulseRPS filteredForward windowForward fas current')
last=first
for r in post:
 if r['done_us']-last['done_us']<25000:continue
 dt=(r['done_us']-last['done_us'])/1e6;sd=(r['step_us']-last['step_us'])/1e6
 print(round((r['done_us']-start)/1000,1),round(direction*(r['counts']-last['counts'])/4096/dt,4),round((r['step_count']-last['step_count'])/3200/sd,4) if sd else None,direction*r['omega_mrev']/1000,direction*r['window_mrev']/1000,r['fas_mrev']/1000,r['current_ma'])
 last=r
peak=direction*first['counts'];peakrow=first;worst=(0,None,None)
for r in post:
 value=direction*r['counts']
 if value>peak:peak=value;peakrow=r
 if peak-value>worst[0]:worst=(peak-value,peakrow,r)
print('max_reverse_excursion_deg',worst[0]*360/4096)
if worst[1]: print('reverse_interval_ms',(worst[1]['done_us']-start)/1000,(worst[2]['done_us']-start)/1000)
print('saved smooth_spin1.csv')