p=open('apply_imbalance.py').read()
j=p.index("def main():")
k=p.index("    if problems:")
new=r'''
HEADER_EDITS = [
 ('arm hz ceiling', 'started = hz >= 40 && hz <= 640; // diagnostic .20 rps ceiling, never > spin-up',
                    'started = hz >= 40 && hz <= 2400; // party window: up to 0.72 rev/s at 3200 usteps/rev'),
 ('arm wheel ceiling', '        forwardRevS > 0.20f) return reject(ENCODER);',
                       '        forwardRevS > 0.75f) return reject(ENCODER);'),
 ('arm pulse window', '    if (position < previousPosition || total < 0 || total > 128)',
                      '    if (position < previousPosition || total < 0 || total > 512)'),
 ('overspeed ceiling', '  return isfinite(wheelRevS) && fabsf(wheelRevS) > 0.30f;',
                       '  return isfinite(wheelRevS) && fabsf(wheelRevS) > 0.80f;'),
]

'''
p=p[:j]+new+p[j:]
p=p.replace("    if problems:", "    arm = (SRC / 'pw_capture_arm.h').read_bytes().decode('utf-8').replace('\\r\\n', '\\n')\n    for name, old, new in HEADER_EDITS:\n        if arm.count(old) != 1:\n            problems.append('%s: anchor found %d times' % (name, arm.count(old)))\n    if problems:",1)
p=p.replace("    shutil.copy2(header, DST / 'pw_imbalance.h')", "    shutil.copy2(header, DST / 'pw_imbalance.h')\n    for name, old, new in HEADER_EDITS:\n        arm = arm.replace(old, new)\n    (DST / 'pw_capture_arm.h').write_bytes((arm.replace('\\n', '\\r\\n') if crlf else arm).encode('utf-8'))",1)
open('apply_imbalance.py','w').write(p)
