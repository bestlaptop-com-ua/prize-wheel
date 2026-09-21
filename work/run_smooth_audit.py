from pathlib import Path
import subprocess,serial,time,traceback
p=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
try:
 assert (p/'smooth_audit_build.exit').read_text().strip()=='0'
 with (p/'smooth_audit_upload.log').open('w') as f:
  r=subprocess.run([cli,'upload','--fqbn',fqbn,'--port','COM7','--input-dir',str(p/'smooth_audit-firmware'),str(p/'smooth_ramp_audit')],stdout=f,stderr=subprocess.STDOUT,timeout=90)
 (p/'smooth_audit_upload.exit').write_text(str(r.returncode))
 assert r.returncode==0
 port=serial.Serial();port.port='COM7';port.baudrate=115200;port.timeout=.05;port.dtr=False;port.rts=False
 try:
  port.open();port.dtr=False;port.rts=True;time.sleep(.2);port.rts=False
  data=bytearray();end=time.monotonic()+35
  while time.monotonic()<end:
   data.extend(port.read(port.in_waiting or 1))
   if b'AUDIT PASS ALL;' in data or b'AUDIT FAIL' in data:break
  (p/'smooth_audit_serial.log').write_bytes(data)
  assert b'AUDIT PASS ALL; MOTOR DISABLED' in data,'audit did not pass'
  (p/'smooth_audit_verified.exit').write_text('0')
 finally:
  if port.is_open:port.close()
except Exception:(p/'smooth_audit.error').write_text(traceback.format_exc())