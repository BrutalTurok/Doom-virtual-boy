#ifndef VB_SOUND_H
#define VB_SOUND_H
/*
 * Virtual Boy sound for Doom:
 *  - music: original Doom MUS scores (LZSS-compressed in ROM), played on the
 *    VSU (wave channels 1-4 for melody, noise channel for percussion)
 *  - effects: Doom's digitised effects (DS* lumps) as 4-bit PCM on channel 5,
 *    two voices mixed in software, stereo panned.
 * Everything is driven from the hardware timer (PCM rate; music at 140 Hz).
 */
typedef struct { const char* name; const unsigned char* data; unsigned int rawlen; } vbsnd_song_t;
typedef struct { const char* name; const unsigned char* data; unsigned int len; } vbsnd_pcm_t;  /* len in samples */

extern const vbsnd_song_t vbsnd_songs[];
extern const vbsnd_pcm_t vbsnd_pcm[];
extern const unsigned short vbsnd_notetab[1024];
/* PCM effects rate: timer in 20us steps, interrupt every PCM_TIMER_RELOAD ticks */
#define PCM_TIMER_RELOAD 9
#define PCM_HZ 5556

void VBSND_Init(void);
void VBSND_PlaySong(const char* name, int looping);
void VBSND_StopSong(void);
void VBSND_PauseSong(int paused);
void VBSND_SetMusicVolume(int vol);              /* 0..15 */
void VBSND_StartSfx(const char* name, int vol, int sep, int priority, int handle); /* vol 0..127, sep 0..255 */
#endif
