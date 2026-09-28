"""Stage flash 8 on MILL-PC: copy flash-7 sources, apply apply_flash8.py, keep CRLF,
print LF-normalized sha256s.  Never compiles/uploads."""
import hashlib, shutil, subprocess, sys
from pathlib import Path
W = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
P7 = W / 'party7_20260928'; P8 = W / 'party8_20260928'
(P8 / 'prize_wheel_gpt').mkdir(parents=True, exist_ok=True)
for f in (P7 / 'prize_wheel_gpt').iterdir():
    if f.is_file() and f.suffix in ('.ino', '.h'):
        shutil.copy2(f, P8 / 'prize_wheel_gpt' / f.name)
for n in ('compile_candidate.py', 'upload_candidate.py'):
    shutil.copy2(P7 / n, P8 / n)
ino = P8 / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
b = ino.read_bytes(); crlf = b'\r\n' in b
print('flash7 ino', hashlib.sha256(b.replace(b'\r\n', b'\n')).hexdigest())
r = subprocess.run([sys.executable, str(P8 / 'apply_flash8.py')], capture_output=True, text=True)
print(r.stdout.strip(), r.stderr.strip(), 'apply exit', r.returncode)
if r.returncode != 0:
    raise SystemExit(1)
b = ino.read_bytes().replace(b'\r\n', b'\n')
print('flash8 ino', hashlib.sha256(b).hexdigest())
if crlf:
    b = b.replace(b'\n', b'\r\n')
ino.write_bytes(b)
print('crlf', crlf)
