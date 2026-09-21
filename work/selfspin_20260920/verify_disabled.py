from pathlib import Path
import serial, time, traceback

root=Path(__file__).resolve().parent
data=bytearray()
port=serial.Serial(); port.port='COM7'; port.baudrate=115200; port.timeout=.05
port.dtr=False; port.rts=False
def read_for(seconds):
    start=len(data); end=time.monotonic()+seconds
    while time.monotonic()<end: data.extend(port.read(port.in_waiting or 1))
    return bytes(data[start:]).decode(errors='replace')
try:
    port.open(); read_for(.3)
    port.write(b'?'); help_text=read_for(2)
    assert '# build: v2-selfspin-LOCAL-CANDIDATE; based on gentle6' in help_text
    read_for(1.5)  # help printing temporarily interrupts the 1 kHz sampler
    port.write(b's'); status=read_for(1.5)
    assert 'motion_compiled=0 timer_ready=1 consumed=0 active=0 inhibit=1' in status
    assert 'state=IDLE_STOPPED fault=NONE' in status
    assert 'pos=FRESH vel=VALID' in status and 'dirCal=1' in status and 'tmc=1' in status
    assert 'fas=0.0000 current=0 EN=1' in status and 'diag_buffer=1' in status
    port.write(b'['); refused=read_for(1.5)
    assert '# SELFSPIN refused:' in refused and '# SELFSPIN START ' not in refused
    port.write(b's'); after=read_for(1)
    assert 'state=IDLE_STOPPED fault=NONE' in after
    assert 'motion_compiled=0 timer_ready=1 consumed=0 active=0 inhibit=1' in after
    assert 'fas=0.0000 current=0 EN=1' in after
    (root/'verify_disabled.exit').write_text('0')
    print('PASS disabled board: exact identity, healthy idle, timer ready, recorder buffer, EN high, start refused, no motion')
    print(after)
except Exception:
    (root/'verify_disabled.error').write_text(traceback.format_exc())
    (root/'verify_disabled.exit').write_text('1')
    raise
finally:
    (root/'verify_disabled_serial.log').write_bytes(data)
    if port.is_open: port.close()
