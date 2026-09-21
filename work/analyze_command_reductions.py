from pathlib import Path
import csv,json
p=Path(__file__).parent
rows=[{k:int(v) for k,v in row.items()} for row in csv.DictReader((p/'five_spin_diag_block2.csv').open())]
first=next(r for r in rows if r['state']==6);start=first['done_us']; prev=first; changes=[]
for r in rows:
 if r['done_us']<start or r['state'] not in (6,7):continue
 if r['cmd_mrev']!=prev['cmd_mrev']:
  dt=(r['done_us']-prev['done_us'])/1e6
  changes.append(dict(ms=round((r['done_us']-start)/1000,1),dt=round(dt,4),old=prev['cmd_mrev']/1000,new=r['cmd_mrev']/1000,decel=round((prev['cmd_mrev']-r['cmd_mrev'])/1000/dt,3) if dt else 0,omega=r['omega_mrev']/1000,fas=r['fas_mrev']/1000,stage=r['stage'],remain=r['remain_ddeg']/10))
  prev=r
print('planned_decel_rev_s2=.2031; changes with implied decel above .35:')
for x in changes:
 if x['decel']>.35:print(json.dumps(x))
print('last 12 changes')
for x in changes[-12:]:print(json.dumps(x))
print('min filtered and max reverse raw delta in first250ms')
cap=[r for r in rows if 0<=r['done_us']-start<=250000]
print(min(cap,key=lambda r:r['omega_mrev']))
print(min(cap,key=lambda r:r['delta']))