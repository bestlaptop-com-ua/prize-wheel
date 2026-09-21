import sys,os
txt=open(os.path.join(sys.argv[1],'serial.raw'),'rb').read().decode('ascii','replace')
txt=txt[txt.rfind('#HOST sent D'):]
for ln in txt.splitlines():
    f=ln.split(',')
    if len(f)==23 and f[0]=='D':
        print('hand first raw',f[3],'counts',f[5]); break
import csv
for p in sys.argv[2:]:
    for r in csv.DictReader(open(p)):
        if r['record']=='D': print(p[-13:],'first raw',r['raw'],'counts',r['counts']); break
