from pathlib import Path
import subprocess,traceback,serial,time
root=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
try:
 assert (root/'wheel_rattle_build.exit').read_text().strip()=='0'
 with (root/'wheel_rattle_upload.log').open('w',encoding='utf-8') as out:
  r=subprocess.run([cli,'upload','--fqbn',fqbn,'--port','COM7','--input-dir',str(root/'wheel-rattle-firmware'),str(root.parent/'prize_wheel_gpt')],stdout=out,stderr=subprocess.STDOUT,timeout=90)
 (root/'wheel_rattle_upload.exit').write_text(str(r.returncode))
 if r.returncode==0:
  p=serial.Serial();p.port='COM7';p.baudrate=115200;p.timeout=.1;p.dtr=False;p.rts=False
  try:
   p.open()
   with (root/'wheel_rattle_boot.log').open('w',encoding='utf-8',buffering=1) as f:
    end=time.monotonic()+12
    while time.monotonic()<end:
     data=p.read(p.in_waiting or 1)
     if data:f.write(data.decode('utf-8',errors='replace'))
    for cmd in ('?','s','f'):
     p.write(cmd.encode());p.flush();end=time.monotonic()+1
     while time.monotonic()<end:
      data=p.read(p.in_waiting or 1)
      if data:f.write(data.decode('utf-8',errors='replace'))
  finally:
   if p.is_open:p.close()
except Exception:(root/'wheel_rattle_upload.error').write_text(traceback.format_exc())
