import serial,time,pathlib,datetime,traceback,os
root=pathlib.Path(__file__).parent
log=root/'rattle_serial.log';mail=root/'rattle_commands.txt'
p=serial.Serial();p.port='COM7';p.baudrate=115200;p.timeout=.05;p.dtr=False;p.rts=False
seen=0;pending=b'';rearm=False
(root/'rattle_capture.pid').write_text(str(os.getpid()))
with log.open('a',encoding='utf-8',buffering=1) as f:
 def host(msg):
  f.write('\n[HOST '+datetime.datetime.now().isoformat(timespec='milliseconds')+'] '+msg+'\n');f.flush()
 try:
  p.open();host('Serial opened; DTR/RTS false; 2-hour lease; automatic diagnostic re-arm after completed dump')
  deadline=time.monotonic()+7200
  while time.monotonic()<deadline:
   data=p.read(p.in_waiting or 1)
   if data:
    f.write(data.decode('utf-8',errors='replace'));f.flush();pending+=data
    while b'\n' in pending:
     line,pending=pending.split(b'\n',1)
     if line.startswith(b'# DIAG n='):rearm=True
   if rearm:
    host('TX d (automatic after completed dump)');p.write(b'd');p.flush();rearm=False
   lines=mail.read_text(encoding='utf-8-sig').splitlines() if mail.exists() else []
   stop=False
   while seen<len(lines):
    cmd=lines[seen].strip();seen+=1
    if cmd=='quit':stop=True;break
    if len(cmd)==1 and cmd in '?sfvrdeD':
     host('TX '+cmd);p.write(cmd.encode());p.flush()
    else:host('Rejected command '+repr(cmd))
   if stop:break
  host('Serial capture ended')
 except Exception:host(traceback.format_exc())
 finally:
  if p.is_open:p.close()
