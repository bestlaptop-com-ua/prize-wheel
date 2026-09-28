"""Flash 7 (2026-09-28): boot volume 15 (gain 0.50) - Timur 17:17 "Do 15".
Applies to the flash-6 source (party6_20260925).  Exact-once anchors; aborts if any drifts.
Universal newlines; the caller restores CRLF on MILL-PC."""
from pathlib import Path
D = Path(__file__).resolve().parent / 'prize_wheel_gpt'


def once(name, old, new):
    p = D / name
    s = p.read_text(encoding='utf-8')
    n = s.count(old)
    assert n == 1, f'{name}: anchor count {n}: {old[:80]!r}'
    p.write_text(s.replace(old, new), encoding='utf-8', newline='\n')


once('pw_party.h',
     '#define PW_DFP_VOLUME 30       /* 0..30; live-adjust with V<n> + Enter; 30 = gain 1.0 */',
     '#define PW_DFP_VOLUME 15       /* 0..30; live-adjust with V<n> + Enter; 30 = gain 1.0; flash 7: 15 (owner 2026-09-28 "50% from current") */')
once('prize_wheel_gpt.ino',
     'Serial.println(F("# build: party6-20260925 (flash 6:',
     'Serial.println(F("# build: party7-20260928 (flash 7: boot volume 15 = gain 0.50) on party6-20260925 (flash 6:')
print('flash7 applied')
