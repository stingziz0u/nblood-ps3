/*
 * ps3_debug.c -- EDuke32-PS3: what is left when the game thread dies.
 *
 * A retail console gives no PC for a crash (lv2's exception handler isn't
 * there), but when the game thread dies the main thread lives on in
 * sysThreadJoin, which comes back with -1. So the game thread leaves a
 * trail in memory, with no I/O, and the main thread writes it to the log
 * (CrispyCell's i_ps3debug.c, re3-PS3's method):
 *
 * - ftrace: the files in the Makefile's FTRACE_OBJS are compiled with
 *   -finstrument-functions; every function entry and exit on the game
 *   thread's stack goes into a ring of 262144 events. The dump rebuilds
 *   the calls still open when the thread died -- the stack at the crash,
 *   innermost last -- plus the last 256 raw events.
 * - crumbs: PS3_Crumb(tag, value), the last 256.
 *
 * The addresses are function descriptors (.opd) and return addresses:
 * ps3/tools/crashaddr.py names them from the build's ELF.
 *
 * Copyright (C) 2026 the EDuke32-PS3 contributors. GPL v2 or later.
 */

#include <stdint.h>
#include <stdio.h>

#include "ps3_platform.h"

#define NO_TRACE __attribute__((no_instrument_function))

#define NCRUMBS     256
/* the ring only in FTRACE=1 builds (1 MB otherwise unused: the Makefile
   gives this file PS3_FTRACE with the instrumented ones) */
#ifdef PS3_FTRACE
#define NFTRACE     262144      /* power of two */
#else
#define NFTRACE     1
#endif

/* the game thread's stack is filled with this at the start: what's still
   there at the bottom was never used (PS3_Debug_StackPeakKB) */
#define STACK_FILL  0x5a5a5a5au
#define STACK_GUARD (64 * 1024) /* kept clear of the stack's bottom (its
                                   start is a guess, see ThreadStart) */
#define STACK_TOP   (4 * 1024)  /* and of this frame */

typedef struct
{
	const char *tag;
	int         val;
	void       *where;
} crumb_t;

static crumb_t   crumbs[NCRUMBS];
static unsigned  crumb_n;

static unsigned  ftrace[NFTRACE];
static unsigned  ftrace_n;
static uintptr_t stack_lo, stack_hi;    /* the game thread's stack */

void NO_TRACE __attribute__((noinline))
PS3_Crumb(const char *tag, int val)
{
	crumb_t *c = &crumbs[crumb_n++ % NCRUMBS];

	c->tag = tag;
	c->val = val;
	c->where = __builtin_return_address(0);
}

/* First thing on the game thread: only its calls are traced (the audio
   thread runs some of the same code). */
void NO_TRACE
PS3_Debug_ThreadStart(unsigned stack_size)
{
	char here;

	stack_hi = (uintptr_t)&here + 4096;
	stack_lo = stack_hi - stack_size;

	/* the stack's high-water mark: everything below this frame (less the
	   guards, the entry's frames being a guess) filled */
	{
		uint32_t *p = (uint32_t *)((stack_lo + STACK_GUARD) & ~(uintptr_t)3);
		uint32_t *const end = (uint32_t *)(((uintptr_t)&here - STACK_TOP) & ~(uintptr_t)3);

		while (p < end)
			*p++ = STACK_FILL;
	}

	PS3_Log("[debug] crash trail ready (game thread stack %08x-%08x; function trace only in FTRACE=1 builds)",
			(unsigned)stack_lo, (unsigned)stack_hi);
}

/* The most of the game thread's stack used so far, in KB (from the
   bottom up: the first word that isn't the fill). For the [mem] lines of
   the loading screens and the exit: it reads up to 8 MB. */
unsigned NO_TRACE
PS3_Debug_StackPeakKB(void)
{
	uint32_t const *p = (uint32_t const *)((stack_lo + STACK_GUARD) & ~(uintptr_t)3);
	uint32_t const *const end = (uint32_t const *)(stack_hi - STACK_TOP);

	if (!stack_lo)
		return 0;

	while (p < end && *p == STACK_FILL)
		p++;

	return (unsigned)((stack_hi - (uintptr_t)p) / 1024);
}

static inline int NO_TRACE
OnGameThread(void)
{
	uintptr_t sp = (uintptr_t)__builtin_frame_address(0);

	return sp > stack_lo && sp < stack_hi;
}

/* ps3_prof.c: only the game thread's time counts */
int NO_TRACE
PS3_Debug_OnGameThread(void)
{
	return OnGameThread();
}

/* NBlood-PS3: the slow calls (FTRACE=1 builds). Every instrumented call
   on the game thread timed (the timebase, no syscall); the ones of 2 ms
   or more kept, the longest SLOW_MAX of each frame, for the slow frames'
   lines (ps3_prof.c: "[slow] depth function ms", named by
   ps3/tools/crashaddr.py). A call's time includes its callees': the
   deepest slow one is where the time went. */
#ifdef PS3_FTRACE
#define SLOW_DEPTH  256
#define SLOW_MAX    24

typedef struct
{
	unsigned fn, depth;
	uint64_t ticks;
} slowcall_t;

static unsigned  slow_depth;
static unsigned  slow_fn[SLOW_DEPTH];
static uint64_t  slow_t0[SLOW_DEPTH];
static slowcall_t slow[SLOW_MAX];
static int       slow_n;
static uint64_t  slow_min = 79800 * 2;     /* 2 ms at 79.8 MHz */

static inline uint64_t NO_TRACE
SlowTicks(void)
{
	uint64_t t;

	__asm__ volatile("mftb %0" : "=r"(t));
	return t;
}

