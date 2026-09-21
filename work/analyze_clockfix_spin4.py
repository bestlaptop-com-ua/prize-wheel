from pathlib import Path
import statistics,csv
root=Path(__file__).parent
text=(root/'clockfix_serial.log').read_text(encoding='utf-8')
block=text.split('# DIAG columns:')[-1]
rows=[]
for line in block.splitlines():
 if line.startswith('D,'):
  vals=line.split(',')[1:]
  if len(vals)==15:rows.append([int(v,16) if i==11 else int(v) for i,v in enumerate(vals)])
with (root/'clockfix_spin4.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['us','dt_us','raw','delta','counts','i2c_us','omega','window','cmd','fas','remaining','flags','state','stage','current_ma']);w.writerows(rows)
active=[r for r in rows if r[12]==7 and r[9]>=100]
print('Captured final %.3f seconds (%d samples)'%((rows[-1][0]-rows[0][0])/1e6,len(rows)))
print('Median absolute speed mismatch for captured decel samples >= 0.1 rev/s:',round(statistics.median(abs(abs(r[6])-r[9]) for r in active)/1000,4),'rev/s')
print('Median measured / FAS speed:',round(statistics.median(abs(r[6])/r[9] for r in active),3))
print('All sampled encoder reads valid:',all(r[11]&1 for r in rows))
