#ifndef VB_FB_H
#define VB_FB_H
/*
 * Virtual Boy 2bpp render target.
 * 240x160 pixels stored exactly like a VB frame buffer column strip:
 * column-major, 40 bytes per column, 4 vertical pixels per byte
 * (pixel y in bits (y&3)*2). Finished frames are copied straight
 * into the VIP frame buffers.
 */
#define VBFB_W    240
#define VBFB_H    160
#define VBFB_COLB (VBFB_H / 4)

extern unsigned char vbfb[VBFB_W * VBFB_COLB];
extern unsigned char lum2bpp[256];

static inline void VBFB_PutLevel(int x, int y, unsigned int lv)
{
    unsigned char* p = &vbfb[x * VBFB_COLB + (y >> 2)];
    const int sh = (y & 3) << 1;
    *p = (unsigned char)((*p & ~(3u << sh)) | (lv << sh));
}

static inline void VBFB_Put(int x, int y, int c)
{
    if ((unsigned)x < VBFB_W && (unsigned)y < VBFB_H)
        VBFB_PutLevel(x, y, lum2bpp[c & 0xff]);
}

static inline unsigned int VBFB_GetLevel(int x, int y)
{
    return (vbfb[x * VBFB_COLB + (y >> 2)] >> ((y & 3) << 1)) & 3;
}

/* Draw a vertical run of texels into column x (and x+1 when dup).
 * texel i = src[(frac + i*step) >> shift], mapped through cmap if non-NULL. */
void VBFB_DrawCol(int x, int dup, int y, int count, const unsigned char* src,
                  const unsigned char* cmap, unsigned int frac, unsigned int step, int shift);

/* Fill count pixels of column x starting at y with palette colour c. */
void VBFB_FillCol(int x, int y, int count, int c);

/* Horizontal span in pair (120-wide) coordinates. */
void VBFB_DrawSpanPairs(int y, int x1, int count, const unsigned char* src,
                        const unsigned char* cmap, unsigned int position, unsigned int step);

void VBFB_FuzzColPair(int x, int yl, int count, const int* fuzzofs, unsigned int* fuzzpos);
#endif
