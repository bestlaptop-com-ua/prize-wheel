from pathlib import Path
import csv,re,collections,json
p=Path(__file__).parent
text=(p/'speedup_serial.log').read_text()
block=text.split('# DIAG columns:')[1]
header=block.splitlines()[0].strip().split(','); rows=[];bad=0
for line in block.splitlines()[1:]:
 if not line.startswith('D,'):continue
 vals=line.strip().split(',')[1:]
 try:
  if len(vals)!=len(header):raise ValueError()
  rows.append({k:int(v,16 if k.endswith('_hex') else 10) for k,v in zip(header,vals)})
 except ValueError:bad+=1
expected=int(re.search(r'# DIAG n=(\d+)',block).group(1))
assert len(rows)==expected and bad==0
with (p/'speedup_spin1.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=header);w.writeheader();w.writerows(rows)
first=next(r for r in rows if r['state']==6); start=first['done_us']
post=[r for r in rows if r['done_us']>=start]
print('complete rows',len(rows),'bad',bad,'states',dict(collections.Counter(r['state'] for r in rows)))
print('powered GSTAT',dict(collections.Counter(hex(r['gstat_hex']) for r in post)))
print('thermal_short_samples',sum(bool(r['drv_status_hex']&0x1e000000) for r in post))
lastkey=None
for r in post:
 key=(r['state'],r['stage'])
 if key!=lastkey:
  print('transition',json.dumps(dict(ms=round((r['done_us']-start)/1000,1),state=r['state'],stage=r['stage'],encoder=r['omega_mrev']/1000,fas=r['fas_mrev']/1000,steps=r['step_count'],current=r['current_ma'])));lastkey=key
hold=[r for r in post if r['state']==9]
print('hold_raw_angle_span_deg',(max(r['counts'] for r in hold)-min(r['counts'] for r in hold))*360/4096)
print('hold_step_delta',hold[-1]['step_count']-hold[0]['step_count'])
peak=-first['counts'];peakrow=first;worst=(0,None,None)
for r in post:
 value=-r['counts']
 if value>peak:peak=value;peakrow=r
 if peak-value>worst[0]:worst=(peak-value,peakrow,r)
print('maximum_rearshaft_reverse_excursion_deg',worst[0]*360/4096)
if worst[1]:print('excursion_start_end_ms',(worst[1]['done_us']-start)/1000,(worst[2]['done_us']-start)/1000)
print('saved speedup_spin1.csv')