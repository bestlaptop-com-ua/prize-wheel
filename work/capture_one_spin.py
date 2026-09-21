import serial,time,pathlib,datetime,sys
root=pathlib.Path(__file__).parent
name="spin_"+datetime.datetime.now().strftime("%Y%m%d_%H%M%S")+".log"
path=root/name
p=serial.Serial()
p.port="COM7";p.baudrate=115200;p.timeout=0.05;p.dtr=False;p.rts=False
with path.open("w",encoding="utf-8",buffering=1) as f:
 def read_for(seconds):
  end=time.monotonic()+seconds
  data=bytearray()
  while time.monotonic()<end:
   b=p.read(p.in_waiting or 1)
   if b:data.extend(b);f.write(b.decode("utf-8",errors="replace"));f.flush()
  return data.decode("utf-8",errors="replace")
 try:
  p.open()
  p.write(b"s");p.flush()
  status=read_for(0.5)
  if "state=IDLE_STOPPED fault=NONE" not in status:
   print("NOT READY: "+status);sys.exit(2)
  p.write(b"v");p.flush()
  response=read_for(0.3)
  if "logging=0" in response:
   p.write(b"v");p.flush();response=read_for(0.3)
  if "logging=1" not in response:
   print("NOT READY: could not enable logging "+response);sys.exit(3)
  (root/"capture_ready.txt").write_text(str(path),encoding="utf-8")
  f.write("[HOST] READY "+datetime.datetime.now().isoformat()+"\n")
  read_for(48)
  p.write(b"s");p.flush();read_for(0.4)
  f.write("[HOST] END "+datetime.datetime.now().isoformat()+"\n")
 finally:
  if p.is_open:p.close()
print("SAVED "+str(path))
print(path.read_text(encoding="utf-8"))
