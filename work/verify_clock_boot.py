from pathlib import Path
import serial,time
root=Path(__file__).parent
p=serial.Serial();p.port='COM7';p.baudrate=115200;p.timeout=.1;p.dtr=False;p.rts=False
try:
 p.open()
 p.dtr=False;p.rts=True;time.sleep(.2);p.rts=False
 data=bytearray();end=time.monotonic()+6
 while time.monotonic()<end:data.extend(p.read(p.in_waiting or 1))
 p.write(b's');p.flush();end=time.monotonic()+1
 while time.monotonic()<end:data.extend(p.read(p.in_waiting or 1))
 text=data.decode('utf-8',errors='replace')
 (root/'wheel_clock_verified_boot.log').write_text(text,encoding='utf-8')
 print(text)
finally:
 if p.is_open:p.close()
