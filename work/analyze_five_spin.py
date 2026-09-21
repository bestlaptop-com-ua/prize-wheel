from pathlib import Path
import csv,json,collections
p=Path(__file__).parent
rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((p/'five_spin_diag_block2.csv').open())]
first=next(r for r in rows if r['state']==6)
start=first['done_us']; post=[r for r in rows if r['done_us']>=start]
print('driver_status',dict(collections.Counter(hex(r['drv_status_hex']) for r in post)))
print('gstat',dict(collections.Counter(hex(r['gstat_hex']) for r in post)))
print('transitions')
prev=None
for r in post:
 key=(r['state'],r['stage'])
 if key!=prev:
  print(json.dumps(dict(ms=round((r['done_us']-start)/1000,1),state=r['state'],stage=r['stage'],omega=r['omega_mrev']/1000,cmd=r['cmd_mrev']/1000,fas=r['fas_mrev']/1000,remain=r['remain_ddeg']/10,current=r['current_ma'],steps=r['step_count'],enc=r['counts'])));prev=key
print('200ms intervals: ms encRPS pulseRPS omega cmd fas remain')
last=first
for r in post:
 if r['done_us']-last['done_us']<200000:continue
 dt=(r['done_us']-last['done_us'])/1e6;sd=(r['step_us']-last['step_us'])/1e6
 print(round((r['done_us']-start)/1000),round((r['counts']-last['counts'])/4096/dt,3),round((r['step_count']-last['step_count'])/3200/sd,3) if sd else None,r['omega_mrev']/1000,r['cmd_mrev']/1000,r['fas_mrev']/1000,r['remain_ddeg']/10)
 last=r