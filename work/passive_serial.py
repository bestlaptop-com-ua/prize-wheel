import serial,time
p=serial.Serial()
p.port="COM7"; p.baudrate=115200; p.timeout=0.2; p.dtr=False; p.rts=False
try:
    p.open()
    end=time.monotonic()+4
    data=bytearray()
    while time.monotonic()<end:
        data.extend(p.read(p.in_waiting or 1))
    print("PASSIVE SERIAL (no commands sent):")
    print(data.decode("utf-8",errors="replace") if data else "(no output)")
finally:
    if p.is_open:
        p.close()
