import re
LOG = r'C:\Users\Mill\Desktop\prize-wheel\work\monitor\monitor.log'
L = open(LOG, encoding='utf-8', errors='replace').read().splitlines()
rows = []; ramp = None; pulse = None
for i, ln in enumerate(L):
    if ln.startswith('# PULSEFIRST TORQUE_ON'): pulse = ln
    if ln.startswith('# RAMP_STOP'): ramp = ln
    if re.match(r'SPIN#\d+ SUMMARY', ln):
        d = dict(re.findall(r'(\w+)=([-+\w.]+)', ln))
        d['spin'] = re.match(r'SPIN#(\d+)', ln).group(1); d['line'] = i
        if d.get('captureEnergized') == '1':
            m = re.search(r'remain=([\d.-]+) lead=([\d.-]+) fas=([\d.-]+)', ramp or '')
            d['r_remain'], d['r_lead'], d['r_fas'] = m.groups() if m else ('', '', '')
            m = re.search(r' a=(\d+)', pulse or ''); d['a'] = m.group(1) if m else ''
            rows.append(d)
        ramp = pulse = None
rows = rows[-120:]
import statistics as st
E=[(int(d['dir']),d['quality'],float(d['err']),float(d['final'])) for d in rows if d.get('dir') and d['result'] not in ('GUEST_RESPUN',) and abs(float(d['err']))<20]
for q in sorted(set(x[1] for x in E)):
    v=[x[2] for x in E if x[1]==q]; print('q',q,'n',len(v),'err mean %.1f sd %.1f'%(st.mean(v),st.pstdev(v)))
near=[x for x in E if min(x[3]%20,20-x[3]%20)<2.0]; print('within 2 deg of a line:',len(near),'of',len(E))
print('line spin dir q tk a remain lead tgt final err result  dLineAhead dLineBehind')
for d in rows:
    try:
        f = float(d['final']); dr = int(d['dir'])
    except: continue
    pos = f % 20.0
    ahead = (20 - pos) if dr > 0 else pos      # distance to next line in travel direction
    behind = pos if dr > 0 else (20 - pos)
    print(f"{d['line']} {d['spin']} {dr:+d} {d['quality']} {d['tkSpeed']} {d['a']} {d['r_remain']} {d['r_lead']} {d['targetAngle']} {f:.1f} {d['err']} {d['result']} {ahead:.1f} {behind:.1f}")
