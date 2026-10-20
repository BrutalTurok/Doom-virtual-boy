#include "vb_fb.h"
#pragma GCC optimize ("O3")

unsigned char vbfb[VBFB_W * VBFB_COLB] __attribute__((aligned(4)));

/* Signed-index lookups: the V810 has no unsigned byte load, so every
 * "unsigned char" table index costs an extra mask. vb_cmap_rot is the COLORMAP
 * lump rotated by 128 (values xor 0x80), which lets texel -> colormap -> level
 * use plain signed loads from the middle of each table. */
extern const unsigned char* fullcolormap;
extern const unsigned char vb_cmap_rot[34 * 256];
#define NCMAPBYTES (34 * 256)

static inline const signed char* cmap_signed(const unsigned char* cmap)
{
    unsigned int o = (unsigned int)(cmap - fullcolormap);
    if (o < NCMAPBYTES)
        return (const signed char*)vb_cmap_rot + o + 128;
    return 0;
}

#define FLUSH(p, acc, mask, dup)                                  \
    do {                                                          \
        if ((mask) == 0xFF) { (p)[0] = (acc); if (dup) (p)[VBFB_COLB] = (acc); } \
        else {                                                    \
            (p)[0] = (unsigned char)(((p)[0] & ~(mask)) | (acc)); \
            if (dup) (p)[VBFB_COLB] = (unsigned char)(((p)[VBFB_COLB] & ~(mask)) | (acc)); \
        }                                                         \
    } while (0)

/* single pixel read-modify-write (column x and, if dup, x+1) */
#define PUT1(p, sh, lv, dup) do {                                            \
        unsigned char _m = (unsigned char)~(3u << (sh));                     \
        unsigned char _v = (unsigned char)(((p)[0] & _m) | ((lv) << (sh)));  \
        (p)[0] = _v;                                                         \
        if (dup) (p)[VBFB_COLB] = (unsigned char)(((p)[VBFB_COLB] & _m) | ((lv) << (sh))); \
    } while (0)

#define TEX(i) lum2bpp[cmap[src[(frac + (i) * step) >> shift]]]
#define RAW(i) lum2bpp[src[(frac + (i) * step) >> shift]]

void VBFB_DrawCol(int x, int dup, int y, int count, const unsigned char* src,
                  const unsigned char* cmap, unsigned int frac, unsigned int step, int shift)
{
    unsigned char* p;
    int sh;

    if ((unsigned)x >= VBFB_W || count <= 0)
        return;
    if (dup && x + 1 >= VBFB_W)
        dup = 0;
    if (y < 0) {                    /* clip top */
        count += y;
        frac += step * (unsigned)(-y);
        y = 0;
    }
    if (y + count > VBFB_H)
        count = VBFB_H - y;
    if (count <= 0)
        return;

    p = &vbfb[x * VBFB_COLB + (y >> 2)];
    sh = (y & 3) << 1;

    if (cmap) {
        const signed char* cs = cmap_signed(cmap);
        if (cs) {
            const signed char* ss = (const signed char*)src;
            const unsigned char* ls = lum2bpp + 128;
#define LVL(i) ls[cs[ss[(frac + (i) * step) >> shift]]]
            while (sh && count) {
                unsigned int lv = LVL(0);
                PUT1(p, sh, lv, dup);
                frac += step; count--; sh += 2;
                if (sh == 8) { sh = 0; p++; }
            }
            if (dup) {
                while (count >= 4) {
                    unsigned int b = LVL(0) | (LVL(1) << 2) | (LVL(2) << 4) | (LVL(3) << 6);
                    p[0] = (unsigned char)b; p[VBFB_COLB] = (unsigned char)b;
                    p++; frac += step * 4; count -= 4;
                }
            } else {
                while (count >= 4) {
                    unsigned int b = LVL(0) | (LVL(1) << 2) | (LVL(2) << 4) | (LVL(3) << 6);
                    *p++ = (unsigned char)b;
                    frac += step * 4; count -= 4;
                }
            }
            sh = 0;
            while (count--) {
                unsigned int lv = LVL(0);
                PUT1(p, sh, lv, dup);
                frac += step; sh += 2;
            }
#undef LVL
            return;
        }
        /* leading pixels up to a byte boundary */
        while (sh && count) {
            unsigned int lv = TEX(0);
            PUT1(p, sh, lv, dup);
            frac += step; count--; sh += 2;
            if (sh == 8) { sh = 0; p++; }
        }
        /* whole bytes: 4 pixels, no read needed */
        if (dup) {
            while (count >= 4) {
                unsigned int b = TEX(0) | (TEX(1) << 2) | (TEX(2) << 4) | (TEX(3) << 6);
                p[0] = (unsigned char)b; p[VBFB_COLB] = (unsigned char)b;
                p++; frac += step * 4; count -= 4;
            }
        } else {
            while (count >= 4) {
                unsigned int b = TEX(0) | (TEX(1) << 2) | (TEX(2) << 4) | (TEX(3) << 6);
                *p++ = (unsigned char)b;
                frac += step * 4; count -= 4;
            }
        }
        /* trailing pixels */
        sh = 0;
        while (count--) {
            unsigned int lv = TEX(0);
            PUT1(p, sh, lv, dup);
            frac += step; sh += 2;
        }
    } else {
        while (sh && count) {
            unsigned int lv = RAW(0);
            PUT1(p, sh, lv, dup);
            frac += step; count--; sh += 2;
            if (sh == 8) { sh = 0; p++; }
        }
        while (count >= 4) {
            unsigned int b = RAW(0) | (RAW(1) << 2) | (RAW(2) << 4) | (RAW(3) << 6);
            p[0] = (unsigned char)b;
            if (dup) p[VBFB_COLB] = (unsigned char)b;
            p++; frac += step * 4; count -= 4;
        }
        sh = 0;
        while (count--) {
            unsigned int lv = RAW(0);
            PUT1(p, sh, lv, dup);
            frac += step; sh += 2;
        }
    }
}

