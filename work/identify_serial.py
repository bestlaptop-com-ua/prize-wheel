import serial,time
p=serial.Serial()
p.port="COM7"; p.baudrate=115200; p.timeout=0.2; p.dtr=False; p.rts=False
try:
    p.open()
    p.write(b"?")
    p.flush()
    end=time.monotonic()+3
    data=bytearray()
    while time.monotonic()<end:
        data.extend(p.read(p.in_waiting or 1))
    print(data.decode("utf-8",errors="replace") if data else "(no response to ?)")
finally:
    if p.is_open:p.close()
