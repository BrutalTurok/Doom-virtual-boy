/* Virtual Boy VSU sound driver for Doom (MUS music + PC-speaker effects). */
#include <string.h>
#include "vb_sound.h"

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned int   u32;

/* ---------------- hardware ---------------- */
#define VSU_WAVE(n)    ((volatile u8*)(0x01000000 + (n) * 0x80))
#define VSU_CH(n)      ((volatile u8*)(0x01000400 + (n) * 0x40))
#define SxINT  0x00
#define SxLRV  0x04
#define SxFQL  0x08
#define SxFQH  0x0C
#define SxEV0  0x10
#define SxEV1  0x14
#define SxRAM  0x18
#define VSU_SSTOP (*(volatile u8*)0x01000580)

#define HW_REGS ((volatile u8*)0x02000000)
#define TLR 0x18
#define THR 0x1C
#define TCR 0x20
#define TIMER_ENB  0x01
#define TIMER_ZCLR 0x04
#define TIMER_INT  0x08
#define TIMER_20US 0x10

extern void (*timVector)(void);   /* crt0: _timVector = 0x0500FFC4 */

#define CH_SFX   4                  /* VSU channel 5 */
#define CH_NOISE 5                  /* VSU channel 6 */
#define NVOICES  4                  /* VSU channels 1-4 */

/* waveforms: 0 square, 1 saw, 2 triangle, 3 pulse 25%, 4 constant (PCM) */
static void write_waves(void)
{
    int i;
    for (i = 0; i < 32; i++) {
        int tri = i < 16 ? i * 4 : (31 - i) * 4;
        /* organ: fundamental + octave, built from two triangles */
        int t2 = ((i * 2) & 31) < 16 ? ((i * 2) & 31) * 4 : (31 - ((i * 2) & 31)) * 4;
        int org = (tri * 2 + t2) / 3;
        VSU_WAVE(0)[i * 4] = i < 16 ? 63 : 0;
        VSU_WAVE(1)[i * 4] = (u8)(i * 2);
        VSU_WAVE(2)[i * 4] = (u8)(tri > 63 ? 63 : tri);
        VSU_WAVE(3)[i * 4] = i < 8 ? 63 : 0;
        (void)org;
        VSU_WAVE(4)[i * 4] = 63;            /* constant: PCM effects modulate its volume */
    }
}

static int crit_level;
static inline void crit_enter(void) { __asm__ volatile ("sei"); crit_level++; }
static inline void crit_leave(void) { if (--crit_level == 0) __asm__ volatile ("cli"); }

/* ---------------- streaming LZSS reader over a MUS lump ---------------- */
static u8 ring[4096];
static const u8* lz_src;
static u32 lz_out, lz_rawlen;
static u8 lz_flags, lz_bit;
static u16 lz_copylen, lz_dist;

static void lz_reset(const u8* src, u32 rawlen)
{
    lz_src = src; lz_out = 0; lz_rawlen = rawlen; lz_bit = 8; lz_copylen = 0;
}

static u8 lz_get(void)
{
    u8 b;
    if (lz_copylen) {
        b = ring[(lz_out - lz_dist) & 4095];
        lz_copylen--;
    } else {
        if (lz_bit == 8) { lz_flags = *lz_src++; lz_bit = 0; }
        if (lz_flags & (1 << lz_bit)) {
            b = *lz_src++;
        } else {
            u16 v = lz_src[0] | (lz_src[1] << 8);
            lz_src += 2;
            lz_dist = (v >> 4) + 1;
            lz_copylen = (v & 15) + 3 - 1;
            b = ring[(lz_out - lz_dist) & 4095];
        }
        lz_bit++;
    }
    ring[lz_out & 4095] = b;
    lz_out++;
    return b;
}

/* ---------------- music state ---------------- */
static const vbsnd_song_t* cur_song;
static u16 score_start;
static volatile u8 mus_playing, mus_paused, mus_looping;
static u32 mus_delay;
static u8 mus_vol = 8;

static u8 ch_instr[16], ch_vol[16], ch_pan[16], ch_vel[16], ch_bend[16];

typedef struct { u8 active, mch, note; u32 age; } voice_t;
static voice_t voices[NVOICES];
static u32 voice_clock;

