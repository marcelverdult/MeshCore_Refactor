import serial, time, sys, os
# advrate.py ESP_PORT NRF_USB_SERIAL N : N adverts each way between a held ESP32 repeater console
# and an nRF52 repeater (nodetool); success = the receiver's neighbors line for the sender is <= 8 s old.
sys.path.insert(0, '/Users/verdi/Projects/MeshCore_Refactor/.claude/worktrees/gifted-murdock-f54535/tools/refactoring')
import nodetool
esp, nrf, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
s = serial.Serial(); s.port = esp; s.baudrate = 115200; s.timeout = 0.1; s.dtr = False; s.rts = False; s.open()
time.sleep(5); s.read(65536)

def ecli(c):
    s.reset_input_buffer(); s.write(c.encode() + b'\r'); t = time.time(); out = b''
    while time.time() - t < 3:
        out += s.read(4096); txt = out.decode(errors='replace')
        if c in txt:
            for l in txt.split(c, 1)[1].splitlines():
                if l.strip().startswith('->'): return l.strip()[2:].strip()
    return None

def ncli(c):
    return nodetool.text_cli(nrf, [c])

def nrf_reply(c):
    r = nodetool.text_cli(nrf, [c])
    return (r[0][1] or '') if r else ''

now = int(time.time())
print('esp time', ecli(f'time {now}')); print('nrf time', nrf_reply(f'time {now}'))
epk = ecli('get public.key').replace('>', '').strip()[:8]
npk = None
def nrf_reply(c):
    r = nodetool.text_cli(nrf, [c])
    return (r[0][1] or '') if r else ''
npk = nrf_reply('get public.key').replace('>', '').strip()[:8]
print('esp', epk, 'nrf', npk)

def heard(nb, pk):
    for e in (nb or '').split('\n'):
        f = e.strip().split(':')
        if len(f) == 3 and f[0].upper() == pk.upper() and int(f[1]) <= 8: return int(f[2]) / 4
    return None

ok1 = 0
for i in range(n):
    nrf_reply('advert'); time.sleep(6)
    r = heard(ecli('neighbors'), npk); ok1 += r is not None
    print(f'nrf->esp #{i+1}: {"OK snr=%s" % r if r is not None else "MISS"}', flush=True)
ok2 = 0
for i in range(n):
    ecli('advert'); time.sleep(6)
    r = heard(nrf_reply('neighbors'), epk); ok2 += r is not None
    print(f'esp->nrf #{i+1}: {"OK snr=%s" % r if r is not None else "MISS"}', flush=True)
print(f'RESULT nrf->esp {ok1}/{n}  esp->nrf {ok2}/{n}')
print('esp stats', ecli('stats-packets'), ecli('stats-radio'))
print('nrf stats', nrf_reply('stats-packets'), nrf_reply('stats-radio'))
s.close()
