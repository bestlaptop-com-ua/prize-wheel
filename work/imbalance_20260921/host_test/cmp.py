import numpy as np
from scipy.integrate import solve_ivp
G,PHI=0.2509,1.779
cb=[(0.0756,0.0082),(0.30,0.15)]
def ref(k,d,sp,a):
    c,b=cb[k]
    f=lambda t,y:[y[1],-(c+b*y[1])+d*G*np.sin(a+d*y[0]-PHI)]
    ev=lambda t,y:y[1]; ev.terminal=True; ev.direction=-1
    s=solve_ivp(f,[0,5000],[0,sp*2*np.pi],events=ev,rtol=1e-10,atol=1e-12,max_step=0.05)
    return s.y[0][-1]
worst=0;rows=[]
for ln in open('out.txt'):
    if ln.startswith('T '):
        _,k,d,sp,a,tr=ln.split(); k=int(k);d=int(d);sp=float(sp);a=float(a);tr=float(tr)
        r=ref(k,d,sp,a); err=np.degrees(tr-r); rows.append((k,d,sp,a,np.degrees(r),err)); 
for k in (0,1):
    e=[abs(r[5]) for r in rows if r[0]==k]; print('model',k,'n',len(e),'max err deg',max(e),'median',np.median(e))
bad=sorted(rows,key=lambda r:-abs(r[5]))[:6]
for r in bad: print('worst',r)
