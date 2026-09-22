import serial, sys, os, time, threading
os.system('title PRIZE WHEEL serial monitor - COM7 - type a command + Enter')
D = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(D, 'monitor.log'); CMD = os.path.join(D, 'cmd.txt')
port = serial.Serial(port=None, baudrate=115200, timeout=0.05)
port.dtr = False; port.rts = False; port.port = 'COM7'; port.open()
log = open(LOG, 'ab')
lock = threading.Lock()
def send(txt, who):
    with lock:
        port.write(txt.encode() + b'\n')
        line = '>> [%s] %s' % (who, txt)
        print(line, flush=True); log.write(('\n' + line + '\n').encode()); log.flush()
def stdin_thread():
    for line in sys.stdin:
        line = line.rstrip('\r\n')
        if line: send(line, 'Timur')
threading.Thread(target=stdin_thread, daemon=True).start()
print('=== connected COM7 @115200. Type ? for help. Claude sends via cmd.txt. Log: ' + LOG, flush=True)
buf = b''
while True:
    b = port.read(4096)
    if b:
        log.write(b); log.flush()
        buf += b
        while b'\n' in buf:
            line, buf = buf.split(b'\n', 1)
            print(line.decode('ascii', 'replace').rstrip('\r'), flush=True)
    if os.path.exists(CMD):
        try:
            c = open(CMD).read().strip(); os.remove(CMD)
            if c: send(c, 'Claude')
        except Exception:
            pass
