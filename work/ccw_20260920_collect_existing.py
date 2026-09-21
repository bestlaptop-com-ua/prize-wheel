import base64
import hashlib
import json
import pathlib
import zlib

root = pathlib.Path(__file__).resolve().parent.parent
names = [
    'brake2200_spin1.csv', 'brake2200_spin6.csv', 'coast_spin7.csv',
    'speedup_spin1.csv', 'speedup_spin2.csv',
    'analyze_brake2200_entry.py', 'analyze_brake2200_spin1.py',
    'analyze_brake2200_spin6.py', 'analyze_speedup_spin1.py',
    'analyze_speedup_spin2.py', 'fit_coast_spin7.py', 'coast_spin7_fit.json',
    'clock_audit.log', 'smooth_audit_verified_serial.log',
    'speedup_replay_serial.log', 'brake2200_events.log', 'speedup_events.log',
]
files = []
for name in names:
    path = root / 'work' / name
    if path.is_file():
        data = path.read_bytes()
        files.append(dict(path='work/' + name, sha256=hashlib.sha256(data).hexdigest(),
                          zlib_base64=base64.b64encode(zlib.compress(data, 9)).decode('ascii')))
print(json.dumps(dict(files=files), separators=(',', ':')))
