from pathlib import Path
p=Path(__file__).parent
s=(p/'upload_verify_smooth.py').read_text().replace('import subprocess,serial,time,traceback,hashlib','import subprocess,serial,time,traceback,hashlib,re')
s=s.replace("assert 'allocated=1' in boot and '16 MHz correction OK' in boot", "assert 'samples=16384' in boot and 'allocated=1' in boot and '16 MHz correction OK' in boot")
a="  assert 'state=IDLE_STOPPED fault=NONE' in status,'not idle; no fault reset attempted'"
b="""  # Only the known pre-update fault may be cleared, after fresh stopped
  # readings and disabled outputs; a different fault is not auto-cleared.
  if 'state=FAULT_LATCHED fault=SUSTAINED_SPEEDUP' in status:
   for _ in range(3):
    assert 'pos=FRESH vel=VALID' in status and 'fas=0.0000 current=0 EN=1' in status
    m=re.search(r'omega=([-0-9.]+)',status);assert m and abs(float(m.group(1)))<=0.005
    read_for(.4);port.write(b's');port.flush();status=read_for(.4)
   assert 'pos=FRESH vel=VALID' in status and 'fas=0.0000 current=0 EN=1' in status
   m=re.search(r'omega=([-0-9.]+)',status);assert m and abs(float(m.group(1)))<=0.005
   port.write(b'r');port.flush();cleared=read_for(1)
   assert '# fault cleared; tmc=1 dirCal=1' in cleared,'guarded reset failed'
   port.write(b's');port.flush();status=read_for(1)
  assert 'state=IDLE_STOPPED fault=NONE' in status,'not healthy after update'"""
assert a in s;s=s.replace(a,b)
s=s.replace("p/'smooth_wheel_upload.error'","p/'smooth_wheel_final_upload.error'")
(p/'upload_verify_smooth16k.py').write_text(s)
print('Guarded final deployment verifier prepared')