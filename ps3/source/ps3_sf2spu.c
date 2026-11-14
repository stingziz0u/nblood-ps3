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

static sf2spu_job   job __attribute__((aligned(128)));
static sf2spu_voice voices[SF2SPU_MAX_VOICES] __attribute__((aligned(128)));
static float        out[SF2SPU_MAX_SAMPLES * 2 + 32] __attribute__((aligned(128)));

static int               state;         /* 0 not tried, 1 running, -1 off */
static sysSpuImage       image;
static sys_spu_group_t   group_id;
static sys_spu_thread_t  thread_id;
static sys_event_queue_t queue;
static u32               seq;

/* statistics, logged every 10 s by the caller's thread */
static u32 st_jobs, st_voices, st_misses, st_waits, st_ticks, st_ticks_max;

#define ptr2ea(x) ((u64)(uintptr_t)(x))

static int
Start(void)
{
    static const char group_name[] = "sf2spu";
    static const char thread_name[] = "sf2spu";
    sys_event_queue_attr_t qattr = { SYS_EVENT_QUEUE_PRIO, SYS_EVENT_QUEUE_PPU, "sf2spuq" };
    sysSpuThreadGroupAttribute gattr;
    sysSpuThreadAttribute tattr;
    sysSpuThreadArgument targ;
    s32 ret;

    /* once per process; an error here means it was already done */
    ret = sysSpuInitialize(6, 0);
    PS3_Log("[sf2spu] sysSpuInitialize: 0x%08x", (unsigned)ret);

    ret = sysSpuImageImport(&image, sf2spu_bin, 0);
    if (ret)
    {
        PS3_Log("[sf2spu] sysSpuImageImport failed: 0x%08x", (unsigned)ret);
        return 0;
    }

    memset(&gattr, 0, sizeof(gattr));
    gattr.nsize = sizeof(group_name);
    gattr.name = group_name;
    gattr.type = 0;

    /* priority 100 (16 is the highest): above the defaults, the audio
       thread waits on it */
    ret = sysSpuThreadGroupCreate(&group_id, 1, 100, &gattr);
    if (ret)
    {
        PS3_Log("[sf2spu] sysSpuThreadGroupCreate failed: 0x%08x", (unsigned)ret);
        sysSpuImageClose(&image);
        return 0;
    }

    ret = sysEventQueueCreate(&queue, &qattr, SYS_EVENT_QUEUE_KEY_LOCAL, 8);
    if (ret)
    {
        PS3_Log("[sf2spu] sysEventQueueCreate failed: 0x%08x", (unsigned)ret);
        sysSpuThreadGroupDestroy(group_id);
        sysSpuImageClose(&image);
        return 0;
    }

    memset(&tattr, 0, sizeof(tattr));
    tattr.name = thread_name;
    tattr.nsize = sizeof(thread_name);
    tattr.option = SPU_THREAD_ATTR_NONE;

    memset(&targ, 0, sizeof(targ));
    targ.arg0 = ptr2ea(&job);

    ret = sysSpuThreadInitialize(&thread_id, group_id, 0, &image, &tattr, &targ);
    if (ret == 0)
        ret = sysSpuThreadSetConfiguration(thread_id, SPU_SIGNAL1_OVERWRITE | SPU_SIGNAL2_OVERWRITE);
    if (ret == 0)
        ret = sysSpuThreadConnectEvent(thread_id, queue, SPU_THREAD_EVENT_USER, SF2SPU_PORT);
    if (ret == 0)
        ret = sysSpuThreadGroupStart(group_id);

    if (ret)
    {
        PS3_Log("[sf2spu] SPU thread setup failed: 0x%08x", (unsigned)ret);
        sysSpuThreadGroupDestroy(group_id);
        sysEventQueueDestroy(queue, 0);
        sysSpuImageClose(&image);
        return 0;
    }

    job.voices = ptr2ea(voices);
    job.out = ptr2ea(out);

    PS3_Log("[sf2spu] SoundFont voices render on an SPU (job %u bytes, voice %u bytes)",
            (unsigned)sizeof(job), (unsigned)sizeof(sf2spu_voice));
    return 1;
}

int PS3_SF2SPU_Available(void)
{
    if (state == 0)
        state = Start() ? 1 : -1;

    return state > 0;
}

sf2spu_job *PS3_SF2SPU_Job(void) { return &job; }
sf2spu_voice *PS3_SF2SPU_Voices(void) { return voices; }
float const *PS3_SF2SPU_Out(void) { return out; }

/* runs the job that's in job/voices; 0 when the results are there */
int PS3_SF2SPU_Run(void)
{
    sys_event_t ev;
    s32 ret;

    if (state <= 0)
        return -1;

    memset(&ev, 0, sizeof(ev));
    job.voices = ptr2ea(voices);
    job.out = ptr2ea(out);
    job.done = 0;
    seq++;

    /* the job, the voices: in memory before the SPU reads them */
    __asm__ volatile ("sync" ::: "memory");

    ret = sysSpuThreadWriteSignal(thread_id, 0, SF2SPU_SIGNAL_RUN);
    if (ret == 0)
        ret = sysEventQueueReceive(queue, &ev, JOB_TIMEOUT_US);

    if (ret || ev.source != SPU_THREAD_EVENT_USER_KEY || (u32)(ev.data_2 & 0xFFFFFF) != (seq & 0xFFFFFF))
    {
        PS3_Log("[sf2spu] job %u failed (0x%08x, source %llx, data %llx): back to the PPU",
                (unsigned)seq, (unsigned)ret, (unsigned long long)ev.source,
                (unsigned long long)ev.data_2);
        state = -1;
        return -1;
    }

    __asm__ volatile ("lwsync" ::: "memory");

    st_jobs++;
    st_voices += job.numVoices;
    st_misses += job.misses;
    st_waits += job.waits;
    st_ticks += job.ticks;
    if (job.ticks > st_ticks_max)
        st_ticks_max = job.ticks;

    return 0;
}

/* jobs since the last call, their voices, cache lines fetched (and of
   those, the ones the SPU stopped for), SPU time
   (us, total and the longest job); resets the counts */
void PS3_SF2SPU_TakeStats(unsigned *jobs, unsigned *voices_, unsigned *misses,
                          unsigned *waits, unsigned *spu_us, unsigned *spu_max_us)
{
    /* the SPU decrementer runs at the timebase, 79.8 MHz */
    *jobs = st_jobs;
    *voices_ = st_voices;
    *misses = st_misses;
    *waits = st_waits;
    *spu_us = (unsigned)((unsigned long long)st_ticks * 10 / 798);
    *spu_max_us = (unsigned)((unsigned long long)st_ticks_max * 10 / 798);
    st_jobs = st_voices = st_misses = st_waits = st_ticks = st_ticks_max = 0;
}

#endif /* PS3_SF2_SPU */
