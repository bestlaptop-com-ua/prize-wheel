import numpy as np
from scipy.integrate import solve_ivp
G,PHI=0.2509,1.779
cb=[(0.0756,0.0082),(0.30,0.15)]
def ref(k,d,sp,a):
    c,b=cb[k]
    f=lambda t,y:[y[1],-(c+b*y[1])+d*G*np.sin(a+d*y[0]-PHI)]
    ev=lambda t,y:y[1]; ev.terminal=True; ev.direction=-1
    s=solve_ivp(f,[0,5000],[0,sp*2*np.pi],events=ev,rtol=1e-10,atol=1e-12,max_step=0.05)
    return np.degrees(s.y[0][-1])
n=0;inh=0;real=[]
for ln in open('out.txt'):
    if ln.startswith('T '):
        _,k,d,sp,a,tr=ln.split(); k=int(k);d=int(d);sp=float(sp);a=float(a);tr=np.degrees(float(tr))
        r=[ref(k,d,sp*f,a) for f in (0.99,1.0,1.01)]
        lo,hi=min(r),max(r); slack=max(0,lo-tr,tr-hi); spread=hi-lo
        n+=1
        if slack>1.0: real.append((k,d,sp,a,round(r[1],1),round(tr,1),round(spread,1)))
print(n,'cases; outside the +-1% speed band by >1 deg:',len(real))
for r in real: print(r)
