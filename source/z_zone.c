// Emacs style mode select   -*- C++ -*-
//-----------------------------------------------------------------------------
//
// $Id:$
//
// Copyright (C) 1993-1996 by id Software, Inc.
//
// This source is available for distribution and/or modification
// only under the terms of the DOOM Source Code License as
// published by id Software. All rights reserved.
//
// The source is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// FITNESS FOR A PARTICULAR PURPOSE. See the DOOM Source Code License
// for more details.
//
// $Log:$
//
// DESCRIPTION:
//	Zone Memory Allocation. Neat.
//
//-----------------------------------------------------------------------------

#include "z_zone.h"
#ifdef ZDEBUG
#undef Z_Malloc
#undef Z_Calloc
#undef Z_Realloc
const char* zdbg_file; int zdbg_line;
typedef struct { void* p; int size; const char* file; int line; int tag; } zdbg_t;
zdbg_t zdbg_tab[3000];
static void zdbg_add(void* p,int size,int tag){int i; for(i=0;i<3000;i++) if(!zdbg_tab[i].p){zdbg_tab[i].p=p;zdbg_tab[i].size=size;zdbg_tab[i].file=zdbg_file;zdbg_tab[i].line=zdbg_line;zdbg_tab[i].tag=tag;break;} zdbg_file=0; zdbg_line=0;}
static void zdbg_del(void* p){int i; for(i=0;i<3000;i++) if(zdbg_tab[i].p==p){zdbg_tab[i].p=0;break;}}
#endif
#include "doomdef.h"
#include "doomtype.h"
#include "lprintf.h"


//
// ZONE MEMORY ALLOCATION
//
// There is never any space between memblocks,
//  and there will never be two contiguous free memblocks.
// The rover can be left pointing at a non-empty block.
//
// It is of no value to free a cachable block,
//  because it will get overwritten automatically if needed.
//

#define ZONEID	0x1d4a11

#ifndef ZONE_SIZE
#define ZONE_SIZE (256 * 1024)
#endif
const unsigned int maxHeapSize = ZONE_SIZE;

#ifndef GBA
    static int running_count = 0;
    extern void vbdebug_zone(int used);
#endif

typedef struct memblock_s
{
    unsigned int size:24;	// including the header and possibly tiny fragments
    unsigned int tag:4;	// purgelevel
    void**		user;	// NULL if a free block
    struct memblock_s*	next;
    struct memblock_s*	prev;
} memblock_t;


typedef struct
{
    // start / end cap for linked list
    memblock_t	blocklist;
    memblock_t*	rover;
} memzone_t;

memzone_t*	mainzone;

//
// Z_Init
//
#ifdef VB_REAL
#include "vb_mem.h"
/* Virtual Boy: the zone is spread over several non-contiguous regions
 * (spare WRAM + unused VIP memory). Each region ends in a permanent
 * PU_STATIC sentinel block so blocks are never merged across regions. */
static memzone_t vb_zone_hdr;
extern char wram_zone_start[], wram_zone_end[];   /* from linker script */
unsigned int vb_zone_total;