static void NO_TRACE
SlowRecord(unsigned fn, unsigned depth, uint64_t ticks)
{
	int i, small = 0;

	if (slow_n < SLOW_MAX)
	{
		i = slow_n++;
	}
	else
	{
		for (i = 1; i < SLOW_MAX; i++)
			if (slow[i].ticks < slow[small].ticks)
				small = i;

		if (slow[small].ticks >= ticks)
			return;

		i = small;
	}

	slow[i].fn = fn;
	slow[i].depth = depth;
	slow[i].ticks = ticks;
}
#endif

/* this frame's slow calls, outermost first, in lines of "[slow] depth
   function ms" (ms = ticks / ticks_per_ms); cleared. Empty without
   FTRACE. */
int NO_TRACE
PS3_Debug_TakeSlowCalls(char *buf, unsigned size, double ticks_per_ms)
{
	unsigned len = 0;

	if (size)
		buf[0] = 0;

#ifdef PS3_FTRACE
	{
		int i, j;

		/* by depth, then the longest first */
		for (i = 1; i < slow_n; i++)
		{
			slowcall_t const s = slow[i];

			for (j = i; j > 0 && (slow[j - 1].depth > s.depth ||
					(slow[j - 1].depth == s.depth && slow[j - 1].ticks < s.ticks)); j--)
				slow[j] = slow[j - 1];

			slow[j] = s;
		}

		for (i = 0; i < slow_n && len < size; i++)
		{
			int const w = snprintf(buf + len, size - len, "%s[slow] %u %08x %.1f", len ? "\n" : "",
								   slow[i].depth, slow[i].fn, slow[i].ticks / ticks_per_ms);

			if (w > 0)
				len += (unsigned)w;
		}

		slow_n = 0;
	}
#else
	(void)ticks_per_ms;
#endif

	return (int)len;
}

void NO_TRACE __cyg_profile_func_enter(void *fn, void *site);
void NO_TRACE __cyg_profile_func_exit(void *fn, void *site);

void NO_TRACE
__cyg_profile_func_enter(void *fn, void *site)
{
	(void)site;

	if (OnGameThread())
	{
		ftrace[ftrace_n++ & (NFTRACE - 1)] = (unsigned)(uintptr_t)fn;

#ifdef PS3_FTRACE
		if (slow_depth < SLOW_DEPTH)
		{
			slow_fn[slow_depth] = (unsigned)(uintptr_t)fn;
			slow_t0[slow_depth] = SlowTicks();
		}

		slow_depth++;
#endif
	}
}

void NO_TRACE
__cyg_profile_func_exit(void *fn, void *site)
{
	(void)site;

	if (OnGameThread())
	{
		ftrace[ftrace_n++ & (NFTRACE - 1)] = (unsigned)(uintptr_t)fn | 1;

#ifdef PS3_FTRACE
		if (slow_depth)
		{
			slow_depth--;

			/* the same function (a call that never came back, a
			   longjmp, leaves the stack out of step: skipped) */
			if (slow_depth < SLOW_DEPTH && slow_fn[slow_depth] == (unsigned)(uintptr_t)fn)
			{
				uint64_t const dt = SlowTicks() - slow_t0[slow_depth];

				if (dt >= slow_min)
					SlowRecord(slow_fn[slow_depth], slow_depth, dt);
			}
		}
#endif
	}
}

/* From the main thread, after the game thread died. */
void NO_TRACE
PS3_Debug_Dump(const char *why)
{
	static unsigned stack[256];
	static int dumped;
	unsigned n, first, i, depth = 0, lost = 0;
	char line[160];
	int len;

	if (dumped++)
		return;

	PS3_Log("[crash] %s, %u crumbs, %u trace events", why, crumb_n, ftrace_n);

	n = crumb_n < NCRUMBS ? crumb_n : NCRUMBS;

	for (i = crumb_n - n; i != crumb_n; i++)
	{
		const crumb_t *c = &crumbs[i % NCRUMBS];

		PS3_Log("[crumb] %s %d @%08x", c->tag ? c->tag : "?", c->val,
				(unsigned)(uintptr_t)c->where);
	}

	/* the calls still open at the end of the ring */
	n = ftrace_n < NFTRACE ? ftrace_n : NFTRACE;
	first = ftrace_n - n;

	for (i = first; i != ftrace_n; i++)
	{
		unsigned e = ftrace[i & (NFTRACE - 1)];

		if ((e & 1) == 0)
		{
			if (depth < 256)
				stack[depth++] = e;
			else
				lost++;
		}
		else
		{
			unsigned fn = e & ~1u;
			int k;

			for (k = (int)depth - 1; k >= 0 && stack[k] != fn; k--)
				;

			if (k >= 0)
				depth = (unsigned)k;
			else
				lost++;
		}
	}

	PS3_Log("[stack] %u calls open at the end (outermost first), "
			"%u returns into callers older than the trace", depth, lost);

	for (i = 0; i < depth; i++)
		PS3_Log("[stack] %u %08x", i, stack[i]);

	/* the last raw events, 8 a line: odd = exit */
	n = ftrace_n < 256 ? ftrace_n : 256;
	len = 0;

	for (i = 0; i < n; i++)
	{
		if (i % 8 == 0)
			len = snprintf(line, sizeof(line), "[ftrace]");

		len += snprintf(line + len, sizeof(line) - (size_t)len, " %08x",
				ftrace[(ftrace_n - n + i) & (NFTRACE - 1)]);

		if (i % 8 == 7 || i == n - 1)
			PS3_Log("%s", line);
	}

	PS3_Log("[crash] end");
}
