from pathlib import Path
import subprocess,serial,time,traceback,re
p=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
try:
 assert (p/'speedup_replay_build.exit').read_text().strip()=='0'
 with (p/'speedup_replay_upload.log').open('w') as f:
  r=subprocess.run([cli,'upload','--fqbn',fqbn,'--port','COM7','--input-dir',str(p/'speedup_replay-firmware'),str(p/'speedup_replay')],stdout=f,stderr=subprocess.STDOUT,timeout=90)
 (p/'speedup_replay_upload.exit').write_text(str(r.returncode))
 assert r.returncode==0
 port=serial.Serial();port.port='COM7';port.baudrate=115200;port.timeout=.05;port.dtr=False;port.rts=False
 data=bytearray()
 try:
  port.open();port.dtr=False;port.rts=True;time.sleep(.2);port.rts=False
  end=time.monotonic()+10
  while time.monotonic()<end:
   data.extend(port.read(port.in_waiting or 1))
   if re.search(rb'REPLAY (PASS|FAIL) checks=\d+ failures=\d+ EN=\d\r?\n',data):break
  (p/'speedup_replay_serial.log').write_bytes(data)
  m=re.search(rb'REPLAY PASS checks=(\d+) failures=0 EN=1',data)
  assert m and int(m.group(1))>=15,'replay checks did not pass'
  (p/'speedup_replay_verified.exit').write_text('0')
 finally:
  if port.is_open:port.close()
except Exception:(p/'speedup_replay.error').write_text(traceback.format_exc())