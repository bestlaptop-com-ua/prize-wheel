import numpy as np
from scipy.integrate import solve_ivp
from scipy.optimize import least_squares
d=np.loadtxt('data.txt',delimiter=',')
t=d[:,0]/1000; th=d[:,1]*2*np.pi/4096
# use moving part only (omega>~0.02 rev/s): up to t=15.2
m=t<15.0
t,th=t[m],th[m]
def sim(p):
    w0,c0,c1,G,phi=p[:5]
    f=lambda tt,y:[y[1],-c0-c1*y[1]+G*np.sin(y[0]-phi)]
    s=solve_ivp(f,[0,t[-1]],[0,w0],t_eval=t,rtol=1e-9,atol=1e-10)
    return s.y[0]
def res_phys(p): return sim(p)-th
def res_full(p):
    e=p[5]*np.sin(th)+p[6]*np.cos(th)+p[7]*np.sin(2*th)+p[8]*np.cos(2*th)
    e0=p[6]+p[8]
    return sim(p)+ (e-e0) - th
p0=[1.65,0.05,0.05,0.3,0.0]
best=None
for phi in np.linspace(0,2*np.pi,8,endpoint=False):
    r=least_squares(res_phys,[1.65,0.05,0.05,0.3,phi])
    if best is None or r.cost<best.cost: best=r
print('PHYS only: w0,c0,c1,G,phi',best.x,'rms deg',np.degrees(np.sqrt(np.mean(best.fun**2))),'max deg',np.degrees(np.abs(best.fun).max()))
r2=least_squares(res_full,list(best.x)+[0,0,0,0])
A1=np.hypot(r2.x[5],r2.x[6]);A2=np.hypot(r2.x[7],r2.x[8])
print('PHYS+INL: ',r2.x[:5],'INL 1x amp deg',np.degrees(A1),'2x amp deg',np.degrees(A2),'rms deg',np.degrees(np.sqrt(np.mean(r2.fun**2))),'max',np.degrees(np.abs(r2.fun).max()))
# uncertainty
J=r2.jac; cov=np.linalg.inv(J.T@J)*(2*r2.cost/(len(t)-9)); sd=np.sqrt(np.diag(cov))
print('sd INL terms deg',np.degrees(sd[5:]))
print('G rad/s2',r2.x[3],'heavy-spot phase deg',np.degrees(r2.x[4])%360)
print('residual phys-only sample (deg):',np.round(np.degrees(best.fun[::12]),2))
