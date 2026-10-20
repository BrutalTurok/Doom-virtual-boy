#include <errno.h>
#include <sys/stat.h>
extern char _heap_start[];  /* ld: __heap_start */
static char* heap_ptr;
#ifndef HEAP_LIMIT
#define HEAP_LIMIT 0x06F00000
#endif
void* sbrk(int incr)
{
#ifdef VB_REAL
    { extern int lprintf(int, const char*, ...); lprintf(2, "sbrk(%d) refused", incr); }
    errno = ENOMEM; return (void*)-1;
#endif
    char* prev;
    if (!heap_ptr) heap_ptr = _heap_start;
    if ((unsigned)(heap_ptr + incr) > HEAP_LIMIT) { errno = ENOMEM; return (void*)-1; }
    prev = heap_ptr; heap_ptr += incr; { extern void vbdebug_sbrk(unsigned); vbdebug_sbrk((unsigned)heap_ptr); } return prev;
}
int write(int f, const char* p, int n) { return n; }
int read(int f, char* p, int n) { return 0; }
int close(int f) { return -1; }
int lseek(int f, int o, int w) { return 0; }
int fstat(int f, struct stat* s) { s->st_mode = S_IFCHR; return 0; }
int isatty(int f) { return 1; }
void _exit(int c) { for(;;); }
int kill(int p, int s) { return -1; }
int getpid(void) { return 1; }
void exit(int c) { for(;;); }
