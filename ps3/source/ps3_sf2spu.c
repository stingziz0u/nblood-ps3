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
 * The PPU side of the SoundFont synth on an SPU (sf2spu_wire.h): starts
 * the SPU thread the first time the synth asks for it, and runs one job
 * at a time for the audio thread (the only caller: SF2Drv_MIDI_Service,
 * from MultiVoc's mix callback).
 *
 * NBlood-PS3: the voices split between two SPUs (see MAX_SPUS).
 *
 * A job is a signal to the SPU and a wait on an event queue: the audio
 * thread sleeps while the SPU renders, the PPU is free for the game.
 * Anything that goes wrong (no SPU, a job that never comes back) turns
 * the SPU off for good, and the synth renders on the PPU as before.
 *
 * Without the SPU toolchain (no spu-gcc) PS3_SF2_SPU isn't defined and
 * only the synth's lock is here: the synth stays on the PPU.
 *
 * =======================================================================
 */

#include <stdint.h>
#include <string.h>

#include <ppu-types.h>
#include <sys/mutex.h>

/* The synth's lock (driver_sf2.cpp: the music renders ahead, outside the
   PCM driver's lock). Recursive; created once, never destroyed. Before
   it exists (no SoundFont yet) locking does nothing. */
static sys_mutex_t sf2_lock;
static int         sf2_lock_ok;

void PS3_SF2_LockInit(void)
{
    sys_mutex_attr_t attr;

    if (sf2_lock_ok)
        return;

    memset(&attr, 0, sizeof(attr));
    attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
    attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
    attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
    attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
    strcpy(attr.name, "sf2lock");

    sf2_lock_ok = (sysMutexCreate(&sf2_lock, &attr) == 0);
}

void PS3_SF2_Lock(void)    { if (sf2_lock_ok) sysMutexLock(sf2_lock, 0); }
int  PS3_SF2_TryLock(void) { return sf2_lock_ok ? sysMutexTryLock(sf2_lock) == 0 : 1; }
void PS3_SF2_Unlock(void)  { if (sf2_lock_ok) sysMutexUnlock(sf2_lock); }

#ifdef PS3_SF2_SPU

#include <sys/spu.h>
#include <sys/event_queue.h>

#include "ps3_platform.h"
#include "sf2spu_wire.h"

/* ps3/spu/sf2spu.c, linked in by bin2s (obj/spu/sf2spu.bin) */
extern const u8 sf2spu_bin[];

#define JOB_TIMEOUT_US (200 * 1000)

/* NBlood-PS3 patch 31: the voices on two SPUs, each with its job, its
   voices and its output (summed here). One SPU took up to 70% of real
   time with Blood's music at 70 voices. Each tsf voice stays on the same
   SPU chunk after chunk while it plays (its samples stay in that SPU's
   cache); a new one goes to the SPU with fewer. If the second SPU won't
   start, one does all, as before. */
#define MAX_SPUS 2

static sf2spu_job   tmpl __attribute__((aligned(128)));   /* the caller fills it */
static sf2spu_job   jobs[MAX_SPUS] __attribute__((aligned(128)));
static sf2spu_voice voices[MAX_SPUS][SF2SPU_MAX_VOICES] __attribute__((aligned(128)));
static float        outs[MAX_SPUS][SF2SPU_MAX_SAMPLES * 2 + 32] __attribute__((aligned(128)));
static float        mixed[SF2SPU_MAX_SAMPLES * 2 + 32] __attribute__((aligned(128)));
static float const *out_ready = mixed;

static int               state;         /* 0 not tried, 1 running, -1 off */
static int               nspu;
static sysSpuImage       image;
static sys_spu_group_t   group_id;
static sys_spu_thread_t  thread_ids[MAX_SPUS];
static sys_event_queue_t queue;
static u32               seqs[MAX_SPUS];
static u32               nv[MAX_SPUS];
static int               flush_pending[MAX_SPUS];

/* which SPU renders each tsf voice (-1 none), and whether it played in
   this chunk */
#define MAX_INDEX 256
static signed char   owner[MAX_INDEX];
static unsigned char seen[MAX_INDEX];

/* statistics, logged every 10 s by the caller's thread */
static u32 st_jobs, st_voices, st_misses, st_waits, st_ticks, st_ticks_max;

#define ptr2ea(x) ((u64)(uintptr_t)(x))

/* a group of n SPU threads; 0 when it can't be had */
static int
StartGroup(int n)
{
    static const char group_name[] = "sf2spu";
    static const char thread_name[] = "sf2spu";
    sysSpuThreadGroupAttribute gattr;
    sysSpuThreadAttribute tattr;
    sysSpuThreadArgument targ;
    s32 ret;
    int k;

    memset(&gattr, 0, sizeof(gattr));
    gattr.nsize = sizeof(group_name);
    gattr.name = group_name;
    gattr.type = 0;

    /* priority 100 (16 is the highest): above the defaults, the audio
       thread waits on it */
    ret = sysSpuThreadGroupCreate(&group_id, (u32)n, 100, &gattr);
    if (ret)
    {
        PS3_Log("[sf2spu] sysSpuThreadGroupCreate (%d SPUs) failed: 0x%08x", n, (unsigned)ret);
        return 0;
    }

    memset(&tattr, 0, sizeof(tattr));
    tattr.name = thread_name;
    tattr.nsize = sizeof(thread_name);
    tattr.option = SPU_THREAD_ATTR_NONE;

    for (k = 0; k < n; k++)
    {
        /* arg0: its job; arg1: the event port it answers on (which SPU) */
        memset(&targ, 0, sizeof(targ));
        targ.arg0 = ptr2ea(&jobs[k]);
        targ.arg1 = SF2SPU_PORT + k;

        ret = sysSpuThreadInitialize(&thread_ids[k], group_id, (u32)k, &image, &tattr, &targ);
        if (ret == 0)
            ret = sysSpuThreadSetConfiguration(thread_ids[k], SPU_SIGNAL1_OVERWRITE | SPU_SIGNAL2_OVERWRITE);
        if (ret == 0)
            ret = sysSpuThreadConnectEvent(thread_ids[k], queue, SPU_THREAD_EVENT_USER, (u8)(SF2SPU_PORT + k));
        if (ret)
            break;
    }

    if (ret == 0)
        ret = sysSpuThreadGroupStart(group_id);

    if (ret)
    {
        PS3_Log("[sf2spu] SPU thread setup (%d SPUs) failed: 0x%08x", n, (unsigned)ret);
        sysSpuThreadGroupDestroy(group_id);
        return 0;
    }

    return 1;
}

static int
Start(void)
{
    sys_event_queue_attr_t qattr = { SYS_EVENT_QUEUE_PRIO, SYS_EVENT_QUEUE_PPU, "sf2spuq" };
    s32 ret;
    int k;

    /* once per process; an error here means it was already done */
    ret = sysSpuInitialize(6, 0);
    PS3_Log("[sf2spu] sysSpuInitialize: 0x%08x", (unsigned)ret);

    ret = sysSpuImageImport(&image, sf2spu_bin, 0);
    if (ret)
    {
        PS3_Log("[sf2spu] sysSpuImageImport failed: 0x%08x", (unsigned)ret);
        return 0;
    }

    ret = sysEventQueueCreate(&queue, &qattr, SYS_EVENT_QUEUE_KEY_LOCAL, 8);
    if (ret)
    {
        PS3_Log("[sf2spu] sysEventQueueCreate failed: 0x%08x", (unsigned)ret);
        sysSpuImageClose(&image);
        return 0;
    }

    for (nspu = MAX_SPUS; nspu > 0; nspu--)
        if (StartGroup(nspu))
            break;

    if (nspu == 0)
    {
        sysEventQueueDestroy(queue, 0);
        sysSpuImageClose(&image);
        return 0;
    }

    for (k = 0; k < MAX_INDEX; k++)
        owner[k] = -1;

    PS3_Log("[sf2spu] SoundFont voices render on %d SPU%s (job %u bytes, voice %u bytes)",
            nspu, nspu == 1 ? "" : "s", (unsigned)sizeof(sf2spu_job), (unsigned)sizeof(sf2spu_voice));
    return 1;
}

int PS3_SF2SPU_Available(void)
{
    if (state == 0)
        state = Start() ? 1 : -1;

    return state > 0;
}

sf2spu_job *PS3_SF2SPU_Job(void) { return &tmpl; }
float const *PS3_SF2SPU_Out(void) { return out_ready; }
int PS3_SF2SPU_Count(void) { return state > 0 ? nspu : 0; }

/* a new chunk: no voices yet; the voices that didn't play in the last
   one lose their SPU */
void PS3_SF2SPU_Begin(void)
{
    int k;

    for (k = 0; k < MAX_INDEX; k++)
    {
        if (!seen[k])
            owner[k] = -1;

        seen[k] = 0;
    }

    for (k = 0; k < MAX_SPUS; k++)
        nv[k] = 0;
}

/* the slot for tsf voice 'index' in this chunk; NULL when all are full */
sf2spu_voice *PS3_SF2SPU_Add(int index)
{
    int k = -1, i;

    if ((unsigned)index < MAX_INDEX)
    {
        seen[index] = 1;
        k = owner[index];
    }

    if (k < 0 || k >= nspu || nv[k] >= SF2SPU_MAX_VOICES)
    {
        k = -1;

        for (i = 0; i < nspu; i++)
            if (nv[i] < SF2SPU_MAX_VOICES && (k < 0 || nv[i] < nv[k]))
                k = i;

        if (k < 0)
            return NULL;

        if ((unsigned)index < MAX_INDEX)
            owner[index] = (signed char)k;
    }

    return &voices[k][nv[k]++];
}

/* the voices SPU k rendered in the last Run (and how many) */
int PS3_SF2SPU_Results(int k, sf2spu_voice **v)
{
    if (k < 0 || k >= nspu)
        return 0;

    *v = voices[k];
    return (int)nv[k];
}

/* runs the chunk (tmpl, the voices added since Begin); 0 when the
   results are there */
int PS3_SF2SPU_Run(void)
{
    sys_event_t ev;
    s32 ret = 0;
    int k, pending = 0, ran = 0, last = 0;
    u32 total = 0, ticks_max = 0;
    int const floats = (int)tmpl.numSamples << (tmpl.stereo ? 1 : 0);

    if (state <= 0)
        return -1;

    if (tmpl.flush)
        for (k = 0; k < nspu; k++)
            flush_pending[k] = 1;

    for (k = 0; k < nspu; k++)
    {
        if (!nv[k])
            continue;

        jobs[k] = tmpl;
        jobs[k].voices = ptr2ea(voices[k]);
        jobs[k].out = ptr2ea(outs[k]);
        jobs[k].numVoices = nv[k];
        jobs[k].flush = flush_pending[k];
        jobs[k].done = 0;
        flush_pending[k] = 0;
        seqs[k]++;
    }

    /* the jobs, the voices: in memory before the SPUs read them */
    __asm__ volatile ("sync" ::: "memory");

    for (k = 0; k < nspu && ret == 0; k++)
    {
        if (!nv[k])
            continue;

        ret = sysSpuThreadWriteSignal(thread_ids[k], 0, SF2SPU_SIGNAL_RUN);
        if (ret == 0)
            pending |= 1 << k;
    }

    /* the answers, in any order: the event port says which SPU */
    while (ret == 0 && pending)
    {
        memset(&ev, 0, sizeof(ev));
        ret = sysEventQueueReceive(queue, &ev, JOB_TIMEOUT_US);

        if (ret)
            break;

        k = (int)((ev.data_2 >> 32) & 0xFF) - SF2SPU_PORT;

        /* lv2 puts the port above the value; else the thread's id */
        if (k < 0 || k >= nspu)
            for (k = nspu - 1; k >= 0; k--)
                if (ev.data_1 == (u64)thread_ids[k])
                    break;

        if (ev.source != SPU_THREAD_EVENT_USER_KEY || k < 0 || k >= nspu || !(pending & (1 << k))
            || (u32)(ev.data_2 & 0xFFFFFF) != (seqs[k] & 0xFFFFFF))
        {
            ret = -1;
            break;
        }

        pending &= ~(1 << k);
    }

    if (ret || pending)
    {
        PS3_Log("[sf2spu] job failed (0x%08x, source %llx, data %llx %llx, waiting for %x): back to the PPU",
                (unsigned)ret, (unsigned long long)ev.source, (unsigned long long)ev.data_1,
                (unsigned long long)ev.data_2, (unsigned)pending);
        state = -1;
        return -1;
    }

    __asm__ volatile ("lwsync" ::: "memory");

    for (k = 0; k < nspu; k++)
    {
        if (!nv[k])
            continue;

        ran++;
        last = k;
        total += nv[k];
        st_misses += jobs[k].misses;
        st_waits += jobs[k].waits;
        if (jobs[k].ticks > ticks_max)
            ticks_max = jobs[k].ticks;
    }

    /* one SPU's output as it is, two summed */
    if (ran == 1)
        out_ready = outs[last];
    else
    {
        int i;

        memset(mixed, 0, (size_t)floats * sizeof(float));

        for (k = 0; k < nspu; k++)
        {
            if (!nv[k])
                continue;

            for (i = 0; i < floats; i++)
                mixed[i] += outs[k][i];
        }

        out_ready = mixed;
    }

    st_jobs++;
    st_voices += total;
    st_ticks += ticks_max;
    if (ticks_max > st_ticks_max)
        st_ticks_max = ticks_max;

    return 0;
}

/* jobs since the last call, their voices, cache lines fetched (and of
   those, the ones the SPU stopped for), SPU time (us, total and the
   longest job; with two SPUs the slower one's, what the audio thread
   waited); resets the counts */
void PS3_SF2SPU_TakeStats(unsigned *jobs_, unsigned *voices_, unsigned *misses,
                          unsigned *waits, unsigned *spu_us, unsigned *spu_max_us)
{
    /* the SPU decrementer runs at the timebase, 79.8 MHz */
    *jobs_ = st_jobs;
    *voices_ = st_voices;
    *misses = st_misses;
    *waits = st_waits;
    *spu_us = (unsigned)((unsigned long long)st_ticks * 10 / 798);
    *spu_max_us = (unsigned)((unsigned long long)st_ticks_max * 10 / 798);
    st_jobs = st_voices = st_misses = st_waits = st_ticks = st_ticks_max = 0;
}

#endif /* PS3_SF2_SPU */
