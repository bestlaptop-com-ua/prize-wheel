from pathlib import Path
import csv,re,collections,math,statistics,json,importlib.util
p=Path(__file__).parent
print('available', {k:bool(importlib.util.find_spec(k)) for k in ('numpy','scipy')})
text=(p/'brake2200_serial.log').read_text();blocks=text.split('# DIAG columns:')[1:];print('blocks',len(blocks))
block=blocks[3];header=block.splitlines()[0].strip().split(',');rows=[];bad=[]
for line in block.splitlines()[1:]:
 if not line.startswith('D,'):continue
 vals=line.strip().split(',')[1:]
 try:
  if len(vals)!=len(header):raise ValueError()
  rows.append({k:int(v,16 if k.endswith('_hex') else 10) for k,v in zip(header,vals)})
 except ValueError:bad.append(line[:150])
end=re.search(r'# DIAG n=(\d+)',block)
print('rows',len(rows),'complete',bool(end),'bad',len(bad))
if not end:raise SystemExit()
assert len(rows)==int(end.group(1)) and not bad,bad
(p/'coast_spin7_verified.log').write_text(text)
with (p/'coast_spin7.csv').open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=header);w.writeheader();w.writerows(rows)
print('states',dict(collections.Counter(r['state'] for r in rows)))
print('current',dict(collections.Counter(r['current_ma'] for r in rows)))
print('en_values',dict(collections.Counter(r['pins_hex']&1 for r in rows)))
print('step_delta',rows[-1]['step_count']-rows[0]['step_count'])
print('flags',dict(collections.Counter(hex(r['flags_hex']) for r in rows)))
print('duration',round((rows[-1]['done_us']-rows[0]['done_us'])/1e6,3),'travel_deg',(rows[-1]['counts']-rows[0]['counts'])*360/4096,'first_omega',rows[0]['omega_mrev']/1000)
print('1-second samples: relative_s, encoder_v, unwrapped_angle')
last=rows[0]
for r in rows:
 if r['done_us']-last['done_us']>=1000000:
  print(round((r['done_us']-rows[0]['done_us'])/1e6,3),r['omega_mrev']/1000,round(r['counts']*360/4096,2));last=r