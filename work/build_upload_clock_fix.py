from pathlib import Path
import subprocess,traceback,sys
root=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
mode=sys.argv[1]
assert mode in ('clock','wheel')
if mode=='clock':
 args=[cli,'upload','--fqbn',fqbn,'--port','COM7','--input-dir',str(root/'clock-firmware'),str(root/'pulse_clock_audit')]
 name='clock_fixed_upload'
else:
 args=[cli,'compile','--fqbn',fqbn,'--output-dir',str(root/'wheel-clock-firmware'),str(root.parent/'prize_wheel_gpt')]
 name='wheel_clock_build'
try:
 with (root/(name+'.log')).open('w',encoding='utf-8') as out:
  r=subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,timeout=600)
 (root/(name+'.exit')).write_text(str(r.returncode))
except Exception:(root/(name+'.error')).write_text(traceback.format_exc())
