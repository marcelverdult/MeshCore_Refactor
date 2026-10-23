import serial, time, sys, subprocess
# hold.py PORT BOOTWAIT step ... ; keeps the ESP32 text console open across all steps
#   step: "L:<cli cmd>" on the held port, "S:<shell cmd>" run in a shell, "W:<secs>" wait
p = sys.argv[1]; wait = float(sys.argv[2])
s = serial.Serial(); s.port = p; s.baudrate = 115200; s.timeout = 0.1; s.dtr = False; s.rts = False; s.open()
t = time.time(); boot = b''
while time.time() - t < wait: boot += s.read(4096)
print('[held console, boot/idle output]', ' / '.join(l for l in boot.decode(errors='replace').splitlines() if l.strip() and 'Wire.cpp' not in l)[:600])

def local(c):
    s.reset_input_buffer(); s.write(c.encode() + b'\r'); t = time.time(); out = b''; rep = None
    while time.time() - t < 3:
        out += s.read(4096); txt = out.decode(errors='replace')
        if c in txt:
            for l in txt.split(c, 1)[1].splitlines():
                if l.strip().startswith('->'): rep = l.strip()[2:].strip(); break
        if rep is not None: break
    print(f'[{p}] {c} => {rep}', flush=True)

for st in sys.argv[3:]:
    k, v = st[:2], st[2:]
    if k == 'L:': local(v)
    elif k == 'W:': time.sleep(float(v))
    elif k == 'S:':
        r = subprocess.run(v, shell=True, capture_output=True, text=True)
        print(r.stdout.rstrip(), r.stderr.rstrip(), flush=True)
s.close()
