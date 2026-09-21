from pathlib import Path
p=Path(__file__).parent/'verify_rattle_idle.py'
s=p.read_text(encoding='utf-8-sig')
s=s.replace('v2-rattle-diag-20260918','v2-persistent-hold-20260918').replace('rattle_idle_test','hold_idle_test')
a=" assert 'state=IDLE_STOPPED fault=NONE' in status,'wheel not idle and healthy'"
b=""" if 'state=FAULT_LATCHED fault=LANDING_UNSAFE' in status:
  speed=re.search(r'omega=([-0-9.]+)',status)
  assert speed and abs(float(speed.group(1)))<=.02,'wheel moving; fault left latched'
  assert 'pos=FRESH vel=VALID' in status and 'stage=0' in status,'reset prerequisites missing'
  p.write(b'r');p.flush();reset=read_for(.8)
  assert 'fault cleared' in reset,'reset did not complete'
  p.write(b's');p.flush();status=read_for(.6)
 assert 'state=IDLE_STOPPED fault=NONE' in status,'wheel not idle and healthy'"""
assert s.count(a)==1;s=s.replace(a,b)
a=" dump='';end=time.monotonic()+20"
b=""" dump=read_for(.2)
 # Deliberately interleave a status request while the idle CSV drains.
 # Complete-line UART framing must preserve every CSV record.
 p.write(b's');p.flush()
 end=time.monotonic()+20"""
assert s.count(a)==1;s=s.replace(a,b)
(Path(__file__).parent/'verify_hold_idle.py').write_text(s,encoding='utf-8')
print('Prepared guarded idle upload test with old-fault reset and CSV framing check.')
