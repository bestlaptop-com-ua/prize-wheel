import numpy as np
p=np.load('inl.npy')[5:]
def e(raw):
    th=(raw-37)*2*np.pi/4096
    return np.degrees(p[0]*np.sin(th)+p[1]*np.cos(th)+p[2]*np.sin(2*th)+p[3]*np.cos(2*th))
x=np.array([0,10,20,32,45,60,72,87,98,110,122,135])
pos_meas=[0,-0.11,-0.47,-0.75,-0.81,-0.65,-0.17,0.34,0.9,1.33,1.84,2.03]
neg_meas=[0,0.16,-0.12,-0.39,-0.46,-0.21,0.36,0.98,1.43,2.06,2.45,2.65]
print('travel  pos_pred pos_meas | neg_pred neg_meas')
for i,xx in enumerate(x):
    c=xx*4096/360
    pp=e(957-c)-e(957); nn=-(e(883+c)-e(883))
    print(f'{xx:5d}  {pp:7.2f} {pos_meas[i]:7.2f} | {nn:7.2f} {neg_meas[i]:7.2f}')
r=np.arange(0,4096,8); ee=e(r); print('INL p-p deg',ee.max()-ee.min(),'max at raw',r[ee.argmax()],'min at raw',r[ee.argmin()])
