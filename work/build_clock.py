from pathlib import Path
import subprocess,traceback
root=Path(__file__).parent
cli=r"C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
fqbn="esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
try:
 with (root/"clock_build.log").open("w",encoding="utf-8") as out:
  r=subprocess.run([cli,"compile","--fqbn",fqbn,"--output-dir",str(root/"clock-firmware"),str(root/"pulse_clock_audit")],stdout=out,stderr=subprocess.STDOUT,timeout=600)
 (root/"clock_build.exit").write_text(str(r.returncode))
except Exception:(root/"clock_build.error").write_text(traceback.format_exc())
