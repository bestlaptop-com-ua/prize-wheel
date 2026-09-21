from pathlib import Path
import serial,time,re,json
root=Path(__file__).parent
p=serial.Serial();p.port='COM7';p.baudrate=115200;p.timeout=.05;p.dtr=False;p.rts=False
all_data=bytearray()
def read_for(seconds):
 data=bytearray();end=time.monotonic()+seconds
 while time.monotonic()<end:data.extend(p.read(p.in_waiting or 1))
 all_data.extend(data);return data.decode('utf-8',errors='replace')
try:
 p.open();p.dtr=False;p.rts=True;time.sleep(.2);p.rts=False
 boot=read_for(4)
 assert 'v2-persistent-hold-20260918' in boot,'wrong build'
 assert 'allocated=1' in boot,'diagnostic PSRAM allocation failed'
 assert '16 MHz correction OK' in boot,'clock correction not verified'
 p.write(b's');p.flush();status=read_for(.6)
 if 'state=FAULT_LATCHED fault=LANDING_UNSAFE' in status:
  speed=re.search(r'omega=([-0-9.]+)',status)
  assert speed and abs(float(speed.group(1)))<=.02,'wheel moving; fault left latched'
  assert 'pos=FRESH vel=VALID' in status and 'stage=0' in status,'reset prerequisites missing'
  p.write(b'r');p.flush();reset=read_for(.8)
  assert 'fault cleared' in reset,'reset did not complete'
  p.write(b's');p.flush();status=read_for(.6)
 assert 'state=IDLE_STOPPED fault=NONE' in status,'wheel not idle and healthy'
 assert 'pos=FRESH vel=VALID' in status,'encoder not ready'
 p.write(b'd');p.flush();armed=read_for(.35)
 assert '8192 PSRAM samples' in armed,'capture not armed'
 p.write(b'D');p.flush()
 dump=read_for(.2)
 # Deliberately interleave a status request while the idle CSV drains.
 # Complete-line UART framing must preserve every CSV record.
 p.write(b's');p.flush()
 end=time.monotonic()+20
 while time.monotonic()<end:
  dump+=read_for(.1)
  if re.search(r'# DIAG n=\d+ wrapped=\d+ frozen=\d+\s',dump):break
 rows=[line.split(',')[1:] for line in dump.splitlines() if line.startswith('D,')]
 trailer=re.search(r'# DIAG n=(\d+) wrapped=(\d+) frozen=(\d+)',dump)
 assert trailer and len(rows)==int(trailer.group(1)),'incomplete dump'
 assert len(rows)>=100,'too few samples'
 assert all(len(r)==22 for r in rows),'malformed diagnostic row'
 assert all(int(r[21],16)&1 for r in rows),'EN not disabled'
 assert all(int(r[15])==0 for r in rows),'unexpected STEP motion'
 assert all(int(r[13])==0 for r in rows),'unexpected current stage'
 p.write(b's');p.flush();final=read_for(.6)
 assert 'state=IDLE_STOPPED fault=NONE' in final,'post-dump health check failed'
 report={'samples':len(rows),'columns':22,'psram':True,'clock_fix':True,'all_EN_high':True,'all_step_counts_zero':True,'all_freewheel':True,'driver_status_values':sorted(set(r[17] for r in rows)),'gstat_values':sorted(set(r[20] for r in rows)),'max_dt_us':max(int(r[1]) for r in rows)}
 (root/'hold_idle_test.json').write_text(json.dumps(report,indent=2))
 print(json.dumps(report,indent=2));print(final)
finally:
 (root/'hold_idle_test.log').write_bytes(all_data)
 if p.is_open:p.close()
