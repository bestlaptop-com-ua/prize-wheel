"""Stage flash 6 on MILL-PC: copy flash-5 sources, apply apply_flash6.py, keep CRLF,
print LF-normalized sha256s.  Never compiles/uploads."""
import hashlib, shutil, subprocess, sys
from pathlib import Path
W = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
P5 = W / 'party5_20260925'; P6 = W / 'party6_20260925'
(P6 / 'prize_wheel_gpt').mkdir(parents=True, exist_ok=True)
for f in (P5 / 'prize_wheel_gpt').iterdir():
    if f.is_file() and f.suffix in ('.ino', '.h'):
        shutil.copy2(f, P6 / 'prize_wheel_gpt' / f.name)
for n in ('compile_candidate.py', 'upload_candidate.py'):
    shutil.copy2(P5 / n, P6 / n)
ino = P6 / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
orig = ino.read_bytes()
crlf = b'\r\n' in orig
print('flash5 normalized sha256', hashlib.sha256(orig.replace(b'\r\n', b'\n')).hexdigest())
r = subprocess.run([sys.executable, str(P6 / 'apply_flash6.py')], capture_output=True, text=True)
print(r.stdout.strip(), r.stderr.strip(), 'apply exit', r.returncode)
if r.returncode != 0:
    raise SystemExit(1)
new = ino.read_bytes().replace(b'\r\n', b'\n')
print('flash6 normalized sha256', hashlib.sha256(new).hexdigest())
if crlf:
    new = new.replace(b'\n', b'\r\n')
ino.write_bytes(new)
print('orig crlf', crlf)
