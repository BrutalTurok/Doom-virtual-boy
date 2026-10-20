from PIL import Image
W,H=264,176
def to_bwr(png, out):
    im=Image.open(png).convert('RGB'); assert im.size==(W,H)
    p=im.load(); data=bytearray(W*H//4)
    for c in range(W):
        x=W-1-c
        for y in range(H):
            r,g,b=p[x,y]
            code=1 if (r>128 and g<128) else (2 if (r+g+b)/3>128 else 0)
            i=c*H+y; data[i>>2]|=code<<(6-2*(i&3))
    open(out,'wb').write(data); return data
def from_bwr(data):
    im=Image.new('RGB',(W,H)); p=im.load(); cols=[(0,0,0),(255,0,0),(255,255,255),(0,255,0)]
    for i in range(W*H):
        c,y=divmod(i,H); x=W-1-c
        p[x,y]=cols[(data[i>>2]>>(6-2*(i&3)))&3]
    return im
def rot_cw(g): return [list(r) for r in zip(*g[::-1])]
def to_icon(px):  # px[y][x], 1=white
    g=px
    for _ in range(3): g=rot_cw(g)
    out=bytearray()
    for y in range(16):
        v=0
        for x in range(16): v=(v<<1)|g[y][x]
        out+=v.to_bytes(2,'big')
    return bytes(out)
def from_icon(b):
    g=[[ (int.from_bytes(b[2*y:2*y+2],'big')>>(15-x))&1 for x in range(16)] for y in range(16)]
    return rot_cw(g)
if __name__=='__main__':
    # self-test icon against the Mario Clash sample (bmp px decoded earlier)
    mc=bytes.fromhex(open('mc_icon.hex').read().strip())
    bmp=bytes.fromhex(open('mc_bmp.hex').read().strip())
    px=[[1 if bmp[54+(15-y)*48+x*3]>128 else 0 for x in range(16)] for y in range(16)]
    print("icon encoder matches sample:", to_icon(px)==mc, from_icon(mc)==px)
