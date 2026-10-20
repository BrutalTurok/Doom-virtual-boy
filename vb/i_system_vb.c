/* Virtual Boy platform layer for GBADoom (harness / first port). */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "doomdef.h"
#include "doomtype.h"
#include "d_main.h"
#include "d_event.h"
#include "global_data.h"
#include "i_system_e32.h"
#include "lprintf.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---------------- hardware ---------------- */
#define HW_REGS   ((volatile u8*)0x02000000)
#define SDLR 0x10
#define SDHR 0x14
#define SCR  0x28
#define VIP_REGS  ((volatile u16*)0x0005F800)
#define INTPND 0x00
#define INTENB 0x01
#define INTCLR 0x02
#define DPSTTS 0x10
#define DPCTRL 0x11
#define BRTA 0x12
#define BRTB 0x13
#define BRTC 0x14
#define REST 0x15
#define FRMCYC 0x17
#define XPSTTS 0x20
#define XPCTRL 0x21
#define BKCOL 0x38
#define FRAMESTART 0x0010
#define CLMN_TBL ((volatile u16*)0x0003DC00)
#define FB_L0 ((volatile u8*)0x00000000)
#define FB_L1 ((volatile u8*)0x00008000)
#define FB_R0 ((volatile u8*)0x00010000)
#define FB_R1 ((volatile u8*)0x00018000)

extern void (*vipVector)(void);  /* crt0: _vipVector = 0x0500FFD0 */

static const u8 colTable[128] = {
 0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,
 0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,
 0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,
 0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xFE,0xE0,0xBC,
 0xA6,0x96,0x8A,0x82,0x7A,0x74,0x6E,0x6A,0x66,0x62,0x60,0x5C,0x5A,0x58,0x56,0x54,
 0x52,0x50,0x50,0x4E,0x4C,0x4C,0x4A,0x4A,0x48,0x48,0x46,0x46,0x46,0x44,0x44,0x44,
 0x42,0x42,0x42,0x40,0x40,0x40,0x40,0x40,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3E,0x3C,
 0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C,0x3C };

/* ---------------- debug / stats block (read by test frontend) ---------------- */
#ifdef VB_REAL
#define LOGSZ 256
#else
#define LOGSZ 4096
#endif
typedef struct {
    u32 magic;          /* 'DBG!' */
    volatile u32 vbframes;
    u32 doomframes;
    u32 zone_used, zone_peak, zone_size;
    u32 log_pos;
    char log[LOGSZ];
    volatile u32 cmd;      /* harness: warp to E1Mx */
    u32 sbrk_top;
    volatile u32 zone_fail; /* filled on I_Error */
} vbdebug_t;
vbdebug_t vbdebug;

static void dbg_puts(const char* s)
{
    while (*s) {
        vbdebug.log[vbdebug.log_pos & (LOGSZ-1)] = *s++;
        vbdebug.log_pos++;
    }
}

int lprintf(OutputLevels pri, const char *s, ...)
{
    char msg[128];
    va_list v;
    va_start(v, s);
    vsnprintf(msg, sizeof(msg), s, v);
    va_end(v);
    dbg_puts(msg);
    dbg_puts("\n");
    return 0;
}

/* ---------------- timing ---------------- */
/* The pad is read by the hardware in the background: each frame we collect
 * the previous result and start the next read, so nobody busy-waits. */
static volatile u16 pad_latch;
static void vip_isr(void)
{
    u16 p = VIP_REGS[INTPND];
    VIP_REGS[INTCLR] = p;
    vbdebug.vbframes++;
    if (!(HW_REGS[0x28] & 0x02)) {                       /* S_STAT: not busy */
        pad_latch = (HW_REGS[0x14] << 8) | HW_REGS[0x10];
        HW_REGS[0x28] = 0x84;                            /* start next hardware read */
    }
}

