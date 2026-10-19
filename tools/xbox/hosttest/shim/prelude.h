/* hosttest prelude: MSVC lacks these POSIX names */
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
typedef long ssize_t;
typedef long off_t_unused;
#define memalign(a, n) malloc(n)
#define aligned_alloc(a, n) malloc(n)
#include <fcntl.h>
#ifndef O_ACCMODE
#define O_ACCMODE (_O_RDONLY | _O_WRONLY | _O_RDWR)
#endif
/* MSVC bitfield layout: only the PVR and jump-pack words care, and the audio test uses neither */
#define _Static_assert(c, m)
