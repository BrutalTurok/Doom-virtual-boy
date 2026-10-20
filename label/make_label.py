import os
import random, math
from PIL import Image, ImageDraw, ImageFont

W, H = 264, 176
K, Wt, R = (0,0,0), (255,255,255), (255,0,0)
img = Image.new('RGB', (W, H), K)
d = ImageDraw.Draw(img)
random.seed(1993)

# ---- background: red sky glow bands (dithered) + flames/mountains at bottom ----
# sparse embers in the sky
random.seed(3)
for i in range(70):
    x_, y_ = random.randint(4, W - 5), random.randint(30, 118)
    img.putpixel((x_, y_), R if i % 3 else Wt)

# jagged mountains (red) with white rim
def ridge(base, amp, seed, step):
    random.seed(seed); pts = []; y = base
    for x in range(-step, W + step, step):
        y = max(base - amp, min(base + amp, y + random.randint(-amp // 2, amp // 2)))
        pts.append((x, y))
    return pts
far = ridge(134, 10, 7, 10)
d.polygon(far + [(W + 20, H), (-20, H)], fill=R)
for i in range(len(far) - 1):
    d.line([far[i], far[i + 1]], fill=Wt, width=1)
near = ridge(152, 6, 21, 7)
d.polygon(near + [(W + 20, H), (-20, H)], fill=K)
for i in range(len(near) - 1):
    d.line([near[i], near[i + 1]], fill=R, width=2)

# flames licking up from the black foreground
random.seed(5)
for fx in range(4, W, 9):
    h = random.randint(8, 20)
    base = 160
    d.polygon([(fx - 4, base), (fx, base - h), (fx + 4, base)], fill=R)
    d.polygon([(fx - 2, base), (fx, base - h // 2), (fx + 2, base)], fill=Wt)

# ---- big title: original chunky block letters "DOOM" ----
U = 8  # block unit
glyphs = {
 'D': ["11110","11011","11011","11011","11011","11011","11110"],
 'O': ["01110","11011","11011","11011","11011","11011","01110"],
 'M': ["1100011","1110111","1111111","1101011","1100011","1100011","1100011"],
}
word = "DOOM"
widths = [len(glyphs[c][0]) for c in word]
gap = 1
total = sum(widths) * U + (len(word) - 1) * gap * U
x0 = (W - total) // 2
y0 = 34
cx_mid = W / 2
def block(px, py):
    # perspective: blocks get taller toward the centre of the word
    return (px, py, px + U, py + U)
title = Image.new('L', (W, H), 0)
td = ImageDraw.Draw(title)
x = x0
for c, w in zip(word, widths):
    g = glyphs[c]
    for gy, row in enumerate(g):
        for gx, b in enumerate(row):
            if b == '1':
                l, t, r_, bt = block(x + gx * U, y0 + gy * U)
                td.rectangle([l, t, r_ - 1, bt - 1], fill=255)
    x += (w + gap) * U
# outline (white), fill red, inner black slashes for texture
from PIL import ImageFilter
mask = title.point(lambda v: 255 if v > 0 else 0)
outline = mask.filter(ImageFilter.MaxFilter(5))
img.paste(K, mask=mask.filter(ImageFilter.MaxFilter(7)))
img.paste(Wt, mask=outline)
img.paste(R, mask=mask)
inner = mask.filter(ImageFilter.MinFilter(3))
tex = Image.new('L', (W, H), 0); tdx = ImageDraw.Draw(tex)
for i in range(-H, W, 7):
    tdx.line([(i, H), (i + H, 0)], fill=255, width=1)
from PIL import ImageChops
img.paste(K, mask=ImageChops.multiply(inner.filter(ImageFilter.MinFilter(3)), tex))

# ---- small text (no anti-aliasing) ----
def text_c(y, s, font, fill, spacing=0):
    dd = ImageDraw.Draw(img); dd.fontmode = "1"
    wds = [dd.textlength(ch, font=font) for ch in s]
    tw = sum(wds) + spacing * (len(s) - 1)
    xx = (W - tw) / 2
    for ch, wd in zip(s, wds):
        dd.text((xx, y), ch, font=font, fill=fill); xx += wd + spacing
fb = "/usr/share/fonts/truetype/google-fonts/Poppins-Bold.ttf"
f_small = ImageFont.truetype(fb, 11)
f_tiny = ImageFont.truetype(fb, 9)
text_c(12, "VIRTUAL  BOY", f_small, Wt, spacing=3)
d.line([(60, 27), (204, 27)], fill=R, width=1)
text_c(100, "EPISODE 1", f_tiny, Wt, spacing=2)

# bottom caption on black strip
d.rectangle([0, 160, W, H], fill=K)
text_c(162, "KNEE-DEEP IN THE DEAD", f_tiny, R, spacing=1)

# quantize strictly to 3 colours
px = img.load()
for yy in range(H):
    for xx in range(W):
        r_, g_, b_ = px[xx, yy]
        if r_ > 128 and g_ < 128: px[xx, yy] = R
        elif (r_ + g_ + b_) / 3 > 128: px[xx, yy] = Wt
        else: px[xx, yy] = K
img.save('out/doom-vb-label.png')
img.save('out/doom-vb-label.bmp')
print("ok")
