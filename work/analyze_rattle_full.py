from pathlib import Path
import csv,json,statistics,collections,re
root=Path(__file__).parent
text=(root/'rattle_serial.log').read_text(encoding='utf-8',errors='replace')
block=text.split('# DIAG columns:')[-1]
# One asynchronous status line interrupted a row; keep original log and remove only that named record.
block=re.sub(r'MANUAL-ADJUST[^\n]*\n','',block)
header=block.splitlines()[0].strip().split(',')
hexkeys={'flags_hex','drv_status_hex','gstat_hex','pins_hex'}
rows=[]
for line in block.splitlines()[1:]:
 if line.startswith('D,'):
  values=line.split(',')[1:]
  if len(values)!=len(header):continue
  try:rows.append({k:int(v,16 if k in hexkeys else 10) for k,v in zip(header,values)})
  except ValueError:continue
complete=re.search(r'# DIAG n=(\d+)',block)
print('rows',len(rows),'dump_complete',bool(complete))
if not complete:raise SystemExit(0)
assert len(rows)==int(complete.group(1))
start=next((r['done_us'] for r in rows if r['state']==6),rows[0]['done_us'])
for r in rows:r['t_ms']=(r['done_us']-start)/1000
out=root/'rattle_full_spin1.csv'
with out.open('w',newline='') as f:
 w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
print('range_ms',rows[0]['t_ms'],rows[-1]['t_ms'],'dt median/max',statistics.median(r['dt_good_us'] for r in rows),max(r['dt_good_us'] for r in rows))
print('driver_status_hex',dict(collections.Counter(hex(r['drv_status_hex']) for r in rows)))
print('gstat_hex',dict(collections.Counter(hex(r['gstat_hex']) for r in rows)))
print('flags',dict(collections.Counter(hex(r['flags_hex']) for r in rows)))
print('transitions')
prev=None
for r in rows:
 key=(r['state'],r['stage'],r['pins_hex'])
 if key!=prev:print(json.dumps(r));prev=key
powered=[r for r in rows if r['t_ms']>=0 and r['stage']>0]
print('powered DIR values',sorted(set((r['pins_hex']>>1)&1 for r in powered)))
print('100ms intervals: ms, encoder_rev_s, step_rev_s, filtered, fas, current, state, phase_error_deg')
first=next(r for r in rows if r['state']==6)
last=first
for r in rows:
 if r['t_ms']<=last['t_ms'] or r['t_ms']-last['t_ms']<100:continue
 dt=(r['done_us']-last['done_us'])/1e6
 stepdt=(r['step_us']-last['step_us'])/1e6
 enc=(r['counts']-last['counts'])/4096/dt
 pulse=(r['step_count']-last['step_count'])/3200/stepdt
 phase=((r['counts']-first['counts'])/4096-(r['step_count']-first['step_count'])/3200)*360
 print(round(r['t_ms'],1),round(enc,4),round(pulse,4),r['omega_mrev']/1000,r['fas_mrev']/1000,r['current_ma'],r['state'],round(phase,3))
 last=r
post=[r for r in rows if r['t_ms']>=0]
peak=max(post,key=lambda r:r['counts'])
end=post[-1]
print('max_encoder_angle_then_final',{'peak_ms':peak['t_ms'],'reverse_to_final_deg':(peak['counts']-end['counts'])*360/4096,'peak_counts':peak['counts'],'end_counts':end['counts']})
print('saved',out)
