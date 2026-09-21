import serial,time,sys,os
d=sys.argv[1]
p=serial.Serial(port=None,baudrate=115200,timeout=0.05)
p.dtr=False; p.rts=False; p.port='COM7'; p.open()
log=open(os.path.join(d,'session.raw'),'ab')
ev=open(os.path.join(d,'session_events.txt'),'a')
def note(s):
    ev.write('%.1f %s\n'%(time.time(),s)); ev.flush()
t0=time.time(); tail=b''; dumps=0; rearm_at=0
p.write(b'd\n'); note('armed initial')
while time.time()-t0<4500 and not os.path.exists(os.path.join(d,'stop_session')):
    b=p.read(65536)
    if b:
        log.write(b); log.flush()
        tail=(tail+b)[-400:]
        if b'# DIAG n=' in tail:
            dumps+=1; note('dump %d complete'%dumps); tail=b''; rearm_at=time.time()+1.5
    if rearm_at and time.time()>=rearm_at:
        p.write(b'd\n'); note('re-armed'); rearm_at=0
p.close(); log.close(); note('logger exit'); ev.close()
