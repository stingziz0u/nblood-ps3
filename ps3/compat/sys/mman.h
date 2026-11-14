/*
 * EDuke32-PS3: PSL1GHT has no <sys/mman.h> (no mmap on the PS3).
 *
 * texcache.h includes mio.hpp (memory-mapped files) unconditionally, but
 * only the OpenGL texture cache uses it, and it's off on the PS3 (no
 * texture compression). ps3layer.cpp has mmap failing, so a mapping that
 * were ever tried would just not happen.
 */
#ifndef PS3_COMPAT_SYS_MMAN_H
#define PS3_COMPAT_SYS_MMAN_H

#include <sys/types.h>

#define PROT_NONE   0
#define PROT_READ   1
#define PROT_WRITE  2
#define PROT_EXEC   4

#define MAP_SHARED  1
#define MAP_PRIVATE 2
#define MAP_FAILED  ((void *)-1)

#define MS_ASYNC      1
#define MS_INVALIDATE 2
#define MS_SYNC       4

#ifdef __cplusplus
extern "C" {
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset);
int munmap(void *addr, size_t length);
int msync(void *addr, size_t length, int flags);

#ifdef __cplusplus
}
#endif

#endif