static u8 wave_for_instr(int prog)
{
    if (prog >= 24 && prog <= 31) return 1;      /* guitars (Doom's distortion) -> saw */
    if (prog >= 32 && prog <= 39) return 1;      /* basses -> saw */
    if (prog >= 40 && prog <= 55) return 2;      /* strings / ensembles -> triangle */
    if (prog >= 56 && prog <= 63) return 3;      /* brass -> narrow pulse */
    if (prog >= 64 && prog <= 79) return 2;      /* reeds / pipes */
    if (prog >= 88 && prog <= 103) return 2;     /* pads / fx */
    if (prog >= 16 && prog <= 23) return 2;      /* organs */
    return 0;                                    /* everything else: square */
}

static void voice_freq(int v)
{
    int idx = voices[v].note * 8 + ((int)ch_bend[voices[v].mch] - 128) / 4;  /* +-2 semitones */
    u16 F;
    if (idx < 0) idx = 0;
    if (idx > 1023) idx = 1023;
    F = vbsnd_notetab[idx];
    VSU_CH(v)[SxFQL] = F & 0xFF;
    VSU_CH(v)[SxFQH] = F >> 8;
}

static void set_lrv(int chn, int l, int r)
{
    if (l > 15) l = 15;
    if (r > 15) r = 15;
    if (l < 0) l = 0;
    if (r < 0) r = 0;
    VSU_CH(chn)[SxLRV] = (u8)((l << 4) | r);
}

static void pan_levels(int pan, int* l, int* r)
{
    /* pan 0..127, 64 = centre */
    *l = pan <= 64 ? 15 : 15 - ((pan - 64) * 15) / 64;
    *r = pan >= 64 ? 15 : 15 - ((64 - pan) * 15) / 64;
}

static void voice_off(int v)
{
    VSU_CH(v)[SxINT] = 0;
    voices[v].active = 0;
}

static void drum_on(int note, int vel)
{
    /* noise channel: frequency, LFSR tap, envelope decay step, length */
    u16 F; u8 tap, step, len;
    int vol = (vel * ch_vol[15] * mus_vol * 2) / (127 * 127 * 3);   /* music sits under the effects */
    if (vol <= 0) return;
    if (vol > 15) vol = 15;
    switch (note) {
        case 35: case 36:           F = 1500; tap = 0; step = 0; len = 6;  break;  /* kick */
        case 38: case 40: case 39:  F = 1900; tap = 0; step = 1; len = 12; break;  /* snare / clap */
        case 37:                    F = 2000; tap = 0; step = 0; len = 3;  break;  /* rim */
        case 42: case 44:           F = 2040; tap = 0; step = 0; len = 2;  break;  /* closed hat */
        case 46:                    F = 2040; tap = 0; step = 1; len = 10; break;  /* open hat */
        case 49: case 57: case 52: case 55:
                                    F = 2030; tap = 0; step = 3; len = 31; break;  /* crash */
        case 51: case 59: case 53:  F = 2042; tap = 0; step = 2; len = 20; break;  /* ride */
        case 41: case 43: case 45: case 47: case 48: case 50:
                                    F = 1600 + (note - 41) * 30; tap = 3; step = 1; len = 12; break; /* toms */
        default:                    F = 1950; tap = 0; step = 1; len = 8;  break;
    }
    {
        int l, r;
        pan_levels(ch_pan[15], &l, &r);
        VSU_CH(CH_NOISE)[SxINT] = 0;
        set_lrv(CH_NOISE, l, r);
        VSU_CH(CH_NOISE)[SxFQL] = F & 0xFF;
        VSU_CH(CH_NOISE)[SxFQH] = F >> 8;
        VSU_CH(CH_NOISE)[SxEV0] = (u8)((vol << 4) | step);       /* decay */
        VSU_CH(CH_NOISE)[SxEV1] = (u8)((tap << 4) | 0x01);       /* tap + envelope on */
        VSU_CH(CH_NOISE)[SxINT] = (u8)(0x80 | 0x20 | (len & 31)); /* enable, auto-stop */
    }
}

