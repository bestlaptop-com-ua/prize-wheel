from pathlib import Path
p=Path(__file__).parent/'hold_capture.py'
s=p.read_text(encoding='utf-8')
s=s.replace("seen=0;pending=b'';rearm=False","seen=0;pending=b'';rearm=False;holdChecks=[]")
a="     if line.startswith(b'# DIAG n='):rearm=True"
b="""     if line.startswith(b'# DIAG n='):rearm=True
     if line.startswith(b'# HOLD current='):
      holdChecks=[time.monotonic()+delay for delay in (10,30,60)]
     if line.startswith(b'# HOLD released:') or line.startswith(b'FAULT code='):
      holdChecks=[]"""
assert s.count(a)==1;s=s.replace(a,b)
a='   if rearm:'
b="""   if holdChecks and time.monotonic()>=holdChecks[0]:
    holdChecks.pop(0);host('TX s (timed persistent-hold check)');p.write(b's');p.flush()
   if rearm:"""
assert s.count(a)==1;s=s.replace(a,b)
p.write_text(s,encoding='utf-8')
print('Hold logger schedules status checks after 10, 30 and 60 seconds; cancels on release/fault.')
