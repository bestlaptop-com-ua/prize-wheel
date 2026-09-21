import serial,time,pathlib,datetime,sys
root=pathlib.Path(__file__).parent
port=serial.Serial()
port.port='COM7';port.baudrate=115200;port.timeout=.05;port.dtr=False;port.rts=False
data=bytearray()
def read_for(seconds):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        data.extend(port.read(port.in_waiting or 1))
try:
    port.open()
    read_for(.5)
    for command in (b'?',b's',b'f'):
        port.write(command);port.flush();read_for(.7)
    read_for(.5)
finally:
    if port.is_open:port.close()
    raw=bytes(data)
    (root/'ccw_20260920_initial_status.log').write_bytes(raw)
    print('HOST '+datetime.datetime.now().isoformat())
    print(raw.decode('utf-8',errors='replace'))