int I_GetTimeVB(void)
{
    /* 50Hz display frames -> 35Hz doom tics */
    return (int)((vbdebug.vbframes * 7) / 10);
}

/* ---------------- video ---------------- */
/* GBADoom draws 120x160 "pixel pairs" (u16 per pixel). */
#ifdef VB_REAL
#include "vb_mem.h"
#include "vb_fb.h"
#define backbuf ((unsigned short*)vbfb)
#else
static unsigned short backbuf[SCREENWIDTH * SCREENHEIGHT];
#endif
u8 lum2bpp[256];
static u32 vb_fbmask = 3;   /* bit0: pair 0 displayed, bit1: pair 1 */

void I_InitScreen_e32()
{
    int i;
#ifdef VB_REAL
    {   /* paint the stack area so the harness can measure stack depth */
        extern char wram_zone_end[];
        volatile u8* p = (volatile u8*)wram_zone_end;
        volatile u8* sp = (volatile u8*)&i;
        while (p < sp - 256) *p++ = 0x5A;
    }
#endif
    vbdebug.magic = 0x21474244;

#ifdef VB_ROM1WAIT
    /* experimental: cartridge ROM with 1 wait state instead of 2 (WCR bit 0).
     * Faster code/texture fetches if the cart's flash is quick enough. */
    HW_REGS[0x24] = 0x01;
#endif
    VIP_REGS[INTENB] = 0;
    VIP_REGS[INTCLR] = VIP_REGS[INTPND];
    VIP_REGS[REST] = 0;
    VIP_REGS[XPCTRL] = 0;                     /* no VIP drawing: we own the framebuffers */
    VIP_REGS[DPCTRL] = VIP_REGS[DPSTTS] | (0x0200 | 0x0100 | 0x0002);
    VIP_REGS[FRMCYC] = 0;
    for (i = 0; i < 128; i++) {
        CLMN_TBL[i] = colTable[i];       CLMN_TBL[i + 256] = colTable[i];
        CLMN_TBL[i + 128] = colTable[127-i]; CLMN_TBL[i + 384] = colTable[127-i];
    }
    VIP_REGS[BKCOL] = 0;
    VIP_REGS[BRTA] = 24; VIP_REGS[BRTB] = 56; VIP_REGS[BRTC] = 48;

    memset((void*)FB_L0, 0, 0x6000); memset((void*)FB_L1, 0, 0x6000);
    memset((void*)FB_R0, 0, 0x6000); memset((void*)FB_R1, 0, 0x6000);

    vipVector = vip_isr;
    VIP_REGS[INTENB] = FRAMESTART;
    asm volatile ("cli");

#ifdef VB_REAL
    /* With the drawing unit off we don't know for sure which frame buffer
     * pair the display shows. Watch the display-busy flags for a few frames:
     * if only one pair is ever scanned out we only need to update that one. */
    {
        u32 start = vbdebug.vbframes;
        u16 seen = 0;
        while (vbdebug.vbframes - start < 8)
            seen |= VIP_REGS[DPSTTS];
        vb_fbmask = ((seen & 0x0C) ? 1 : 0) | ((seen & 0x30) ? 2 : 0);
        if (!vb_fbmask) vb_fbmask = 3;
        lprintf(LO_INFO, "VB: frame buffer pairs in use: %d", vb_fbmask);
    }
#endif
}

#ifdef VB_REAL
void I_CreateBackBuffer_e32() { memset(vbfb, 0, sizeof(vbfb)); }
#else
void I_CreateBackBuffer_e32() { memset(backbuf, 0, SCREENWIDTH * SCREENHEIGHT * 2); }
#endif
int I_GetVideoWidth_e32()  { return SCREENWIDTH; }
int I_GetVideoHeight_e32() { return SCREENHEIGHT; }
unsigned short* I_GetBackBuffer()  { return backbuf; }
unsigned short* I_GetFrontBuffer() { return backbuf; }

