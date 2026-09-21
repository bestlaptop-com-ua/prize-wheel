import datetime
import os
import pathlib
import serial
import time
import traceback

root = pathlib.Path(__file__).parent
prefix = 'ccw_20260920'
port = serial.Serial()
port.port = 'COM7'
port.baudrate = 115200
port.timeout = .05
port.dtr = False
port.rts = False

def event(message):
    with (root / (prefix + '_events.log')).open('a', encoding='utf-8') as output:
        output.write(datetime.datetime.now().isoformat(timespec='milliseconds') + ' ' + message + '\n')

(root / (prefix + '_capture.pid')).write_text(str(os.getpid()))
pending = b''
seen = 0
try:
    port.open()
    event('OPEN COM7, DTR/RTS false; 30-minute lease; read/diagnostic commands only')
    with (root / (prefix + '_serial.log')).open('ab', buffering=0) as log:
        for command in (b'?', b's', b'f', b'd'):
            port.write(command)
            port.flush()
            event('TX ' + command.decode())
            time.sleep(.1)
        deadline = time.monotonic() + 1800
        while time.monotonic() < deadline:
            data = port.read(port.in_waiting or 1)
            if data:
                log.write(data)
                pending += data
                while b'\n' in pending:
                    line, pending = pending.split(b'\n', 1)
                    if not line.startswith(b'D,'):
                        event('RX ' + line.decode('utf-8', errors='replace').strip())
            mail = root / (prefix + '_commands.txt')
            try:
                commands = mail.read_text(encoding='utf-8-sig').splitlines() if mail.exists() else []
            except (PermissionError, FileNotFoundError):
                commands = []
            quit_requested = False
            while seen < len(commands):
                command = commands[seen].strip()
                seen += 1
                if command == 'quit':
                    quit_requested = True
                    break
                if command in ('?', 's', 'f', 'd'):
                    event('TX ' + command)
                    port.write(command.encode())
                    port.flush()
                else:
                    event('REJECTED command')
            if quit_requested:
                break
        event('CLOSED')
except Exception:
    event(traceback.format_exc())
finally:
    if port.is_open:
        port.close()
