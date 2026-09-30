import re, collections
LOG = r'C:\Users\Mill\Desktop\prize-wheel\work\monitor\monitor.log'
L = open(LOG, encoding='utf-8', errors='replace').read().splitlines()
# segment by build banners
starts = [i for i, l in enumerate(L) if l.startswith('# build: party1')]
idx10 = max(i for i in starts if 'party10' in L[i][:30]) if any('party10' in L[i][:30] for i in starts) else 0
sections = {}
cur = None
for i, l in enumerate(L):
    m = re.match(r'# build: (party\d+)', l)
    if m: cur = m.group(1)
    if cur in ('party10', 'party11') and re.match(r'SPIN#\d+ SUMMARY', l):
        d = dict(re.findall(r'(\w+)=([-+\w.]+)', l))
        sections.setdefault(cur, []).append(d)
dares = {2, 7, 12, 15}
for sec in ('party10', 'party11'):
    S = sections.get(sec, [])
    fin = collections.Counter(); tgt = collections.Counter(); q = collections.Counter(); qw = collections.defaultdict(collections.Counter)
    n = 0
    for d in S:
        if d.get('result') in ('GUEST_RESPUN',): continue
        n += 1
        w = int(d['finalW']) + 1; fin[w] += 1
        if d.get('captureEnergized') == '1':
            t = int(d['targetW']) + 1; tgt[t] += 1; q[d['quality']] += 1; qw[d['quality']][t] += 1
    print(f'== {sec}: {n} spins (labels 1-18, dares 3/8/13/16)')
    print(' final :', ' '.join(f'{w}:{fin[w]}' for w in range(1, 19)))
    print(' target:', ' '.join(f'{w}:{tgt[w]}' for w in range(1, 19)))
    print(' quality:', dict(q))
    for qq in sorted(qw): print('  q', qq, ' '.join(f'{w}:{qw[qq][w]}' for w in range(1, 19) if qw[qq][w]))
