"""Stage flash 13 on MILL-PC: copy flash-12 sources (+ pw_net.h already in place), apply apply_flash13.py, keep each
file's CRLF, print LF-normalized sha256s.  Never compiles/uploads."""
import hashlib, shutil, subprocess, sys
from pathlib import Path
W = Path(r'C:\Users\Mill\Desktop\prize-wheel\work')
PA = W / 'party12_20260930'; PB = W / 'party13_20260930'
(PB / 'prize_wheel_gpt').mkdir(parents=True, exist_ok=True)
crlf = {}
for f in (PA / 'prize_wheel_gpt').iterdir():
    if f.is_file() and f.suffix in ('.ino', '.h'):
        shutil.copy2(f, PB / 'prize_wheel_gpt' / f.name)
        crlf[f.name] = b'\r\n' in f.read_bytes()
for n in ('compile_candidate.py', 'upload_candidate.py'):
    shutil.copy2(PA / n, PB / n)
r = subprocess.run([sys.executable, str(PB / 'apply_flash13.py')], capture_output=True, text=True)
print(r.stdout.strip(), r.stderr.strip(), 'apply exit', r.returncode)
if r.returncode != 0:
    raise SystemExit(1)
for n in ('prize_wheel_gpt.ino', 'pw_party_impl.h', 'pw_party.h'):
    p = PB / 'prize_wheel_gpt' / n
    b = p.read_bytes().replace(b'\r\n', b'\n')
    print('flash13', n, hashlib.sha256(b).hexdigest())
    if crlf[n]:
        b = b.replace(b'\n', b'\r\n')
    p.write_bytes(b)
print('crlf', {k: v for k, v in crlf.items() if k in ('prize_wheel_gpt.ino', 'pw_party_impl.h', 'pw_party.h')})
import hashlib as _h
print('flash13 pw_net.h', _h.sha256((PB / 'prize_wheel_gpt' / 'pw_net.h').read_bytes().replace(b'\r\n', b'\n')).hexdigest())
