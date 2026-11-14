/*
 * Copyright (C) 2026 the EDuke32-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Included first in every engine source (-include, ps3/Makefile).
 *
 * PSL1GHT's newlib declares clock_gettime() and nanosleep() only under
 * _POSIX_TIMERS, which it doesn't define, and has neither of them: the
 * Build timer (enet_gettime = clock_gettime) and sdlayer-style idle()
 * need both. They are implemented in ps3layer.cpp on top of the lv2
 * clock and sysUsleep.
 *
 * =======================================================================
 */

#ifndef PS3_COMPAT_H
#define PS3_COMPAT_H

/* newlib on the PPU makes ssize_t an int (32 bits) while size_t is 64:
   EDuke32's compat.h checks that its signed and unsigned register types
   match. Its C++ path doesn't use ssize_t; for the C files (miniz, lz4,
   xxhash, libxmp-lite) ssize_t is made a long here, before newlib picks.
   The C library's own functions are unchanged: on the PPU an int return
   comes sign-extended in a 64 bit register. */
#ifndef __cplusplus
#define __ssize_t_defined
typedef long _ssize_t;
#endif

#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME (clockid_t)1
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC (clockid_t)4
#endif

int clock_gettime(clockid_t clock_id, struct timespec *tp);
int nanosleep(const struct timespec *rqtp, struct timespec *rmtp);

/* mmulti.cpp (old networking code, unused) asks for the machine's name */
int gethostname(char *name, size_t len);

/* PSL1GHT's <netinet/in.h> has no IPv6. enet.h is included for
   enet_gettime only (no network play, no enet.cpp) but needs the
   address type of its ENetAddress complete. */
struct in6_addr
{
    unsigned char s6_addr[16];
};

#ifdef __cplusplus
}
#endif

/* C11's static_assert: newlib 1.20's <assert.h> predates it */
#if !defined __cplusplus && !defined static_assert
#define static_assert _Static_assert
#endif

#endif