unsigned int vb_lum_version;
void I_SetPallete_e32(const byte* p)
{
    int i;
    vb_lum_version++;
    for (i = 0; i < 256; i++) {
        int r = p[i*3], g = p[i*3+1], b = p[i*3+2];
        int l = (r * 77 + g * 151 + b * 28) >> 8;     /* 0..255 */
        int v = (l + 21) / 64;                        /* 0..3 (+bias so darks still show) */
        if (v > 3) v = 3;
        lum2bpp[i] = (u8)v;
    }
}

/* Blit the 240x160 8bpp buffer (stored as 120 u16 pixel pairs per row) to the
 * VB frame buffers, centred, same image to both eyes. We write both frame
 * buffer pairs because with the XP unit off we cannot rely on which pair the
 * display process shows on real hardware. */
#define FBW (SCREENWIDTH*2)
#define XOFS ((384 - FBW) / 2)
#define YOFS ((224 - SCREENHEIGHT) / 2)
void I_FinishUpdate_e32(const byte* src, const byte* pal, const unsigned int w, const unsigned int h)
{
#ifdef VB_REAL
    /* vbfb already has VB frame-buffer layout: copy 40-byte columns as words */
    int x;
    const u32* s = (const u32*)vbfb;
    for (x = 0; x < VBFB_W; x++) {
        u32 o = (XOFS + x) * 64 + YOFS/4;
        volatile u32* l0 = (volatile u32*)(FB_L0 + o); volatile u32* l1 = (volatile u32*)(FB_L1 + o);
        volatile u32* r0 = (volatile u32*)(FB_R0 + o); volatile u32* r1 = (volatile u32*)(FB_R1 + o);
        u32 v0=s[0],v1=s[1],v2=s[2],v3=s[3],v4=s[4],v5=s[5],v6=s[6],v7=s[7],v8=s[8],v9=s[9];
        s += 10;
#define CP(d) d[0]=v0;d[1]=v1;d[2]=v2;d[3]=v3;d[4]=v4;d[5]=v5;d[6]=v6;d[7]=v7;d[8]=v8;d[9]=v9;
        if (vb_fbmask & 1) { CP(l0) CP(r0) }
        if (vb_fbmask & 2) { CP(l1) CP(r1) }
#undef CP
    }
#else

    int x, y;
    const u8* buf = (const u8*)backbuf;
    for (x = 0; x < FBW; x++) {
        const u8* s = buf + x;
        u32 o = (XOFS + x) * 64 + YOFS/4;
        volatile u8* l0 = FB_L0 + o; volatile u8* l1 = FB_L1 + o;
        volatile u8* r0 = FB_R0 + o; volatile u8* r1 = FB_R1 + o;
        for (y = 0; y < SCREENHEIGHT; y += 4) {
            u8 b = lum2bpp[s[0]]
                 | (lum2bpp[s[FBW]] << 2)
                 | (lum2bpp[s[FBW*2]] << 4)
                 | (lum2bpp[s[FBW*3]] << 6);
            s += FBW*4;
            *l0++ = b; *l1++ = b; *r0++ = b; *r1++ = b;
        }
    }
#endif
    vbdebug.doomframes++;
}

/* ---------------- input ---------------- */
#define K_A   0x0004
#define K_B   0x0008
#define K_RT  0x0010
#define K_LT  0x0020
#define K_RU  0x0040
#define K_RR  0x0080
#define K_LR  0x0100
#define K_LL  0x0200
#define K_LD  0x0400
#define K_LU  0x0800
#define K_STA 0x1000
#define K_SEL 0x2000
#define K_RL  0x4000
#define K_RD  0x8000

static u16 vbReadPad(void)
{
    return pad_latch;
}

