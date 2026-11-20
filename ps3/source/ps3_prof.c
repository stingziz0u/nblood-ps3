/*
 * ps3_prof.c -- NBlood-PS3: where a slow frame's time went (ps3_prof.h).
 *
 * Every 10 seconds, with the log on:
 *
 *   [prof] 10 s: frames, how many over 20 ms (a missed vblank at 60) and
 *          over 33 ms, then each section's average per frame
 *   [prof] worst: the slowest frame of those 10 s, section by section
 *
 * and a frame over 50 ms right away ("[prof] hitch"), one a second at
 * most. Times in ms, inclusive (a tile's disk read is in tiles, disk and
 * rooms); "rest" is the frame minus the top level sections. xN: how many
 * times, KB: bytes read or uploaded.
 *
 * Only the game thread counts: the audio thread takes the same lock.
 *
 * Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <ppu-types.h>
#include <sys/systime.h>

#include "ps3_platform.h"
#include "ps3_prof.h"

#define NO_TRACE __attribute__((no_instrument_function))

#define SLOW_MS     20
#define VSLOW_MS    33
#define HITCH_MS    50
#define WINDOW_S    10

int PS3_Debug_OnGameThread(void);   /* ps3_debug.c */
int PS3_Debug_TakeSlowCalls(char *buf, unsigned size, double ticks_per_ms);

/* the slow calls (FTRACE=1 builds, ps3_debug.c): this frame's, and the
   worst frame's of the window */
static char slow_cur[1024], slow_worst[1024];

/* the lines of a "[slow] ..." block, one PS3_Log each */
static void NO_TRACE
LogLines(char const *s)
{
    while (*s)
    {
        char const *const nl = strchr(s, '\n');
        int const n = nl ? (int)(nl - s) : (int)strlen(s);

        PS3_Log("%.*s", n, s);
        s += n + (nl ? 1 : 0);
    }
}

typedef struct
{
    u64 t[PS3P_COUNT];        /* timebase ticks */
    unsigned n[PS3P_COUNT];   /* times begun */
    u64 b[PS3P_COUNT];        /* bytes */
    u64 part[PS3P_MAX_PARTS]; /* the game logic's parts (ticks) */
    u64 total;
} prof_t;

static prof_t cur, win, worst;
static u64 start[PS3P_COUNT];
static int depth[PS3P_COUNT];
static u64 tb_freq, frame_start, win_start, last_hitch;
static unsigned win_frames, win_slow, win_vslow;

static char const *const *part_names;
static int part_count, part_cur = -1;
static u64 part_start;

static char const *const names[PS3P_COUNT] =
{
    "game", "rooms", "masks", "present",
    "tiles", "disk", "tex", "upload", "audio lock", "rsx sync", "flip"
};

static inline u64 NO_TRACE
Ticks(void)
{
    u64 t;

    __asm__ volatile("mftb %0" : "=r"(t));
    return t;
}

void NO_TRACE
PS3_ProfBegin(int id)
{
    if ((unsigned)id >= PS3P_COUNT || !PS3_Debug_OnGameThread())
        return;

    if (depth[id]++ == 0)
        start[id] = Ticks();

    cur.n[id]++;
}

void NO_TRACE
PS3_ProfEnd(int id)
{
    if ((unsigned)id >= PS3P_COUNT || !PS3_Debug_OnGameThread() || depth[id] <= 0)
        return;

    if (--depth[id] == 0)
        cur.t[id] += Ticks() - start[id];
}

void NO_TRACE
PS3_ProfBytes(int id, unsigned bytes)
{
    if ((unsigned)id < PS3P_COUNT && PS3_Debug_OnGameThread())
        cur.b[id] += bytes;
}

static double NO_TRACE
Ms(u64 ticks)
{
    return (double)ticks * 1000.0 / (double)tb_freq;
}

void NO_TRACE
PS3_ProfPartNames(char const *const *names_, int count)
{
    part_names = names_;
    part_count = count < PS3P_MAX_PARTS ? count : PS3P_MAX_PARTS;
}

void NO_TRACE
PS3_ProfPart(int part)
{
    u64 now;

    if (!PS3_Debug_OnGameThread())
        return;

    now = Ticks();

    if (part_cur >= 0 && part_cur < part_count)
        cur.part[part_cur] += now - part_start;

    part_cur = part < part_count ? part : -1;
    part_start = now;
}

