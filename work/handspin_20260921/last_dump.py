import sys,os
d=sys.argv[1]
txt=open(os.path.join(d,'serial.raw'),'rb').read().decode('ascii','replace')
txt=txt[txt.rfind('#HOST sent D'):]
rows=[]
for ln in txt.splitlines():
    f=ln.split(',')
    if len(f)==23 and f[0]=='D':
        try: rows.append((int(f[1]),int(f[3]),f[12],int(f[6])))
        except: pass
unw=[0]
for i in range(1,len(rows)): unw.append(unw[-1]+((rows[i][1]-rows[i-1][1]+2048)%4096-2048))
sp=[(unw[i+100]-unw[i])*10 for i in range(0,len(rows)-100,100)]
print('rows',len(rows),'raw0',rows[0][1] if rows else None,'total',unw[-1],'peak_cps',max(sp,key=abs) if sp else 0,'flips',sum(1 for i in range(1,len(sp)) if sp[i]*sp[i-1]<0))
t0=rows[0][0]
print(' '.join('%d,%d'%((rows[i][0]-t0)//1000,unw[i]) for i in range(0,len(rows),80)))
