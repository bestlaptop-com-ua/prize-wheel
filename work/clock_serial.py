import serial,time,pathlib,sys,datetime
root=pathlib.Path(__file__).parent
p=serial.Serial()
p.port="COM7";p.baudrate=115200;p.timeout=.1;p.dtr=False;p.rts=False
data=bytearray()
try:
 p.open()
 if len(sys.argv)>1:
  if sys.argv[1] not in ("t","c"):raise ValueError("audit commands only")
  p.write(sys.argv[1].encode());p.flush()
 end=time.monotonic()+12
 while time.monotonic()<end:
  data.extend(p.read(p.in_waiting or 1))
finally:
 if p.is_open:p.close()
s=data.decode("utf-8",errors="replace")
with (root/"clock_audit.log").open("a",encoding="utf-8") as f:
 f.write("\nHOST "+datetime.datetime.now().isoformat()+" command="+repr(sys.argv[1:])+"\n"+s)
print(s)