/* " | game parts: act 50.1, seq 2.0" (the ones over 0.5 ms) */
static void NO_TRACE
Parts(char *buf, size_t size, prof_t const *p)
{
    size_t len = strlen(buf);
    int i, first = 1;

    for (i = 0; i < part_count && len < size; i++)
    {
        double const ms = Ms(p->part[i]);
        int w;

        if (ms < 0.5)
            continue;

        w = snprintf(buf + len, size - len, "%s%s %.1f", first ? " | game parts: " : ", ",
                     part_names[i], ms);
        first = 0;

        if (w > 0)
            len += (size_t)w;
    }
}

/* "game 3.1 x1, rooms 5.0, ..." for one frame (div 1) or the window's
   average (div = frames) */
static void NO_TRACE
Breakdown(char *buf, size_t size, prof_t const *p, unsigned div)
{
    u64 top = 0;
    size_t len = 0;
    int i;

    for (i = 0; i <= PS3P_PRESENT; i++)
        top += p->t[i];

    for (i = 0; i < PS3P_COUNT && len < size; i++)
    {
        int w = snprintf(buf + len, size - len, "%s%s %.1f", i ? (i == PS3P_TILE ? "; " : ", ") : "",
                         names[i], Ms(p->t[i]) / div);

        if (w > 0)
            len += (size_t)w;

        /* how many times, where it says something */
        if (len < size && i != PS3P_ROOMS && i != PS3P_MASKS && i != PS3P_PRESENT && i != PS3P_FLIP)
        {
            if (div == 1)
                w = snprintf(buf + len, size - len, " x%u", p->n[i]);
            else
                w = snprintf(buf + len, size - len, " x%.1f", (double)p->n[i] / div);

            if (w > 0)
                len += (size_t)w;
        }

        if (len < size && p->b[i])
        {
            w = snprintf(buf + len, size - len, " (%u KB)", (unsigned)(p->b[i] / 1024 / div));

            if (w > 0)
                len += (size_t)w;
        }

        if (len < size && i == PS3P_PRESENT)
        {
            w = snprintf(buf + len, size - len, ", rest %.1f",
                         p->total > top ? Ms(p->total - top) / div : 0.0);

            if (w > 0)
                len += (size_t)w;
        }
    }
}

void NO_TRACE
PS3_ProfFrame(void)
{
    char buf[768];
    u64 const now = Ticks();
    int i;

    if (!PS3_Debug_OnGameThread())
        return;

    if (!tb_freq)
    {
        tb_freq = sysGetTimebaseFrequency();

        if (!tb_freq)
            tb_freq = 79800000;

        frame_start = win_start = now;
        memset(&cur, 0, sizeof(cur));
        return;
    }

    cur.total = now - frame_start;
    frame_start = now;

    PS3_Debug_TakeSlowCalls(slow_cur, sizeof(slow_cur), (double)tb_freq / 1000.0);

    if (PS3_LogsEnabled())
    {
        double const ms = Ms(cur.total);

        win_frames++;

        if (ms > SLOW_MS)
            win_slow++;

        if (ms > VSLOW_MS)
            win_vslow++;

        for (i = 0; i < PS3P_COUNT; i++)
        {
            win.t[i] += cur.t[i];
            win.n[i] += cur.n[i];
            win.b[i] += cur.b[i];
        }

        win.total += cur.total;

        if (cur.total > worst.total)
        {
            worst = cur;
            memcpy(slow_worst, slow_cur, sizeof(slow_worst));
        }

        if (ms > HITCH_MS && Ms(now - last_hitch) >= 1000.0)
        {
            last_hitch = now;
            Breakdown(buf, sizeof(buf), &cur, 1);
            Parts(buf, sizeof(buf), &cur);
            PS3_Log("[prof] hitch %.1f ms: %s", ms, buf);
            LogLines(slow_cur);
        }

        if (Ms(now - win_start) >= WINDOW_S * 1000.0 && win_frames)
        {
            Breakdown(buf, sizeof(buf), &win, win_frames);
            PS3_Log("[prof] %d s: %u frames, %u over %d ms, %u over %d ms; per frame: %s",
                    WINDOW_S, win_frames, win_slow, SLOW_MS, win_vslow, VSLOW_MS, buf);

            Breakdown(buf, sizeof(buf), &worst, 1);
            Parts(buf, sizeof(buf), &worst);
            PS3_Log("[prof] worst %.1f ms: %s", Ms(worst.total), buf);
            LogLines(slow_worst);
            slow_worst[0] = 0;

            memset(&win, 0, sizeof(win));
            memset(&worst, 0, sizeof(worst));
            win_frames = win_slow = win_vslow = 0;
            win_start = now;
        }
    }

    memset(&cur, 0, sizeof(cur));
}