static u16 oldkeys;
static const struct { u16 vb; int dk; } keymap[] = {
    { K_LU, KEYD_UP }, { K_LD, KEYD_DOWN }, { K_LL, KEYD_LEFT }, { K_LR, KEYD_RIGHT },
    { K_RL, KEYD_L },  { K_RR, KEYD_R },      /* right d-pad left/right = strafe */
    { K_B,  KEYD_B },  { K_A,  KEYD_A },      /* B use/run, A next weapon (and menu select) */
    { K_STA, KEYD_START }, { K_SEL, KEYD_SELECT },
};

#include "g_game.h"
void I_ProcessKeyEvents()
{
    unsigned i;
    if (vbdebug.cmd) { G_DeferedInitNew(sk_medium, 1, vbdebug.cmd); vbdebug.cmd = 0; }
    u16 k = vbReadPad();
    u16 chg = k ^ oldkeys;
    event_t ev;
    k &= 0xFFFC;   /* bit0 = low battery, bit1 = pad signature: not buttons */
    for (i = 0; i < sizeof(keymap)/sizeof(keymap[0]); i++) {
        if (chg & keymap[i].vb) {
            ev.type = (k & keymap[i].vb) ? ev_keydown : ev_keyup;
            ev.data1 = keymap[i].dk;
            D_PostEvent(&ev);
        }
    }
    /* both triggers fire: one combined key so releasing one while holding the other keeps firing */
    {
        int fire_now = (k & (K_LT | K_RT)) != 0, fire_old = (oldkeys & (K_LT | K_RT)) != 0;
        if (fire_now != fire_old) {
            ev.type = fire_now ? ev_keydown : ev_keyup;
            ev.data1 = KEYD_FIRE;
            D_PostEvent(&ev);
        }
    }
    oldkeys = k;
}

void I_Error(const char *error, ...)
{
    char msg[128];
    va_list v;
    vbdebug.zone_fail = 1;
    dbg_puts("I_Error: "); dbg_puts(error); dbg_puts("\n");
    va_start(v, error);
    vsnprintf(msg, sizeof(msg), error, v);
    va_end(v);
    dbg_puts(" -> "); dbg_puts(msg); dbg_puts("\n");
    vbdebug.zone_fail = 2;
    for (;;) ;
}

void I_Quit_e32() { }

void vbdebug_zone(int used)
{
    vbdebug.zone_used = used;
    if ((u32)used > vbdebug.zone_peak) vbdebug.zone_peak = used;
}

void vbdebug_sbrk(unsigned top) { vbdebug.sbrk_top = top; }

static void dbg_putn(unsigned v)
{
    char b[12]; int i = 11; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    dbg_puts(&b[i]);
}

/* memblock layout mirrors z_zone.c */
typedef struct dbg_memblock_s {
    unsigned int size:24; unsigned int tag:4;
    void** user; struct dbg_memblock_s* next; struct dbg_memblock_s* prev;
} dbg_memblock_t;

void vbdebug_zonefail(int size, void* zone)
{
    dbg_memblock_t* head = (dbg_memblock_t*)zone;
    dbg_memblock_t* b;
    unsigned freetot = 0, freemax = 0, used[8] = {0};
    dbg_puts("ZFAIL size="); dbg_putn(size); dbg_puts("\n");
    for (b = head->next; b != head; b = b->next) {
        if (!b->user) { freetot += b->size; if (b->size > freemax) freemax = b->size; }
        else used[b->tag & 7] += b->size;
        if (b->user == (void**)zone && b->size == sizeof(dbg_memblock_t)) {
            dbg_puts(" |region free="); dbg_putn(freetot); dbg_puts(" max="); dbg_putn(freemax); dbg_puts("\n");
            freetot = 0; freemax = 0;
        }
    }
    dbg_puts("used by tag: static="); dbg_putn(used[1]); dbg_puts(" level="); dbg_putn(used[2]);
    dbg_puts(" levspec="); dbg_putn(used[3]); dbg_puts(" cache="); dbg_putn(used[4]); dbg_puts("\n");
}
