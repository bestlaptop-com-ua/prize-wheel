from pathlib import Path
p=Path(__file__).parent
u=r'''from pathlib import Path
import subprocess,serial,time,traceback,hashlib
p=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
data=bytearray()
try:
 assert (p/'smooth_wheel_build.exit').read_text().strip()=='0'
 assert (p/'smooth_audit_verified.exit').read_text().strip()=='0'
 with (p/'smooth_wheel_upload.log').open('w') as f:
  r=subprocess.run([cli,'upload','--fqbn',fqbn,'--port','COM7','--input-dir',str(p/'smooth_wheel-firmware'),str(p.parent/'prize_wheel_gpt')],stdout=f,stderr=subprocess.STDOUT,timeout=90)
 (p/'smooth_wheel_upload.exit').write_text(str(r.returncode))
 assert r.returncode==0
 port=serial.Serial();port.port='COM7';port.baudrate=115200;port.timeout=.05;port.dtr=False;port.rts=False
 def read_for(sec):
  b=bytearray();end=time.monotonic()+sec
  while time.monotonic()<end:b.extend(port.read(port.in_waiting or 1))
  data.extend(b);return b.decode('utf-8',errors='replace')
 try:
  port.open();port.dtr=False;port.rts=True;time.sleep(.2);port.rts=False
  boot=read_for(5)
  assert 'v2-smooth-ramp-20260918' in boot,'wrong build'
  assert 'allocated=1' in boot and '16 MHz correction OK' in boot,'PSRAM/clock check'
  port.write(b's');port.flush();status=read_for(1)
  assert 'state=IDLE_STOPPED fault=NONE' in status,'not idle; no fault reset attempted'
  assert 'pos=FRESH vel=VALID' in status and 'tmc=1' in status and 'dirCal=1' in status,'sensor/driver/calibration check'
  assert 'fas=0.0000 current=0 EN=1' in status,'outputs not disabled'
  (p/'smooth_wheel_verified.exit').write_text('0')
  binary=p/'smooth_wheel-firmware/prize_wheel_gpt.ino.bin'
  (p/'smooth_wheel.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest())
 finally:
  (p/'smooth_wheel_verified_boot.log').write_bytes(data)
  if port.is_open:port.close()
except Exception:(p/'smooth_wheel_upload.error').write_text(traceback.format_exc())
'''
(p/'upload_verify_smooth.py').write_text(u)
s=(p/'hold1650_capture.py').read_text().replace('hold1650','smooth')
s=s.replace(" def host(msg):\n  f.write('\\n[HOST '+datetime.datetime.now().isoformat(timespec='milliseconds')+'] '+msg+'\\n');f.flush()", " def host(msg):\n  with (root/'smooth_events.log').open('a',encoding='utf-8') as h:\n   h.write('[HOST '+datetime.datetime.now().isoformat(timespec='milliseconds')+'] '+msg+'\\n')")
needle="     line,pending=pending.split(b'\\n',1)"
assert needle in s
s=s.replace(needle,needle+"\n     if line.startswith((b'SPIN#',b'FAULT',b'# HOLD',b'# RAMP_STOP')):host('RX '+line.decode('utf-8',errors='replace').strip())")
assert "f.write('\\n[HOST" not in s
(p/'smooth_capture.py').write_text(s)
(p/'smooth_commands.txt').write_text('?\ns\nd\n')
print('Deployment and non-interleaving timestamped recorder prepared')