import serial,time,pathlib,datetime,traceback
root=pathlib.Path(__file__).parent
log=root/"clockfix_serial.log"
mail=root/"clockfix_commands.txt"
p=serial.Serial()
p.port="COM7";p.baudrate=115200;p.timeout=0.05;p.dtr=False;p.rts=False
seen=0
with log.open("a",encoding="utf-8",buffering=1) as f:
 def host(msg):
  f.write("\n[HOST "+datetime.datetime.now().isoformat(timespec="milliseconds")+"] "+msg+"\n");f.flush()
 try:
  p.open();host("Serial opened; DTR/RTS false; lease 3600 seconds")
  deadline=time.monotonic()+3600
  while time.monotonic()<deadline:
   data=p.read(p.in_waiting or 1)
   if data:f.write(data.decode("utf-8",errors="replace"));f.flush()
   lines=mail.read_text(encoding="utf-8-sig").splitlines() if mail.exists() else []
   stop=False
   while seen<len(lines):
    cmd=lines[seen].strip();seen+=1
    if cmd=="quit":stop=True;break
    if len(cmd)==1 and cmd in "?sfvrde":
     host("TX "+cmd);p.write(cmd.encode("ascii"));p.flush()
    else:host("Rejected command "+repr(cmd))
   if stop:break
  host("Serial capture ended")
 except Exception:
  host(traceback.format_exc())
 finally:
  if p.is_open:p.close()

