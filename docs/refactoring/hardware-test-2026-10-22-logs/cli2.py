import serial,time,sys
# cli2.py PORT WAIT [-b] @file|cmd ... ; prints 'cmd => reply' (ESP32: no DTR/RTS, boot log optional)
p=sys.argv[1]; wait=float(sys.argv[2]); args=sys.argv[3:]
showboot = args[:1]==['-b']; args=args[1:] if showboot else args
cmds=[]
for a in args:
    if a.startswith('@'): cmds+= [l.strip() for l in open(a[1:]) if l.strip()]
    else: cmds.append(a)
s=serial.Serial(); s.port=p; s.baudrate=115200; s.timeout=0.1; s.dtr=False; s.rts=False; s.open()
t=time.time(); boot=b''
while time.time()-t<wait: boot+=s.read(4096)
if showboot: print('--- boot/console ---'); print(boot.decode(errors='replace')); print('---')
for c in cmds:
    s.reset_input_buffer(); s.write(c.encode()+b'\r'); t=time.time(); out=b''; rep=None
    while time.time()-t<3:
        out+=s.read(4096)
        txt=out.decode(errors='replace')
        if c in txt:
            rest=txt.split(c,1)[1]
            for l in rest.splitlines():
                if l.strip().startswith('->'): rep=l.strip()[2:].strip(); break
        if rep is not None: break
    if c=='reboot': rep='(sent)'
    print(f'{c} => {rep}')
s.close()
