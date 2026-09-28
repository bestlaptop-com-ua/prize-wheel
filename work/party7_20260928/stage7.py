"""Stage flash 7 on MILL-PC: copy flash-6 sources, apply apply_flash7.py, keep CRLF,
print LF-normalized sha256s.  Never compiles/uploads."""
import hashlib, shutil, subprocess, sys
from pathlib import Path
W = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
P6 = W / 'party6_20260925'; P7 = W / 'party7_20260928'
(P7 / 'prize_wheel_gpt').mkdir(parents=True, exist_ok=True)
for f in (P6 / 'prize_wheel_gpt').iterdir():
    if f.is_file() and f.suffix in ('.ino', '.h'):
        shutil.copy2(f, P7 / 'prize_wheel_gpt' / f.name)
for n in ('compile_candidate.py', 'upload_candidate.py'):
    shutil.copy2(P6 / n, P7 / n)
crlf = {}
for n in ('prize_wheel_gpt.ino', 'pw_party.h'):
    b = (P7 / 'prize_wheel_gpt' / n).read_bytes()
    crlf[n] = b'\r\n' in b
    print('flash6', n, hashlib.sha256(b.replace(b'\r\n', b'\n')).hexdigest())
r = subprocess.run([sys.executable, str(P7 / 'apply_flash7.py')], capture_output=True, text=True)
print(r.stdout.strip(), r.stderr.strip(), 'apply exit', r.returncode)
if r.returncode != 0:
    raise SystemExit(1)
for n in ('prize_wheel_gpt.ino', 'pw_party.h'):
    p = P7 / 'prize_wheel_gpt' / n
    b = p.read_bytes().replace(b'\r\n', b'\n')
    print('flash7', n, hashlib.sha256(b).hexdigest())
    if crlf[n]:
        b = b.replace(b'\n', b'\r\n')
    p.write_bytes(b)
print('crlf', crlf)
