import os
"""Libretro (Beetle VB) test harness: run a VB ROM, script inputs, read debug block, save screenshots."""
import ctypes as C, sys, re, struct, os
from PIL import Image

class VB:
    KEYS={'A':8,'B':0,'START':3,'SELECT':2,'UP':4,'DOWN':5,'LEFT':6,'RIGHT':7,'L':10,'R':11,
          'RUP':12,'RDOWN':13,'RLEFT':14,'RRIGHT':15}
    def __init__(self, core, rom, mapfile=None, gpram_mask='0xFFFFFF'):
        os.environ['VB_GPRAM_MASK']=gpram_mask
        self.core=core=C.CDLL(core)
        self.img=None; self.pf=1; self.pressed=set(); self.keep={}
        self.noop=C.CDLL(os.path.join(os.path.dirname(os.path.abspath(__file__)),'noop.so'))
        ENV=C.CFUNCTYPE(C.c_bool,C.c_uint,C.c_void_p)
        VID=C.CFUNCTYPE(None,C.c_void_p,C.c_uint,C.c_uint,C.c_size_t)
        AUD=C.CFUNCTYPE(None,C.c_int16,C.c_int16)
        AUDB=C.CFUNCTYPE(C.c_size_t,C.c_void_p,C.c_size_t)
        POLL=C.CFUNCTYPE(None)
        STATE=C.CFUNCTYPE(C.c_int16,C.c_uint,C.c_uint,C.c_uint,C.c_uint)
        opts={b'vb_3dmode':b'side-by-side', b'vb_anaglyph_preset':b'disabled', b'vb_color_mode':b'black & red',
              b'vb_right_analog_to_digital':b'disabled'}
        def env(cmd,data):
            c=cmd&0xffff
            if c==27:
                C.cast(data,C.POINTER(C.c_void_p))[0]=C.cast(self.noop.noop_log,C.c_void_p).value; return True
            if c==10: self.pf=C.cast(data,C.POINTER(C.c_int))[0]; return True
            if c==15:
                key=C.cast(data,C.POINTER(C.c_char_p))[0]; val=opts.get(key)
                if val is None: return False
                self.keep.setdefault(key,C.c_char_p(val))
                C.cast(data+8,C.POINTER(C.c_void_p))[0]=C.cast(self.keep[key],C.c_void_p).value
                return True
            if c==9:
                self.keep.setdefault('sys',C.c_char_p(b'/tmp'))
                C.cast(data,C.POINTER(C.c_void_p))[0]=C.cast(self.keep['sys'],C.c_void_p).value; return True
            return False
        def vid(d,w,h,p):
            if not d: return
            buf=C.string_at(d,p*h)
            self.img=Image.frombuffer('RGBX',(w,h),buf,'raw','BGRX',p,1).convert('RGB') if self.pf==1 \
                else Image.frombuffer('RGB',(w,h),buf,'raw','BGR;16',p,1)
        def st(port,dev,idx,id): return 1 if (port==0 and id in self.pressed) else 0
        self.audio=bytearray(); self.capture=False
        def audb(d,n):
            if self.capture: self.audio+=C.string_at(d,n*4)
            return n
        self._cbs=[ENV(env),VID(vid),AUD(lambda l,r:None),AUDB(audb),POLL(lambda:None),STATE(st)]
        core.retro_set_environment(self._cbs[0]); core.retro_set_video_refresh(self._cbs[1])
        core.retro_set_audio_sample(self._cbs[2]); core.retro_set_audio_sample_batch(self._cbs[3])
        core.retro_set_input_poll(self._cbs[4]); core.retro_set_input_state(self._cbs[5])
        core.retro_init()
        class GI(C.Structure): _fields_=[('path',C.c_char_p),('data',C.c_void_p),('size',C.c_size_t),('meta',C.c_char_p)]
        data=open(rom,'rb').read(); self._rom=C.create_string_buffer(data)
        gi=GI(rom.encode(),C.cast(self._rom,C.c_void_p),len(data),None)
        assert core.retro_load_game(C.byref(gi))
        core.retro_get_memory_data.restype=C.c_void_p; core.retro_get_memory_size.restype=C.c_size_t
        self.syms={}
        if mapfile:
            for m in re.finditer(r'^\s+0x([0-9a-f]+)\s+(_\w+)\s*$', open(mapfile).read(), re.M):
                self.syms[m.group(2)[1:]]=int(m.group(1),16)
        self.frames=0
    def mem(self, addr, n):
        if 0x05000000<=addr<0x06000000:
            base=self.core.retro_get_memory_data(2); return C.string_at(base+(addr&0xFFFF), n)
        if 0x06000000<=addr<0x07000000:
            base=self.core.retro_get_memory_data(0); sz=self.core.retro_get_memory_size(0)
            return C.string_at(base+((addr-0x06000000)%sz), n)
        raise ValueError(hex(addr))
    def run(self, frames, keys=()):
        self.pressed={self.KEYS[k] for k in keys}
        for _ in range(frames): self.core.retro_run(); self.frames+=1
    def shot(self, path):
        if self.img: self.img.save(path)
    def dbg(self):
        L=getattr(self,'logsz',4096); a=self.syms['vbdebug']; raw=self.mem(a, 28+L)
        magic,vbf,dof,zu,zp,zs,lp=struct.unpack('<7I',raw[:28])
        log=raw[28:]; 
        if lp<=L: txt=log[:lp]
        else: s=lp%L; txt=log[s:]+log[:s]
        return dict(magic=hex(magic),vbframes=vbf,doomframes=dof,zone_used=zu,zone_peak=zp,log=txt.decode('latin1'))

def _memw(self, addr, data):
    if 0x06000000<=addr<0x07000000:
        base=self.core.retro_get_memory_data(0); sz=self.core.retro_get_memory_size(0)
        C.memmove(base+((addr-0x06000000)%sz), data, len(data)); return
    if 0x05000000<=addr<0x06000000:
        base=self.core.retro_get_memory_data(2); C.memmove(base+(addr&0xFFFF), data, len(data)); return
    raise ValueError(hex(addr))
VB.memw=_memw
def _field(self, off): return self.syms['vbdebug']+off
VB.field=_field
def _stack_used(self):
    w=self.mem(0x05000000, 0xFFC0)
    i=0
    while i<len(w) and w[i]==0: i+=1
    return 0xFFC0 - i
VB.stack_used=_stack_used