void VBFB_FillCol(int x, int y, int count, int c)
{
    unsigned int lv = lum2bpp[c & 0xff];
    unsigned int full = lv | (lv << 2) | (lv << 4) | (lv << 6);
    unsigned char* p;
    int sh;
    if ((unsigned)x >= VBFB_W) return;
    if (y < 0) { count += y; y = 0; }
    if (y + count > VBFB_H) count = VBFB_H - y;
    if (count <= 0) return;
    p = &vbfb[x * VBFB_COLB + (y >> 2)];
    sh = (y & 3) << 1;
    while (sh && count) {               /* leading pixels */
        PUT1(p, sh, lv, 0);
        count--; sh += 2;
        if (sh == 8) { sh = 0; p++; }
    }
    while (count >= 4) {                /* whole bytes */
        *p++ = (unsigned char)full;
        count -= 4;
    }
    sh = 0;
    while (count--) {                   /* trailing pixels */
        PUT1(p, sh, lv, 0);
        sh += 2;
    }
}

void VBFB_DrawSpanPairs(int y, int x1, int count, const unsigned char* src,
                        const unsigned char* cmap, unsigned int position, unsigned int step)
{
    const int sh = (y & 3) << 1;
    const unsigned char m = (unsigned char)~(3u << sh);
    unsigned char* p = &vbfb[(x1 * 2) * VBFB_COLB + (y >> 2)];
    const signed char* cs = cmap_signed(cmap);
    if (cs) {
        const signed char* ss = (const signed char*)src;
        const unsigned char* ls = lum2bpp + 128;
        while (count-- > 0) {
            unsigned int lv = (unsigned int)ls[cs[ss[((position >> 4) & 0x0fc0) | (position >> 26)]]] << sh;
            unsigned char v = (unsigned char)((p[0] & m) | lv);
            p[0] = v;
            p[VBFB_COLB] = v;
            p += VBFB_COLB * 2;
            position += step;
        }
        return;
    }
    while (count-- > 0) {
        unsigned int lv = (unsigned int)lum2bpp[cmap[src[((position >> 4) & 0x0fc0) | (position >> 26)]]] << sh;
        /* both columns of a 3D-view pixel pair always hold the same value here */
        unsigned char v = (unsigned char)((p[0] & m) | lv);
        p[0] = v;
        p[VBFB_COLB] = v;
        p += VBFB_COLB * 2;
        position += step;
    }
}

/* Spectre/invisibility fuzz: darken the pixel one row above/below. */
void VBFB_FuzzColPair(int x, int yl, int count, const int* fuzzofs, unsigned int* fuzzpos)
{
    unsigned int fp = *fuzzpos;
    int cx = x * 2;
    int y;
    for (y = yl; count-- > 0; y++) {
        int sy = y + (fuzzofs[fp] > 0 ? 1 : -1);
        unsigned int lv;
        if (sy < 0) sy = 0;
        if (sy >= VBFB_H) sy = VBFB_H - 1;
        lv = VBFB_GetLevel(cx, sy);
        lv = (lv * 3) >> 2;
        VBFB_PutLevel(cx, y, lv);
        VBFB_PutLevel(cx + 1, y, lv);
        if (++fp >= 50) fp = 0;
    }
    *fuzzpos = fp;
}
