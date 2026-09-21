from pathlib import Path
import csv,math,statistics,bisect,json
p=Path(__file__).parent;source=p/'coast_spin7.csv'
if not source.exists():print('Waiting for completed coast trace');raise SystemExit()
rows=[{k:int(v) for k,v in r.items()} for r in csv.DictReader(source.open())]
t0=rows[0]['done_us'];ts=[(r['done_us']-t0)/1e6 for r in rows];xs=[r['counts']/4096 for r in rows]
def x(t):
 j=bisect.bisect_left(ts,t)
 if j<=0:return xs[0]
 if j>=len(ts):return xs[-1]
 a=(t-ts[j-1])/(ts[j]-ts[j-1]);return xs[j-1]+a*(xs[j]-xs[j-1])
def v(t):return (x(t+.05)-x(t-.05))/.1
points=[]
for i in range(3,int((ts[-1]-.3)*10)):
 t=i/10;speed=v(t);a=(v(t-.15)-v(t+.15))/.3;angle=x(t)*2*math.pi
 if speed>.06 and v(t-.15)>.04 and v(t+.15)>.04:
  points.append((t,[1,speed,math.sin(angle),math.cos(angle)],a))
def solve(points,k):
 m=[[sum(z[1][i]*z[1][j] for z in points) for j in range(k)]+[sum(z[1][i]*z[2] for z in points)] for i in range(k)]
 for i in range(k):
  pivot=max(range(i,k),key=lambda j:abs(m[j][i]));m[i],m[pivot]=m[pivot],m[i]
  assert abs(m[i][i])>1e-10,'Singular fit'
  a=m[i][i];m[i]=[q/a for q in m[i]]
  for j in range(k):
   if j==i:continue
   a=m[j][i];m[j]=[q-a*r for q,r in zip(m[j],m[i])]
 return [m[i][-1] for i in range(k)]
def residual(z,coef):return z[2]-sum(a*b for a,b in zip(z[1],coef))
results={}
print('valid points',len(points),'speed_range',min(z[1][1] for z in points),max(z[1][1] for z in points))
for k in (2,4):
 keep=points
 for it in range(4):
  coef=solve(keep,k);res=[residual(z,coef) for z in keep];med=statistics.median(res);mad=statistics.median(abs(r-med) for r in res)
  keep=[z for z in points if abs(residual(z,coef)-med)<=max(.008,4*1.4826*mad)]
 coef=solve(keep,k);rms=math.sqrt(statistics.mean(residual(z,coef)**2 for z in keep))
 results[str(k)]={'coef_decel_revs2':coef,'retained':len(keep),'total':len(points),'rms_revs2':rms,'c_rad':coef[0]*2*math.pi,'b_per_s':coef[1]}
 if k==4:results[str(k)]['imbalance_revs2']=math.hypot(coef[2],coef[3])
 print('model',k,json.dumps(results[str(k)]))
 print('temporal fits')
 for a,b in ((0,len(points)//2),(len(points)//2,len(points))):
  try:print(a,b,solve(points[a:b],k))
  except AssertionError as e:print(str(e))
print('fit ranges: t,min/max v,mean deceleration,existing_model')
for j in range(0,len(points),20):
 group=points[j:j+20];print(round(group[0][0],1),round(group[-1][0],1),round(min(z[1][1] for z in group),4),round(max(z[1][1] for z in group),4),round(statistics.mean(z[2] for z in group),5),round(statistics.mean(.55/(2*math.pi)+.28*z[1][1] for z in group),5))
(p/'coast_spin7_fit.json').write_text(json.dumps(results,indent=2))
with (p/'coast_spin7_fit_points.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['t_s','v_revs','decel_revs2','sin_angle','cos_angle']);w.writerows((z[0],z[1][1],z[2],z[1][2],z[1][3]) for z in points)