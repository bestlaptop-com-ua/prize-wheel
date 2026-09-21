from pathlib import Path
import subprocess,serial,time,traceback,hashlib,re
p=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
data=bytearray()
try:
 assert (p/'brake2200_wheel_build.exit').read_text().strip()=='0'
 assert (p/'speedup_replay_verified.exit').read_text().strip()=='0'
 with (p/'brake2200_wheel_upload.log').open('w') as f:
  r=subprocess.run([cli,'upload','--fqbn',fqbn,'--port','COM7','--input-dir',str(p/'brake2200_wheel-firmware'),str(p.parent/'prize_wheel_gpt')],stdout=f,stderr=subprocess.STDOUT,timeout=90)
 (p/'brake2200_wheel_upload.exit').write_text(str(r.returncode))
 assert r.returncode==0
 port=serial.Serial();port.port='COM7';port.baudrate=115200;port.timeout=.05;port.dtr=False;port.rts=False
 def read_for(sec):
  b=bytearray();end=time.monotonic()+sec
  while time.monotonic()<end:b.extend(port.read(port.in_waiting or 1))
  data.extend(b);return b.decode('utf-8',errors='replace')
 try:
  port.open();port.dtr=False;port.rts=True;time.sleep(.2);port.rts=False
  boot=read_for(5)
  assert 'v2-brake2200-20260918' in boot,'wrong build'
  assert 'samples=16384' in boot and 'allocated=1' in boot and '16 MHz correction OK' in boot,'PSRAM/clock check'
  port.write(b's');port.flush();status=read_for(1)
  assert 'state=IDLE_STOPPED fault=NONE' in status,'not healthy after update'
  assert 'pos=FRESH vel=VALID' in status and 'tmc=1' in status and 'dirCal=1' in status,'sensor/driver/calibration check'
  assert 'fas=0.0000 current=0 EN=1' in status,'outputs not disabled'
  (p/'brake2200_wheel_verified.exit').write_text('0')
  binary=p/'brake2200_wheel-firmware/prize_wheel_gpt.ino.bin'
  (p/'brake2200_wheel.sha256').write_text(hashlib.sha256(binary.read_bytes()).hexdigest())
 finally:
  (p/'brake2200_wheel_verified_boot.log').write_bytes(data)
  if port.is_open:port.close()
except Exception:(p/'brake2200_wheel_final_upload.error').write_text(traceback.format_exc())
