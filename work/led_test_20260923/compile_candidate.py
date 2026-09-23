"""Compile the LED test sketch on MILL-PC. Never uploads or opens serial."""
from pathlib import Path
import hashlib, json, subprocess, traceback
ROOT = Path(__file__).resolve().parent
CLI = Path(r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe')
FQBN = 'esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'
assert not (ROOT / 'compile.exit').exists(), 'Build already recorded'
args = [str(CLI), 'compile', '--fqbn', FQBN, '--warnings', 'all', '--build-path', str(ROOT / 'build'),
        '--output-dir', str(ROOT / 'firmware'), str(ROOT / 'led_test_20260923')]
(ROOT / 'compile.command.json').write_text(json.dumps(args, indent=2))
try:
    with (ROOT / 'compile.log').open('w', encoding='utf-8') as log:
        result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    (ROOT / 'compile.exit').write_text(str(result.returncode))
    if result.returncode == 0:
        hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in (ROOT / 'firmware').glob('*.bin')}
        (ROOT / 'hashes.json').write_text(json.dumps(hashes, indent=2))
except BaseException:
    (ROOT / 'compile.error').write_text(traceback.format_exc())
    (ROOT / 'compile.exit').write_text('-1')
    raise
