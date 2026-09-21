import numpy as np
from scipy.integrate import solve_ivp
from scipy.optimize import least_squares
d=np.loadtxt('data.txt',delimiter=',')
t=d[:,0]/1000; th=d[:,1]*2*np.pi/4096
m=t<15.0; t,th=t[m],th[m]
def sim(p,two=False):
    w0,c0,c1,G,phi=p[:5]
    if two: G2,phi2=p[5],p[6]
    else: G2,phi2=0,0
    f=lambda tt,y:[y[1],-c0-c1*y[1]+G*np.sin(y[0]-phi)+G2*np.sin(2*y[0]-phi2)]
    return solve_ivp(f,[0,t[-1]],[0,w0],t_eval=t,rtol=1e-9,atol=1e-10).y[0]
base=[1.6787,0.0675,0.0155,0.2412,4.633]
best=None
for ph in np.linspace(0,2*np.pi,8,endpoint=False):
    r=least_squares(lambda p:sim(p,True)-th,base+[0.05,ph])
    if best is None or r.cost<best.cost: best=r
print('PHYS + physical 2x torque: rms deg',np.degrees(np.sqrt(np.mean(best.fun**2))),'G2',best.x[5])
def inl(p,thh): return p[0]*np.sin(thh)+p[1]*np.cos(thh)+p[2]*np.sin(2*thh)+p[3]*np.cos(2*thh)
rf=least_squares(lambda p:sim(p)+inl(p[5:],th)-inl(p[5:],0*th)-th,base+[0,0,0,0])
print('PHYS + encoder INL(1x,2x): rms deg',np.degrees(np.sqrt(np.mean(rf.fun**2))))
# only 2x INL
r3=least_squares(lambda p:sim(p)+inl([0,0,p[5],p[6]],th)-inl([0,0,p[5],p[6]],0*th)-th,base+[0,0])
print('PHYS + encoder INL(2x only): rms deg',np.degrees(np.sqrt(np.mean(r3.fun**2))),'amp deg',np.degrees(np.hypot(r3.x[5],r3.x[6])))
# both
best2=None
for ph in np.linspace(0,2*np.pi,6,endpoint=False):
    r=least_squares(lambda p:sim(list(p[:5])+list(p[9:11]),True)+inl(p[5:9],th)-inl(p[5:9],0*th)-th,list(rf.x)+[0.02,ph])
    if best2 is None or r.cost<best2.cost: best2=r
print('PHYS + 2x torque + INL: rms',np.degrees(np.sqrt(np.mean(best2.fun**2))),'INL2x amp deg',np.degrees(np.hypot(best2.x[7],best2.x[8])),'INL1x',np.degrees(np.hypot(best2.x[5],best2.x[6])),'G2',best2.x[9])
np.save('inl.npy',rf.x)
print('INL coeffs (rad) a1,b1,a2,b2 rel. to theta=0 at dump start:',rf.x[5:])
