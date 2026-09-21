import serial,time
p=serial.Serial()
p.port="COM7"; p.baudrate=115200; p.timeout=0.1; p.dtr=False; p.rts=False
try:
    p.open()
    for command in (b"s",b"f"):
        p.write(command);p.flush()
        end=time.monotonic()+1
        while time.monotonic()<end:
            b=p.read(p.in_waiting or 1)
            if b:print(b.decode("utf-8",errors="replace"),end="")
finally:
    if p.is_open:p.close()
