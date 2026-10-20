#ifndef VB_MEM_H
#define VB_MEM_H
/*
 * Virtual Boy memory map used by this port (real-hardware layout).
 *
 * The VIP's XP (drawing) unit is disabled; we write straight into the four
 * frame buffers. That leaves the CHR RAM, BGMap/param DRAM, world attributes
 * and OAM unused by the hardware, so we use them as general RAM.
 *
 *  0x00000-0x05FFF  left FB0        (display)
 *  0x06000-0x07FFF  CHR seg 0  8KB  -> zone
 *  0x08000-0x0DFFF  left FB1        (display)
 *  0x0E000-0x0FFFF  CHR seg 1  8KB  -> zone
 *  0x10000-0x15FFF  right FB0       (display)
 *  0x16000-0x17FFF  CHR seg 2  8KB  -> zone
 *  0x18000-0x1DFFF  right FB1       (display)
 *  0x1E000-0x1FFFF  CHR seg 3  8KB  -> zone
 *  0x29600-0x2D5FF  column cache (16KB)
 *  0x20000-0x295FF  zone
 *  0x2D600-0x3DBFF  zone (BGMap DRAM + world attrs)
 *  0x3DC00-0x3DFFF  column table (used by display - do not touch)
 *  0x3E000-0x3FFFF  OAM 8KB -> zone
 */
#define VB_COLUMNCACHE  ((unsigned char*)0x00029600)

#define VB_ZONE_REGIONS { \
    { 0x0002D600, 0x0003DC00 }, \
    { 0x00020000, 0x00029600 }, \
    { 0x00006000, 0x00008000 }, \
    { 0x0000E000, 0x00010000 }, \
    { 0x00016000, 0x00018000 }, \
    { 0x0001E000, 0x00020000 }, \
    { 0x0003E000, 0x00040000 }, \
}
#endif
