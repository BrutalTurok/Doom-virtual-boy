import os
import sys,struct,random,bisect,collections,subprocess,ctypes as C
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from harness import VB
rom,elf,mapf=sys.argv[1],sys.argv[2],sys.argv[3]
maps=[int(x) for x in sys.argv[4].split(',')] if len(sys.argv)>4 else [1,3,6]
nm=subprocess.run([os.path.join(os.environ.get('TC','/opt/v810/bin'),'v810-nm'),'-n',elf],capture_output=True,text=True).stdout
syms=[(int(a,16),n) for a,t,n in (l.split() for l in nm.splitlines() if len(l.split())==3) if t in 'tTwW' and int(a,16)>=0x07000000]
syms.sort(); addrs=[a for a,_ in syms]
vb=VB(os.environ.get('BEETLE_VB_CORE','mednafen_vb_libretro.so'),rom,mapf,'0xFFFF'); vb.logsz=256
base=vb.core.retro_get_memory_data(0)
hist=collections.Counter(); total=0
idx_addr=vb.syms['vbprof_idx']
def drain(last):
    global total
    cur=struct.unpack('<I',vb.mem(idx_addr,4))[0]
    buf=C.string_at(base,0x10000)
    i=last
    while i!=cur:
        pc=struct.unpack_from('<I',buf,i*4)[0]
        k=bisect.bisect_right(addrs,pc)-1
        hist[syms[k][1] if k>=0 else hex(pc)]+=1; total+=1
        i=(i+1)&0x3FFF
    return cur
vb.run(150); last=struct.unpack('<I',vb.mem(idx_addr,4))[0]
random.seed(7)
for m in maps:
    vb.memw(vb.field(28+256),struct.pack('<I',m)); vb.run(150); last=drain(struct.unpack('<I',vb.mem(idx_addr,4))[0])
    for i in range(60):
        vb.run(random.randint(10,30), random.choice([['UP'],['UP','L'],['LEFT'],['RIGHT'],['UP','RIGHT']]))
        last=drain(last)
print('samples',total)
for n,c in hist.most_common(30): print(f"{100*c/total:5.1f}% {n}")
