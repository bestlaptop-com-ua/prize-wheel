from pathlib import Path
import subprocess,traceback
p=Path(__file__).parent
cli=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
fqbn='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
try:
 for name,sketch in [('smooth_audit',p/'smooth_ramp_audit'),('smooth_wheel',p.parent/'prize_wheel_gpt')]:
  with (p/(name+'_build.log')).open('w') as f:
   r=subprocess.run([cli,'compile','--fqbn',fqbn,'--output-dir',str(p/(name+'-firmware')),str(sketch)],stdout=f,stderr=subprocess.STDOUT,timeout=600)
  (p/(name+'_build.exit')).write_text(str(r.returncode))
  if r.returncode:break
except Exception:(p/'smooth_build.error').write_text(traceback.format_exc())
