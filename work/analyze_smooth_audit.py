from pathlib import Path
import re,json,statistics
p=Path(__file__).parent
text=(p/'smooth_audit_verified_serial.log').read_text()
assert 'AUDIT PASS ALL; MOTOR DISABLED' in text
for block in text.split('RAMP hz=')[1:]:
 line=block.splitlines()[0]
 hz=int(line.split()[0]);a=int(re.search(r'a=(\d+)',line).group(1))
 rows=[]
 for line in block.splitlines()[1:]:
  if line.startswith('R,'):
   _,t,n,v=line.split(',');rows.append((int(t)*1e-6,int(n),int(v)/1000))
 # Integrate actual GPIO edges over >=200ms windows; exclude startup and
 # sparse final pulses from slope fitting. No reliance on reported FAS rate.
 bins=[];last=rows[0]
 for row in rows[1:]:
  if row[0]-last[0]>=.2:
   rate=(row[1]-last[1])/(row[0]-last[0]);mid=(row[0]+last[0])/2
   if mid>.25 and rate>hz*.12:bins.append((mid,rate))
   last=row
 mt=statistics.mean(t for t,v in bins);mv=statistics.mean(v for t,v in bins)
 slope=sum((t-mt)*(v-mv) for t,v in bins)/sum((t-mt)**2 for t,v in bins)
 positive=max((bins[i][1]-bins[i-1][1] for i in range(1,len(bins))),default=0)
 print(json.dumps(dict(entryHz=hz,plannedSps2=a,gpioFittedDecel=round(-slope,2),largestWindowRiseHz=round(positive,2),windows=len(bins))))
 assert abs(-slope-a)<a*.08+5
 assert positive<max(10,hz*.02)
print('PASS: GPIO-derived slopes agree with plan; no speed rise in checked windows')