static void note_on(int mch, int note, int vel)
{
    int v, best = -1;
    u32 oldest = 0xFFFFFFFF;
    int vol, l, r;

    if (mch == 15) { drum_on(note, vel); return; }

    vol = (vel * ch_vol[mch] * mus_vol * 2) / (127 * 127 * 3);       /* music sits under the effects */
    if (vol <= 0) return;
    if (vol > 15) vol = 15;

    for (v = 0; v < NVOICES; v++)
        if (voices[v].active && voices[v].mch == mch && voices[v].note == note) { best = v; break; }
    if (best < 0)
        for (v = 0; v < NVOICES; v++)
            if (!voices[v].active) { best = v; break; }
    if (best < 0)
        for (v = 0; v < NVOICES; v++)
            if (voices[v].age < oldest) { oldest = voices[v].age; best = v; }

    v = best;
    voices[v].active = 1; voices[v].mch = (u8)mch; voices[v].note = (u8)note; voices[v].age = ++voice_clock;

    VSU_CH(v)[SxINT] = 0;
    VSU_CH(v)[SxRAM] = wave_for_instr(ch_instr[mch]);
    voice_freq(v);
    pan_levels(ch_pan[mch], &l, &r);
    set_lrv(v, l, r);
    VSU_CH(v)[SxEV0] = (u8)(vol << 4);            /* constant level */
    VSU_CH(v)[SxEV1] = 0;
    VSU_CH(v)[SxINT] = 0x80;
}

static void note_off(int mch, int note)
{
    int v;
    for (v = 0; v < NVOICES; v++)
        if (voices[v].active && voices[v].mch == mch && voices[v].note == note)
            voice_off(v);
}

static void all_notes_off(void)
{
    int v;
    for (v = 0; v < NVOICES; v++) voice_off(v);
    VSU_CH(CH_NOISE)[SxINT] = 0;
}

static void mus_restart(void)
{
    int i;
    lz_reset(cur_song->data, cur_song->rawlen);
    /* header: "MUS\x1a" len start ... */
    {
        u8 h[8]; u32 n;
        for (i = 0; i < 8; i++) h[i] = lz_get();
        score_start = h[6] | (h[7] << 8);
        for (n = 8; n < score_start; n++) lz_get();
    }
    for (i = 0; i < 16; i++) { ch_instr[i] = 0; ch_vol[i] = 100; ch_pan[i] = 64; ch_vel[i] = 100; ch_bend[i] = 128; }
    mus_delay = 0;
}

static u32 read_varlen(void)
{
    u32 v = 0; u8 b;
    do { b = lz_get(); v = (v << 7) | (b & 0x7F); } while (b & 0x80);
    return v;
}

static void mus_tick(void)
{
    int guard = 0;
    if (!mus_playing || mus_paused) return;

    while (mus_delay == 0) {
        u8 ev, mch, type;
        if (lz_out >= lz_rawlen || ++guard > 256) { mus_playing = 0; all_notes_off(); return; }
        ev = lz_get(); mch = ev & 15; type = (ev >> 4) & 7;
        switch (type) {
            case 0: note_off(mch, lz_get() & 127); break;
            case 1: {
                u8 n = lz_get();
                if (n & 0x80) ch_vel[mch] = lz_get() & 127;
                note_on(mch, n & 127, ch_vel[mch]);
                break;
            }
            case 2: {
                int v;
                ch_bend[mch] = lz_get();
                for (v = 0; v < NVOICES; v++)
                    if (voices[v].active && voices[v].mch == mch) voice_freq(v);
                break;
            }
            case 3: {
                u8 c = lz_get();
                if (c == 10 || c == 11) all_notes_off();
                break;
            }
            case 4: {
                u8 c = lz_get(), val = lz_get() & 127;
                if (c == 0) ch_instr[mch] = val;
                else if (c == 3) ch_vol[mch] = val;
                else if (c == 4) ch_pan[mch] = val;
                break;
            }
            case 5: break;                  /* measure end */
            case 6:                         /* score end */
                all_notes_off();
                if (mus_looping) { mus_restart(); return; }
                mus_playing = 0;
                return;
            default: break;
        }
        if (ev & 0x80) mus_delay = read_varlen();
    }
    mus_delay--;
}

/* ---------------- sampled effects (PCM) ----------------
 * Doom's digitised effects, resampled to PCM_HZ and stored as 4-bit signed
 * samples (two per byte, low nibble first). VSU channel 5 plays a constant
 * waveform and its left/right levels are rewritten every sample, which turns
 * it into a stereo 4-bit DAC. Two effects are mixed in software. */
#define PCM_VOICES 2
/* layout shared with vb_timer_isr.s - keep 32 bytes, fields in this order */
typedef struct {
    const u8* data;        /* 0  NULL = idle */
    u32 pos;               /* 4  in samples */
    u32 len;               /* 8  */
    u32 vl;                /* 12 */
    u32 vr;                /* 16 */
    int prio;              /* 20 */
    int handle;            /* 24 */
    u32 age;               /* 28 */
} pcmvoice_t;
pcmvoice_t vbpcm[PCM_VOICES];
#define pcm vbpcm
static u32 pcm_clock;
u32 vbsnd_mus_acc;
volatile u32 vbsnd_isr_count;


