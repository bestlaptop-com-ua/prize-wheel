from pathlib import Path
import subprocess,traceback
root=Path(__file__).parent
cli=r"C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe"
fqbn="esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600"
args=[cli,"compile","--fqbn",fqbn,str(root.parent/"prize_wheel_gpt")]
try:
 with (root/"diag_build.log").open("w",encoding="utf-8") as out:
  result=subprocess.run(args,stdout=out,stderr=subprocess.STDOUT,timeout=600)
 (root/"diag_build.exit").write_text(str(result.returncode))
except Exception:
 (root/"diag_build.error").write_text(traceback.format_exc())
