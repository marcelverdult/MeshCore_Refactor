# Reads the log of the ASan probe (companion_asan_probe.patch) and prints one line per
# handler, first non-libc frame and access kind, with the codes, fills and lengths that
# hit it.
import sys,re,collections
c=collections.defaultdict(lambda:[0,set(),set(),set()])
cur=None
lines=open(sys.argv[1],errors='replace').read().split('\n')
i=0
while i<len(lines):
    l=lines[i]
    m=re.match(r'PROBE code=(\d+) fill=(\w+) len=(\d+)',l)
    if m: cur=m.groups()
    if 'ERROR: AddressSanitizer' in l:
        blk=lines[i+1:i+16]
        rw=[x for x in blk if x.startswith(('READ','WRITE'))][0].split(' at ')[0]
        fr=[re.sub(r'.* in ','',x).split('(')[0] for x in blk if re.match(r'\s+#\d',x)]
        h=next((f for f in fr if f.startswith('MyMesh::cmd')),'?')
        first=next((f for f in fr if 'asan' not in f and 'Interceptor' not in f and not re.match(r'(memcpy|memcmp|strlen|strcmp|strchr|strncmp|wrap_)',f)), '?')
        k=(h,first,rw.split(' of')[0])
        c[k][0]+=1
        if cur: c[k][1].add(cur[0]); c[k][2].add(cur[1]); c[k][3].add(int(cur[2]))
    i+=1
for k,v in sorted(c.items()):
    print(v[0],k,'codes',sorted(v[1],key=int),'fills',sorted(v[2]),'lens',min(v[3]) if v[3] else '-', '..', max(v[3]) if v[3] else '-')
