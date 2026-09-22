import serial,time,sys,os
d=sys.argv[1]
p=serial.Serial(port=None,baudrate=115200,timeout=0.05)
p.dtr=False; p.rts=False; p.port='COM7'; p.open()
log=open(os.path.join(d,'serial.raw'),'ab')
t0=time.time(); last=0
p.write(b's\n')
while time.time()-t0<14400 and not os.path.exists(os.path.join(d,'stop')):
    b=p.read(65536)
    if b: log.write(b); log.flush()
    cmdf=os.path.join(d,'cmd.txt')
    if os.path.exists(cmdf):
        c=open(cmdf).read().strip(); os.remove(cmdf)
        if c: p.write(c.encode()+b'\n'); log.write(('\n#HOST sent '+c+' at '+str(time.time())+'\n').encode())
p.close(); log.close()
