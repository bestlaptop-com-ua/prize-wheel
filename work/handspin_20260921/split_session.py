import sys,os
d=sys.argv[1]
txt=open(os.path.join(d,'session.raw'),'rb').read().decode('ascii','replace')
parts=txt.split('# DIAG n=')
print('dumps',len(parts)-1)
for k,part in enumerate(parts[:-1]):
    rows=[]
    for ln in part.splitlines():
        f=ln.split(',')
        if len(f)==23 and f[0]=='D':
            try: rows.append((int(f[1]),int(f[3]),int(f[5]),f[12],int(f[6])))
            except: pass
    hdr=parts[k+1].splitlines()[0]
    if not rows: print(k+1,'no rows'); continue
    unw=[0]
    for i in range(1,len(rows)):
        unw.append(unw[-1]+((rows[i][1]-rows[i-1][1]+2048)%4096-2048))
    flags={}
    for r in rows: flags[r[3]]=flags.get(r[3],0)+1
    # speed in counts/s over 100ms windows
    sp=[(unw[i+100]-unw[i])*10 for i in range(0,len(rows)-100,100)]
    mx=max(sp,key=abs) if sp else 0
    rev=sum(1 for i in range(1,len(sp)) if sp[i]*sp[i-1]<0)
    print('dump',k+1,'n=',hdr.strip(),'rows',len(rows),'span_s',round((rows[-1][0]-rows[0][0])/1e6,2),'raw0',rows[0][1],'rawEnd',rows[-1][1],'total_counts',unw[-1],'peak_cps',mx,'start_cps',sp[0] if sp else 0,'sign_flips',rev,'flags',flags,'max_i2c_us',max(r[4] for r in rows))
    with open(os.path.join(d,'dump%d.csv'%(k+1)),'w') as o:
        t0=rows[0][0]
        for i in range(0,len(rows),80): o.write('%d,%d\n'%((rows[i][0]-t0)//1000,unw[i]))
