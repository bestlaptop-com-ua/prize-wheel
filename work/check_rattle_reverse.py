from pathlib import Path
import csv,json
rows=list(csv.DictReader((Path(__file__).parent/'rattle_full_spin1.csv').open()))
rows=[{k:float(v) if k=='t_ms' else int(v) for k,v in r.items()} for r in rows]
for a,b in [(0,250),(250,1999),(1999,2593)]:
 subset=[r for r in rows if a<=r['t_ms']<b]
 high=None;largest=0;pair=None
 for r in subset:
  if high is None or r['counts']>high['counts']:high=r
  rev=high['counts']-r['counts']
  if rev>largest:largest=rev;pair=(high['t_ms'],r['t_ms'])
 print('interval_ms',a,b,'maximum_backward_drawdown_deg',round(largest*360/4096,4),'time_pair',pair)
powered=[r for r in rows if r['stage']>0]
mask=sum(1<<x for x in [25,26,27,28,29,30])
print('powered_driver_thermal_short_open_flags',sorted(set(hex(r['drv_status_hex']&mask) for r in powered)))
print('any_negative_PCNT_delta_during_powered_capture',any(b['step_count']<a['step_count'] for a,b in zip(powered,powered[1:]) if a['t_ms']>=0))
