import subprocess,json
from pathlib import Path
ROOT=Path(__file__).resolve().parent
CLI=r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe'
FQBN='esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
args=[CLI,'upload','--fqbn',FQBN,'--port','COM7','--input-dir',str(ROOT/'firmware'),str(ROOT/'striptest')]
(ROOT/'upload.command.json').write_text(json.dumps(args,indent=2))
with (ROOT/'upload.log').open('w') as log:
    r=subprocess.run(args,stdout=log,stderr=subprocess.STDOUT,timeout=300)
(ROOT/'upload.exit').write_text(str(r.returncode))
