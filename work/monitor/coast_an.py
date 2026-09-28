import sys, re
LOG = r'C:\Users\Mill\Desktop\prize-wheel\work\monitor\monitor.log'
START = int(sys.argv[1]) if len(sys.argv) > 1 else 19302
lines = open(LOG, encoding='utf-8', errors='replace').read().splitlines()[START:]
cols = None; dumps = []; cur = None; last_spin = None
for ln in lines:
    m = re.match(r'SPIN#(\d+) SUMMARY', ln)
    if m: last_spin = int(m.group(1))
    if ln.startswith('# DIAG columns:'):
        cols = ln.split(':', 1)[1].strip().split(',')
        cur = {'spin': last_spin, 'rows': []}; dumps.append(cur); continue
    if ln.startswith('D,') and cur is not None:
        f = ln[2:].split(',')
        if len(f) == len(cols):
            cur['rows'].append(dict(zip(cols, f)))
    if ln.startswith('# DIAG n='): cur = None
for d in dumps:
    R = d['rows']
    if not R: continue
    t0 = int(R[0]['done_us']); T = [(int(r['done_us']) - t0) / 1e6 for r in R]
    W = [int(r['omega_mrev']) / 1000 for r in R]
    C = [int(r['counts']) for r in R]
    S = [r['state'] for r in R]; I = [r['current_ma'] for r in R]
    tend = T[-1]
    # last moving sample
    mov = [i for i, w in enumerate(W) if abs(w) > 0.02]
    stop_i = mov[-1] if mov else 0
    print(f"=== dump spin#{d['spin']} n={len(R)} span={tend:.2f}s states={sorted(set(S))} currents={sorted(set(I))}")
    print(" t_rel(s) omega(rev/s) travel(deg) decel_100ms(rev/s2)")
    step = 100
    for i in range(0, len(R), step):
        j = min(i + step, len(R) - 1)
        dec = (W[i] - W[j]) / (T[j] - T[i]) if T[j] > T[i] else 0
        trav = (C[i] - C[0]) * 360 / 4096
        print(f" {T[i]-T[stop_i]:+6.2f} {W[i]:+7.3f} {trav:+8.1f} {dec:+7.3f}")
    # reversals after stop
    signs = [1 if w > 0.005 else -1 if w < -0.005 else 0 for w in W]
    flips = sum(1 for a, b in zip(signs, signs[1:]) if a and b and a != b)
    tot = (C[-1] - C[stop_i]) * 360 / 4096
    print(f" last moving at t={T[stop_i]:.2f}s; sign flips={flips}; drift after stop={tot:+.1f} deg")
    # band decels
    def cross(v):
        for i, w in enumerate(W):
            if abs(w) <= v: return i
        return None
    bands = [0.5, 0.4, 0.3, 0.2, 0.1, 0.05]
    out = []
    for a, b in zip(bands, bands[1:]):
        i, j = cross(a), cross(b)
        if i is not None and j is not None and j > i:
            out.append(f"{a}->{b}: {(abs(W[i])-abs(W[j]))/(T[j]-T[i]):.3f} rev/s2 over {(C[j]-C[i])*360/4096:+.1f} deg")
    print(' bands:', '; '.join(out))
