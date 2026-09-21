import sys,os
d=sys.argv[1]; step=int(sys.argv[2])
txt=open(os.path.join(d,'serial.raw'),'rb').read().decode('ascii','replace')
k=txt.rfind('#HOST sent D')
txt=txt[k:]
rows=[]
for ln in txt.splitlines():
    f=ln.split(',')
    if len(f)==23 and f[0]=='D':
        try: rows.append([int(f[1]),int(f[2]),int(f[3]),int(f[4]),int(f[5]),int(f[6]),int(f[7]),f[12]])
        except: pass
print('rows',len(rows))
t0=rows[0][0]
bad_dt=[(i,r[1]) for i,r in enumerate(rows) if r[1]>3000]
bad_i2c=[(i,r[5]) for i,r in enumerate(rows) if r[5]>1500]
flags={}
for r in rows: flags[r[7]]=flags.get(r[7],0)+1
print('dt>3ms',len(bad_dt),bad_dt[:8],'i2c>1.5ms',len(bad_i2c),bad_i2c[:8],'flags',flags)
unw=[0]
jumps=[]
for i in range(1,len(rows)):
    dd=(rows[i][2]-rows[i-1][2]+2048)%4096-2048
    unw.append(unw[-1]+dd)
    if i>1:
        prev=unw[-2]-unw[-3]
        if abs(dd-prev)>6: jumps.append((i,prev,dd,rows[i][1]))
print('accel-jumps(|d2|>6 counts)',len(jumps),jumps[:15])
cnt_mis=sum(1 for i in range(1,len(rows)) if (rows[i][4]-rows[i-1][4])!=-(unw[i]-unw[i-1]) and (rows[i][4]-rows[i-1][4])!=(unw[i]-unw[i-1]))
print('fw counts vs raw unwrap mismatches',cnt_mis,'span s',(rows[-1][0]-t0)/1e6,'total counts',unw[-1])
print('CSV t_ms,unw')
for i in range(0,len(rows),step):
    print(str((rows[i][0]-t0)//1000)+','+str(unw[i]))
