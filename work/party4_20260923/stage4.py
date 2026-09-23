"""Stage flash 4 on MILL-PC: copy flash-3 sources, apply apply_engage_vary.py, keep the
original line endings, print the LF-normalized sha256 of the result. Never compiles/uploads."""
import hashlib, shutil, subprocess, sys
from pathlib import Path
W = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
P3 = W / 'party3_20260923'; P4 = W / 'party4_20260923'
(P4 / 'prize_wheel_gpt').mkdir(parents=True, exist_ok=True)
for f in (P3 / 'prize_wheel_gpt').iterdir():
    if f.is_file() and f.suffix in ('.ino', '.h'):
        shutil.copy2(f, P4 / 'prize_wheel_gpt' / f.name)
for n in ('compile_candidate.py', 'upload_candidate.py'):
    shutil.copy2(P3 / n, P4 / n)
ino = P4 / 'prize_wheel_gpt' / 'prize_wheel_gpt.ino'
orig = ino.read_bytes()
crlf = b'\r\n' in orig
print('flash3 normalized sha256', hashlib.sha256(orig.replace(b'\r\n', b'\n')).hexdigest())
r = subprocess.run([sys.executable, str(P4 / 'apply_engage_vary.py')], capture_output=True, text=True)
print(r.stdout.strip(), r.stderr.strip(), 'apply exit', r.returncode)
if r.returncode != 0:
    raise SystemExit(1)
new = ino.read_bytes().replace(b'\r\n', b'\n')
print('flash4 normalized sha256', hashlib.sha256(new).hexdigest())
if crlf:
    new = new.replace(b'\n', b'\r\n')
ino.write_bytes(new)
print('orig crlf', crlf)
