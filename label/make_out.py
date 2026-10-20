from encode import *
from PIL import Image
skull = [
"................",
"....########....",
"...##########...",
"..############..",
"..############..",
"..###..##..###..",
".###....#....##.",
".###....#....##.",
"..###..###..###.",
"..#####.#.#####.",
"...####...####..",
"....#########...",
"....#.#.#.#.#...",
"....#########...",
".....#######....",
"................",
]
# fix asymmetry: build left half and mirror
left=[r[:8] for r in [
"........",
"....####",
"...#####",
"..######",
"..######",
"..###..#",
".###...#",
".###...#",
"..###..#",
"..#####.",
"...####.",
"....####",
"....#.#.",
"....####",
".....###",
"........",
]]
rows=[l+l[::-1] for l in left]
px=[[0 if ch=='#' else 1 for ch in r] for r in rows]
icon=to_icon(px)
open('out/doom-vb.icon','wb').write(icon)
assert from_icon(icon)==px
im=Image.new('1',(16,16)); 
for y in range(16):
    for x in range(16): im.putpixel((x,y),px[y][x])
im.save('out/doom-vb-icon.bmp'); im.convert('RGB').resize((128,128),Image.NEAREST).save('out/icon_preview.png')
data=to_bwr('out/doom-vb-label.png','out/doom-vb.bwr')
back=from_bwr(open('out/doom-vb.bwr','rb').read())
src=Image.open('out/doom-vb-label.png').convert('RGB')
print('bwr bytes',len(data),'roundtrip ok', list(back.getdata())==list(src.getdata()))
print('icon bytes',len(icon))
for r in rows: print(r)
