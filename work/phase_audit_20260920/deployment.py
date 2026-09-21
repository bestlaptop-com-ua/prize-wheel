"""Explicit build/upload steps. Never starts the motor or clears NVS/faults."""
from pathlib import Path
import hashlib
import json
import subprocess
import sys
import traceback

ROOT = Path(__file__).resolve().parent
CLI = Path(r'C:\Users\Mill\AppData\Local\Programs\arduino-ide\resources\app\lib\backend\resources\arduino-cli.exe')
FQBN = 'esp32:esp32:esp32s3:CDCOnBoot=default,FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB,UploadSpeed=921600'

def main():
    action, variant = sys.argv[1:]
    assert action in ('compile', 'upload') and variant == 'disabled'
    out = ROOT / ('firmware_' + variant)
    stem = ROOT / (action + '_' + variant)
    assert not stem.with_suffix('.exit').exists(), 'Step already recorded; review before another attempt'
    if action == 'compile':
        args = [str(CLI), 'compile', '--fqbn', FQBN,
                '--build-path', str(ROOT / ('build_' + variant)),
                '--output-dir', str(out),
                '--build-property', 'compiler.cpp.extra_flags=-DPW_SELFSPIN_MOTION_ENABLE=' + ('1' if variant == 'enabled' else '0'),
                str(ROOT / 'prize_wheel_gpt')]
    else:
        assert (ROOT / ('compile_' + variant + '.exit')).read_text().strip() == '0'
        assert (out / 'prize_wheel_gpt.ino.bin').is_file()
        args = [str(CLI), 'upload', '--fqbn', FQBN, '--port', 'COM7',
                '--input-dir', str(out), str(ROOT / 'prize_wheel_gpt')]
    stem.with_suffix('.command.json').write_text(json.dumps(args, indent=2), encoding='utf-8')
    try:
        with stem.with_suffix('.log').open('w', encoding='utf-8') as log:
            result = subprocess.run(args, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=600 if action == 'compile' else 100)
        stem.with_suffix('.exit').write_text(str(result.returncode))
        if result.returncode == 0 and action == 'compile':
            hashes = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in out.glob('*.bin')}
            (ROOT / ('hashes_' + variant + '.json')).write_text(json.dumps(hashes, indent=2))
    except Exception:
        stem.with_suffix('.error').write_text(traceback.format_exc())
        stem.with_suffix('.exit').write_text('-1')
        raise

if __name__ == '__main__':
    main()