void Z_Init (void)
{
    static const struct { unsigned int s, e; } regs[] = VB_ZONE_REGIONS;
    const int nregs = sizeof(regs) / sizeof(regs[0]);
    memblock_t* prev;
    int i;

    mainzone = &vb_zone_hdr;
    mainzone->blocklist.user = (void *)mainzone;
    mainzone->blocklist.tag = PU_STATIC;
    mainzone->blocklist.size = 0;
    prev = &mainzone->blocklist;
    vb_zone_total = 0;

    for (i = -1; i < nregs; i++)
    {
        unsigned int start = (i < 0) ? (unsigned int)wram_zone_start : regs[i].s;
        unsigned int end   = (i < 0) ? (unsigned int)wram_zone_end   : regs[i].e;
        memblock_t *b, *sent;

        start = (start + 7) & ~7u;
        end &= ~7u;
        if (end <= start + 2 * sizeof(memblock_t) + 64)
            continue;

        b = (memblock_t *)start;
        sent = (memblock_t *)(end - sizeof(memblock_t));

        b->user = NULL;
        b->tag = 0;
        b->size = (byte *)sent - (byte *)b;

        sent->user = (void *)mainzone;
        sent->tag = PU_STATIC;
        sent->size = sizeof(memblock_t);

        prev->next = b; b->prev = prev;
        b->next = sent; sent->prev = b;
        prev = sent;
        vb_zone_total += b->size;
    }

    prev->next = &mainzone->blocklist;
    mainzone->blocklist.prev = prev;
    mainzone->rover = mainzone->blocklist.next;

    lprintf(LO_INFO,"Z_Init: VB zone %d bytes.", vb_zone_total);
}
#else
void Z_Init (void)
{
    memblock_t*	block;

    unsigned int heapSize = maxHeapSize;

    //We can now alloc all of the rest fo the memory.
    do
    {
        mainzone = malloc(heapSize);
        heapSize -= 4;

    } while(mainzone == NULL);

    heapSize += 4;

    lprintf(LO_INFO,"Z_Init: Heapsize is %d bytes.", heapSize);

    // set the entire zone to one free block
    mainzone->blocklist.next =
    mainzone->blocklist.prev =
    block = (memblock_t *)( (byte *)mainzone + sizeof(memzone_t) );

    mainzone->blocklist.user = (void *)mainzone;
    mainzone->blocklist.tag = PU_STATIC;
    mainzone->rover = block;

    block->prev = block->next = &mainzone->blocklist;

    // NULL indicates a free block.
    block->user = NULL;

    block->size = heapSize - sizeof(memzone_t);
}
#endif


//
// Z_Free
//
void Z_Free (void* ptr)
{
    memblock_t*		block;
    memblock_t*		other;

    if(ptr == NULL)
        return;

    block = (memblock_t *) ( (byte *)ptr - sizeof(memblock_t));

    if (block->user > (void **)0x100)
    {
        // smaller values are not pointers
        // Note: OS-dependend?

        // clear the user's mark
        *block->user = 0;
    }

    // mark as free
    block->user = NULL;
    block->tag = 0;


#ifndef GBA
    running_count -= block->size;
    vbdebug_zone(running_count);
#ifdef ZDEBUG
    zdbg_del(ptr);
#endif
#endif

    other = block->prev;

    if (!other->user)
    {
        // merge with previous free block
        other->size += block->size;
        other->next = block->next;
        other->next->prev = other;

        if (block == mainzone->rover)
            mainzone->rover = other;

        block = other;
    }

    other = block->next;
    if (!other->user)
    {
        // merge the next free block onto the end
        block->size += other->size;
        block->next = other->next;
        block->next->prev = block;

        if (other == mainzone->rover)
            mainzone->rover = block;
    }
}



//
// Z_Malloc
// You can pass a NULL user if the tag is < PU_PURGELEVEL.
//
#define MINFRAGMENT		64


