import sys
rows=[l.split(',') for l in open(sys.argv[1]).read().splitlines() if l.startswith('D,')]
t0=int(rows[0][1])
print('t_ms raw counts omega cmd fas state stage mA')
step=max(1,len(rows)//70)
for i in range(0,len(rows),step):
    r=rows[i]; print((int(r[1])-t0)//1000, r[3], r[5], r[7], r[9], r[10], r[13], r[14], r[15])
