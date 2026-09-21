from pathlib import Path
p=Path(__file__).parent/'upload_verify_gentle320.py'
s=p.read_text();needle="try:\n assert (p/'gentle320_wheel_build.exit')"
assert needle in s
s=s.replace(needle,"try:\n assert not (p/'gentle320_DO_NOT_UPLOAD.txt').exists(), 'Rejected feasibility preflight; do not upload this candidate'\n assert (p/'gentle320_wheel_build.exit')",1)
p.write_bytes(s.replace('\n','\r\n').encode())