/* The PCM mixing runs in vb_timer_isr.s (fast path, few registers saved).
 * About 140 times a second it falls back to the normal interrupt prolog and
 * lands here to run the music sequencer. */
static void timer_music_isr(void)
{
    vbsnd_isr_count++;
    mus_tick();
    HW_REGS[TCR] = TIMER_ENB | TIMER_20US | TIMER_INT;
}

void VBSND_Init(void)
{
    int i;
    VSU_SSTOP = 1;                       /* wave RAM is only writable with all channels stopped */
    write_waves();
    for (i = 0; i < 6; i++) { VSU_CH(i)[SxINT] = 0; VSU_CH(i)[SxLRV] = 0; }

    /* PCM channel: constant waveform, always running, centred at level 8 */
    VSU_CH(CH_SFX)[SxRAM] = 4;
    VSU_CH(CH_SFX)[SxFQL] = 0x00;
    VSU_CH(CH_SFX)[SxFQH] = 0x07;
    VSU_CH(CH_SFX)[SxEV0] = 0xF0;
    VSU_CH(CH_SFX)[SxEV1] = 0;
    VSU_CH(CH_SFX)[SxLRV] = 0x88;
    VSU_CH(CH_SFX)[SxINT] = 0x80;

    timVector = timer_music_isr;
    HW_REGS[TCR] = 0;
    HW_REGS[TLR] = PCM_TIMER_RELOAD;
    HW_REGS[THR] = 0;
    HW_REGS[TCR] = TIMER_ENB | TIMER_20US | TIMER_INT | TIMER_ZCLR;
}

void VBSND_PlaySong(const char* name, int looping)
{
    const vbsnd_song_t* s;
    for (s = vbsnd_songs; s->name; s++)
        if (!strcmp(s->name, name)) break;
    crit_enter();
    all_notes_off();
    if (s->name) {
        cur_song = s;
        mus_looping = (u8)looping;
        mus_paused = 0;
        mus_restart();
        mus_playing = 1;
    } else {
        mus_playing = 0;
    }
    crit_leave();
}

void VBSND_StopSong(void)
{
    crit_enter();
    mus_playing = 0;
    all_notes_off();
    crit_leave();
}

void VBSND_PauseSong(int paused)
{
    crit_enter();
    mus_paused = (u8)paused;
    if (paused) all_notes_off();
    crit_leave();
}

void VBSND_SetMusicVolume(int vol)
{
    if (vol < 0) vol = 0;
    if (vol > 15) vol = 15;
    mus_vol = (u8)vol;
    if (!vol) { crit_enter(); all_notes_off(); crit_leave(); }
}

void VBSND_StartSfx(const char* name, int vol, int sep, int priority, int handle)
{
    const vbsnd_pcm_t* p;
    int v, l, r, i, best = -1;
    for (p = vbsnd_pcm; p->name; p++)
        if (!strcmp(p->name, name)) break;
    if (!p->name || !p->len) return;

    v = vol >> 3;                         /* 0..127 -> 0..15 */
    if (v <= 0) return;
    if (v > 15) v = 15;
    if (sep < 0) sep = 0;
    if (sep > 255) sep = 255;
    l = (v * (256 - sep)) >> 7; if (l > 15) l = 15;
    r = (v * sep) >> 7;         if (r > 15) r = 15;

    crit_enter();
    /* same Doom channel -> replace it; else a free voice; else steal the
     * least important (higher priority number), oldest first */
    for (i = 0; i < PCM_VOICES; i++)
        if (pcm[i].data && pcm[i].handle == handle) { best = i; break; }
    if (best < 0)
        for (i = 0; i < PCM_VOICES; i++)
            if (!pcm[i].data) { best = i; break; }
    if (best < 0) {
        for (i = 0; i < PCM_VOICES; i++) {
            if (pcm[i].prio < priority) continue;      /* playing one is more important */
            if (best < 0 || pcm[i].prio > pcm[best].prio ||
                (pcm[i].prio == pcm[best].prio && pcm[i].age < pcm[best].age))
                best = i;
        }
    }
    if (best >= 0) {
        pcmvoice_t* pv = &pcm[best];
        pv->data = 0;
        pv->pos = 0; pv->len = p->len;
        pv->vl = (u32)l; pv->vr = (u32)r;
        pv->prio = priority; pv->handle = handle; pv->age = ++pcm_clock;
        pv->data = p->data;
    }
    crit_leave();
}