void* Z_Malloc(int size, int tag, void **user)
{
    int		extra;
    memblock_t*	start;
    memblock_t* rover;
    memblock_t* newblock;
    memblock_t*	base;

    size = (size + 3) & ~3;

    // scan through the block list,
    // looking for the first free block
    // of sufficient size,
    // throwing out any purgable blocks along the way.

    // account for size of block header
    size += sizeof(memblock_t);

#ifdef VB_REAL
    /* VB: the zone is split in several regions, some only 8KB. Use best-fit
     * so small blocks fill small holes and big level arrays still fit. */
    {
        memblock_t* best = NULL;
        for (rover = mainzone->blocklist.next; rover != &mainzone->blocklist; rover = rover->next)
            if (!rover->user && rover->size >= (unsigned)size && (!best || rover->size < best->size))
                best = rover;
        if (best)
        {
            base = best;
            goto found_block;
        }
    }
#endif

    // if there is a free block behind the rover,
    //  back up over them
    base = mainzone->rover;

    if (!base->prev->user)
    base = base->prev;

    rover = base;
    start = base->prev;

    do
    {
        if (rover == start)
        {
            // scanned all the way around the list
#ifdef VB_REAL
            {
                extern void vbdebug_zonefail(int size, void* zone);
                vbdebug_zonefail(size, mainzone);
            }
#endif
            I_Error ("Z_Malloc: failed on allocation of %i bytes", size);
        }

        if (rover->user)
        {
            if (rover->tag < PU_PURGELEVEL)
            {
                // hit a block that can't be purged,
                //  so move base past it
                base = rover = rover->next;
            }
            else
            {
                // free the rover block (adding the size to base)

                // the rover can be the base block
                base = base->prev;
                Z_Free ((byte *)rover+sizeof(memblock_t));
                base = base->next;
                rover = base->next;
            }
        }
        else
            rover = rover->next;

    } while (base->user || base->size < size);


#ifdef VB_REAL
found_block:
#endif
    // found a block big enough
    extra = base->size - size;

    if (extra >  MINFRAGMENT)
    {
        // there will be a free fragment after the allocated block
        newblock = (memblock_t *) ((byte *)base + size );
        newblock->size = extra;

        // NULL indicates free block.
        newblock->user = NULL;
        newblock->tag = 0;
        newblock->prev = base;
        newblock->next = base->next;
        newblock->next->prev = newblock;

        base->next = newblock;
        base->size = size;
    }

    if (user)
    {
        // mark as an in use block
        base->user = user;
        *(void **)user = (void *) ((byte *)base + sizeof(memblock_t));
    }
    else
    {
        if (tag >= PU_PURGELEVEL)
            I_Error ("Z_Malloc: an owner is required for purgable blocks");

        // mark as in use, but unowned
        base->user = (void *)2;
    }

    base->tag = tag;

    // next allocation will start looking here
    mainzone->rover = base->next;

#ifndef GBA
    running_count += base->size;
    vbdebug_zone(running_count);
#ifdef ZDEBUG
    zdbg_add((byte *)base + sizeof(memblock_t), base->size, tag);
#endif
#endif

    return (void *) ((byte *)base + sizeof(memblock_t));
}

void* Z_Calloc(size_t count, size_t size, int tag, void **user)
{
    const size_t bytes = count * size;
    void* ptr = Z_Malloc(bytes, tag, user);

    if(ptr)
        memset(ptr, 0, bytes);

    return ptr;
}

char* Z_Strdup(const char* s)
{
    const unsigned int len = strlen(s);

    if(!len)
        return NULL;

    char* ptr = Z_Malloc(len+1, PU_STATIC, NULL);

    if(ptr)
        strcpy(ptr, s);

    return ptr;
}

void* Z_Realloc(void *ptr, size_t n, int tag, void **user)
{
    void *p = Z_Malloc(n, tag, user);

    if (ptr)
    {
        memblock_t *block = (memblock_t *)((char *) ptr - sizeof(memblock_t));

        memcpy(p, ptr, n <= block->size ? n : block->size);

        Z_Free(ptr);

        if (user) // in case Z_Free nullified same user
            *user = p;
    }
    return p;
}

//
// Z_FreeTags
//
void Z_FreeTags(int lowtag, int hightag)
{
    memblock_t*	block;
    memblock_t*	next;

    for (block = mainzone->blocklist.next ;
         block != &mainzone->blocklist ;
         block = next)
    {
        // get link before freeing
        next = block->next;

        // free block?
        if (!block->user)
            continue;

        if (block->tag >= lowtag && block->tag <= hightag)
            Z_Free ( (byte *)block+sizeof(memblock_t));
    }
}

//
// Z_CheckHeap
//
void Z_CheckHeap (void)
{
    memblock_t*	block;

    for (block = mainzone->blocklist.next ; ; block = block->next)
    {
        if (block->next == &mainzone->blocklist)
        {
            // all blocks have been hit
            break;
        }

        if ( (byte *)block + block->size != (byte *)block->next
#ifdef VB_REAL
             /* region sentinels are followed by the next (non-adjacent) region */
             && !(block->user == (void *)mainzone && block->size == sizeof(memblock_t))
#endif
           )
            I_Error ("Z_CheckHeap: block size does not touch the next block\n");

        if ( block->next->prev != block)
            I_Error ("Z_CheckHeap: next block doesn't have proper back link\n");

        if (!block->user && !block->next->user)
            I_Error ("Z_CheckHeap: two consecutive free blocks\n");
    }
}
