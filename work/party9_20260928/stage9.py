"""Stage flash 9 on MILL-PC: copy flash-8 sources, apply apply_flash9.py, keep each
file's CRLF, print LF-normalized sha256s.  Never compiles/uploads."""
import hashlib, shutil, subprocess, sys
from pathlib import Path
W = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
P8 = W / 'party8_20260928'; P9 = W / 'party9_20260928'
(P9 / 'prize_wheel_gpt').mkdir(parents=True, exist_ok=True)
crlf = {}
for f in (P8 / 'prize_wheel_gpt').iterdir():
    if f.is_file() and f.suffix in ('.ino', '.h'):
        shutil.copy2(f, P9 / 'prize_wheel_gpt' / f.name)
        crlf[f.name] = b'\r\n' in f.read_bytes()
for n in ('compile_candidate.py', 'upload_candidate.py'):
    shutil.copy2(P8 / n, P9 / n)
r = subprocess.run([sys.executable, str(P9 / 'apply_flash9.py')], capture_output=True, text=True)
print(r.stdout.strip(), r.stderr.strip(), 'apply exit', r.returncode)
if r.returncode != 0:
    raise SystemExit(1)
for n in ('prize_wheel_gpt.ino', 'pw_party_impl.h'):
    p = P9 / 'prize_wheel_gpt' / n
    b = p.read_bytes().replace(b'\r\n', b'\n')
    print('flash9', n, hashlib.sha256(b).hexdigest())
    if crlf[n]:
        b = b.replace(b'\n', b'\r\n')
    p.write_bytes(b)
print('crlf', {k: v for k, v in crlf.items() if k in ('prize_wheel_gpt.ino', 'pw_party_impl.h')})
