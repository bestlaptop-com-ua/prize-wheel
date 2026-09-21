from pathlib import Path
import serial,time,json
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
 assert 'v2-hold1650-20260918; 1650mA hold until next spin' in boot,'wrong build'
 assert 'allocated=1' in boot,'diagnostic PSRAM unavailable'
 assert '16 MHz correction OK' in boot,'STEP clock correction missing'
 p.write(b's');p.flush();status=read_for(.8)
 assert 'state=IDLE_STOPPED fault=NONE' in status,'controller not idle/healthy; no reset sent'
 assert 'pos=FRESH vel=VALID' in status,'encoder not ready'
 assert 'tmc=1' in status and 'dirCal=1' in status,'driver/calibration missing'
 assert 'fas=0.0000 current=0 EN=1' in status,'unexpected motor output at boot'
 print('PASS: 1650mA hold build, clock correction, PSRAM, driver, encoder, calibration and disabled idle outputs. Loaded hold validation pending.')
 print(status)
finally:
 (root/'hold1650_verified_boot.log').write_bytes(all_data)
 if p.is_open:p.close()
