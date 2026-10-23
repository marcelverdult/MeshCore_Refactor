import serial, struct, sys, time
sys.stdout.reconfigure(line_buffering=True)
# usage: comp.py PORT action [args] ; actions joined by ' ;; '
#  info | contacts | advert | flood | send <hexprefix> <text> | sync | cli <text> | reboot | listen <sec> | time | settime
p = sys.argv[1]
s = serial.Serial(); s.port = p; s.baudrate = 115200; s.timeout = 0.05; nrf = 'usbmodem' in p; s.dtr = nrf; s.rts = False
s.open(); time.sleep(0.3 if nrf else 5); s.read(65536)
buf = b''

def frames(timeout):
    global buf
    end = time.time() + timeout
    while time.time() < end:
        buf += s.read(4096)
        while True:
            i = buf.find(b'>')
            if i < 0: buf = b''; break
            buf = buf[i:]
            if len(buf) < 3: break
            n = struct.unpack_from('<H', buf, 1)[0]
            if n > 300: buf = buf[1:]; continue
            if len(buf) < 3 + n: break
            f = buf[3:3 + n]; buf = buf[3 + n:]
            yield f
            end = max(end, time.time() + 0.3)

def tx(payload):
    s.write(b'<' + struct.pack('<H', len(payload)) + payload)

def show(f):
    c = f[0]
    if c == 5:
        tx_pow, max_pow = f[2], f[3]; pk = f[4:36].hex()
        lat, lon = struct.unpack_from('<ii', f, 36)
        freq, bw = struct.unpack_from('<II', f, 48); sf, cr = f[56], f[57]
        name = f[58:].split(b'\0')[0].decode(errors='replace')
        return f'SELF_INFO name={name} pub={pk} tx={tx_pow}/{max_pow} radio={freq/1000:.3f},{bw/1000},{sf},{cr} lat={lat} lon={lon}'
    if c in (3, 0x8A, 0x80):
        if c == 0x80: return f'PUSH_ADVERT pub={f[1:33].hex()}'
        pk = f[1:33].hex(); typ = f[33]; opl = f[35]
        name = f[100:132].split(b'\0')[0].decode(errors='replace'); la = struct.unpack_from('<I', f, 132)[0]
        return f'{"CONTACT" if c==3 else "PUSH_NEW_ADVERT"} name={name} type={typ} out_path_len={opl} last_advert={la} pub={pk}'
    if c == 2: return f'CONTACTS_START count={struct.unpack_from("<I",f,1)[0]}'
    if c == 4: return 'END_OF_CONTACTS'
    if c == 6: return f'SENT flood={f[1]} ack={f[2:6].hex()} timeout_ms={struct.unpack_from("<I",f,6)[0]}'
    if c == 16:
        snr = struct.unpack_from('b', f, 1)[0] / 4
        return f'MSG_V3 snr={snr} from={f[4:10].hex()} path_len={f[10]} type={f[11]} ts={struct.unpack_from("<I",f,12)[0]} text={f[16:].decode(errors="replace")!r}'
    if c == 7: return f'MSG from={f[1:7].hex()} path_len={f[7]} type={f[8]} ts={struct.unpack_from("<I",f,9)[0]} text={f[13:].decode(errors="replace")!r}'
    if c == 0x81: return f'PUSH_PATH_UPDATED pub={f[1:33].hex()[:12]}'
    if c == 10: return 'NO_MORE_MESSAGES'
    if c == 0x82: return f'PUSH_SEND_CONFIRMED ack={f[1:5].hex()} rtt_ms={struct.unpack_from("<I",f,5)[0]}'
    if c == 0x83: return 'PUSH_MSG_WAITING'
    if c == 0x88:
        return f'PUSH_LOG_RX snr={struct.unpack_from("b",f,1)[0]/4} rssi={struct.unpack_from("b",f,2)[0]} raw={f[3:].hex()[:40]}..'
    if c == 29: return f'CLI_REPLY {f[1:].decode(errors="replace")!r}'
    if c == 9: return f'CURR_TIME {struct.unpack_from("<I",f,1)[0]}'
    if c == 0: return 'OK'
    if c == 1: return f'ERR {f[1:].hex()}'
    return f'FRAME code={c} {f.hex()[:80]}'

def run(act):
    a = act.split(' ', 1); cmd = a[0]; arg = a[1] if len(a) > 1 else ''
    if cmd == 'info': tx(b'\x01\x03' + b'\0' * 6 + b'hwtest')
    elif cmd == 'contacts': tx(b'\x04')
    elif cmd == 'advert': tx(b'\x07\x00')
    elif cmd == 'flood': tx(b'\x07\x01')
    elif cmd == 'sync': tx(b'\x0a')
    elif cmd == 'time': tx(b'\x05')
    elif cmd == 'settime': tx(b'\x06' + struct.pack('<I', int(time.time())))
    elif cmd == 'reboot': tx(b'\x13reboot')
    elif cmd == 'cli': tx(b'\x42' + arg.encode())
    elif cmd == 'send':
        pre, text = arg.split(' ', 1)
        tx(b'\x02\x00\x00' + struct.pack('<I', int(time.time())) + bytes.fromhex(pre)[:6] + text.encode())
    elif cmd == 'listen': pass
    elif cmd == 'shell':
        import subprocess; print(f'>> {act}'); r = subprocess.run(arg, shell=True, capture_output=True, text=True); print(r.stdout.rstrip(), r.stderr.rstrip()); return
    wait = float(arg) if cmd == 'listen' else (3 if cmd in ('contacts',) else 2)
    if cmd == 'send': wait = 12
    print(f'>> {act}')
    for f in frames(wait):
        print('   ', show(f))
        if cmd == 'reboot': break

for act in ' '.join(sys.argv[2:]).split(' ;; '):
    run(act)
s.close()
