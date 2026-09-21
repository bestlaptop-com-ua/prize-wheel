from pathlib import Path
import serial,time,traceback
p=Path(__file__).parent
port=serial.Serial();port.port='COM7';port.baudrate=115200;port.timeout=.05;port.dtr=False;port.rts=False
try:
 port.open();port.dtr=False;port.rts=True;time.sleep(.2);port.rts=False
 data=bytearray();end=time.monotonic()+35
 while time.monotonic()<end:
  data.extend(port.read(port.in_waiting or 1))
  if b'AUDIT PASS ALL; MOTOR DISABLED\r\n' in data or b'AUDIT FAIL ramp; MOTOR DISABLED\r\n' in data:break
 (p/'smooth_audit_verified_serial.log').write_bytes(data)
 assert b'AUDIT PASS ALL; MOTOR DISABLED' in data,'audit did not pass'
 (p/'smooth_audit_verified.exit').write_text('0')
except Exception:(p/'smooth_audit_verify.error').write_text(traceback.format_exc())
finally:
 if port.is_open:port.close()