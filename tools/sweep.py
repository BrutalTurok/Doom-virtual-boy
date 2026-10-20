import sys, struct, random
sys.path.insert(0,os.path.dirname(os.path.abspath(__file__)))
from harness import VB
rom, mapf = sys.argv[1], sys.argv[2]
maps = [int(x) for x in sys.argv[3].split(',')] if len(sys.argv)>3 else list(range(1,10))
secs = int(sys.argv[4]) if len(sys.argv)>4 else 30
shotdir = sys.argv[5] if len(sys.argv)>5 else None
import os
L=int(os.environ.get('LOGSZ','4096'))
vb=VB(os.environ.get('BEETLE_VB_CORE','mednafen_vb_libretro.so'),rom,mapf,os.environ.get('GPRAM','0xFFFFFF'))
vb.logsz=L
OFF_ZPEAK=16; OFF_CMD=28+L; OFF_SBRK=OFF_CMD+4; OFF_FAIL=OFF_CMD+8
vb.run(150)
random.seed(1)
print("map zone_peak zone_now fps stack sbrk fail")
for m in maps:
    vb.memw(vb.field(OFF_CMD), struct.pack('<I',m))
    vb.run(150)
    vb.memw(vb.field(OFF_ZPEAK), struct.pack('<I',0))
    d0=vb.dbg(); f0=vb.frames
    t=0; worst=99
    while vb.frames-f0 < secs*50:
        k=random.choice([['UP'],['UP','B'],['UP','LEFT'],['UP','RIGHT'],['A'],['RLEFT','UP'],['RRIGHT'],['UP','A'],['LEFT'],['DOWN']])
        n=random.randint(15,60)
        a=vb.dbg()['doomframes']; vb.run(n,k); b=vb.dbg()['doomframes']
    d1=vb.dbg()
    fps=(d1['doomframes']-d0['doomframes'])*50/(vb.frames-f0)
    raw=vb.mem(vb.field(OFF_SBRK),8); sbrk,fail=struct.unpack('<II',raw)
    print(f"E1M{m} {d1['zone_peak']:7d} {d1['zone_used']:7d} {fps:5.1f} {vb.stack_used():6d} {sbrk-0x06000000:8d} {fail}")
    if shotdir: vb.shot(f"{shotdir}/E1M{m}.png")
    if fail: print(d1['log'][-300:]); break
