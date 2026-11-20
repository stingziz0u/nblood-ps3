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
 * The audiolib's PCM driver on the PS3's libaudio: one port, 2 channels,
 * 48 kHz float32, 8 blocks of 256 frames.
 *
 * MultiVoc mixes everything (sound effects, MIDI through the OPL3 or
 * SoundFont synth, Ogg/FLAC/XM music) into its own ring of 16 bit stereo
 * "divisions" and asks for the next one through a callback, as with SDL:
 * this driver is SDL's fillData() fed by the hardware instead of SDL.
 *
 * The audio thread follows the notes of the 8 bit ports (CrispyCell 1.1):
 *  - audioSetNotifyEventQueue is global (one port, one queue, 3.5);
 *  - audioPortStart, or nothing plays (3.6);
 *  - the buffer is float32, not int16 (3.7);
 *  - every wake-up reads which block the hardware is playing (readIndex)
 *    and tops the ring up to LEAD_BLOCKS ahead of it, so a late or lost
 *    notification never leaves the writer behind the reader (3.41);
 *  - the thread is joined before the port and the mutex go (3.8: a
 *    mutex destroyed under a live audio thread restarts the console).
 *
 * MultiVoc's callback runs with the driver locked, as SDL runs its audio
 * callback with SDL's audio lock: MV_Lock() is PCM_Lock() (recursive,
 * MultiVoc counts its own nesting too).
 *
 * =======================================================================
 */

#include <stdint.h>
#include <string.h>

#include <ppu-types.h>
#include <audio/audio.h>
#include <sys/event_queue.h>
#include <sys/mutex.h>
#include <sys/thread.h>

#include "driver_ps3.h"
#include "ps3_platform.h"
#include "ps3_prof.h"

/* the synth renders its next buffer of music before the mix, outside
   the lock (driver_sf2.cpp) */
extern "C" void SF2_PS3_Prerender(void);

#define PS3_AUDIO_RATE   48000
/* 6 of the 8 blocks queued ahead (32 ms): room for a slow mix (the
   SoundFont synth) before the hardware catches up with the writer */
#define LEAD_BLOCKS      6

enum
{
    PS3Err_Error = -1,
    PS3Err_Ok = 0,
    PS3Err_Uninitialised,
    PS3Err_AudioInit,
    PS3Err_PortOpen,
    PS3Err_Thread,
};

static int ErrorCode = PS3Err_Ok;
static int Initialised;
static volatile int Playing;

static char *MixBuffer;
static int   MixBufferSize;
static int   MixBufferCount;
static int   MixBufferCurrent;
static int   MixBufferUsed;
static void (*MixCallBack)(void);

static u32              port = (u32)-1;
static audioPortConfig  config;
static sys_event_queue_t queue;
static u64              queue_key;
static sys_ppu_thread_t thread_id;
static volatile int     running;
static sys_mutex_t      lock;
static int              lock_ok;

/* one hardware block of 16 bit stereo, as MultiVoc mixes it */
static int16_t blockbuf[AUDIO_BLOCK_SAMPLES * 2];

/* SDL's fillData(): copies 'remaining' bytes of MultiVoc's ring into ptr,
   asking for the next division whenever one is used up. Driver locked. */
static void
FillData(uint8_t *ptr, int remaining)
{
    if (!MixBuffer || !MixCallBack)
    {
        memset(ptr, 0, (size_t)remaining);
        return;
    }

    while (remaining > 0)
    {
        if (MixBufferUsed == MixBufferSize)
        {
            MixCallBack();

            MixBufferUsed = 0;

            if (++MixBufferCurrent >= MixBufferCount)
                MixBufferCurrent -= MixBufferCount;
        }

        while (remaining > 0 && MixBufferUsed < MixBufferSize)
        {
            char *sptr = MixBuffer + (MixBufferCurrent * MixBufferSize) + MixBufferUsed;
            int len = MixBufferSize - MixBufferUsed;

            if (remaining < len)
                len = remaining;

            memcpy(ptr, sptr, (size_t)len);

            ptr += len;
            MixBufferUsed += len;
            remaining -= len;
        }
    }
}

static void
FillBlock(float *dst)
{
    int const n = AUDIO_BLOCK_SAMPLES * 2;

    if (Playing)
    {
        SF2_PS3_Prerender();

        PS3Drv_PCM_Lock();
        FillData((uint8_t *)blockbuf, (int)sizeof(blockbuf));
        PS3Drv_PCM_Unlock();

        for (int i = 0; i < n; i++)
            dst[i] = (float)blockbuf[i] * (1.0f / 32768.0f);
    }
    else
    {
        memset(dst, 0, (size_t)n * sizeof(float));
    }
}

/* the SoundFont synth's numbers (driver_sf2.cpp), when it has some */
extern "C" const char *SF2_PS3_TakeStatsMessage(void);


/* what filling a block costs (the whole mix: effects, music, synth),
   and how close the writer came to the hardware: in the log every 10 s */
static unsigned long long fill_last;
static unsigned fill_count, fill_us, fill_max, fill_minqueued = LEAD_BLOCKS;

static void
FillStats(unsigned us)
{
    if (!PS3_LogsEnabled())
        return;

    unsigned long long const now = PS3_Micros();

    fill_count++;
    fill_us += us;
    if (us > fill_max)
        fill_max = us;
    if (fill_last == 0)
        fill_last = now;

    if (now - fill_last < 10000000ull)
        return;

    PS3_Log("[audio] %u blocks, fill %u us avg, %u us max (%.1f%% of real time), lowest lead %u of %d blocks at wake-up",
            fill_count, fill_us / fill_count, fill_max, fill_us * 100.0 / (double)(now - fill_last),
            fill_minqueued, LEAD_BLOCKS);

    fill_last = now;
    fill_count = fill_us = fill_max = 0;
    fill_minqueued = LEAD_BLOCKS;
}

static void
AudioThread(void *arg)
{
    (void)arg;

    int const num_blocks = (int)config.numBlocks;
    int const block_floats = AUDIO_BLOCK_SAMPLES * (int)config.channelCount;
    volatile u64 *read_index = (volatile u64 *)(uintptr_t)config.readIndex;
    float *data_start = (float *)(uintptr_t)config.audioDataStart;
    int next_fill = 1 % num_blocks;
    unsigned long long restarts = 0;

    while (running)
    {
        sys_event_t event;

        /* timeout or not, fall through and top the ring up */
        sysEventQueueReceive(queue, &event, 20 * 1000);

        int const playing = (int)(*read_index % (u64)num_blocks);
        int queued = (next_fill - playing - 1 + num_blocks) % num_blocks;

        /* the hardware lapped the writer during a long stall: start again
           right after the block that is playing */
        if (next_fill == playing || queued > LEAD_BLOCKS)
        {
            next_fill = (playing + 1) % num_blocks;
            queued = 0;

            if (restarts++ < 5)
                PS3_Log("[audio] writer restarted after the playing block");
        }

        /* still queued when the thread woke: how close it came */
        if ((unsigned)queued < fill_minqueued)
            fill_minqueued = (unsigned)queued;

        while (queued < LEAD_BLOCKS)
        {
            unsigned long long const t0 = PS3_Micros();

            FillBlock(data_start + next_fill * block_floats);
            next_fill = (next_fill + 1) % num_blocks;
            queued++;

            FillStats((unsigned)(PS3_Micros() - t0));
        }

        if (char const *const msg = SF2_PS3_TakeStatsMessage())
            PS3_Log("%s", msg);
    }

    sysThreadExit(0);
}

int PS3Drv_GetError(void) { return ErrorCode; }

const char *PS3Drv_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case PS3Err_Error:         return PS3Drv_ErrorString(ErrorCode);
        case PS3Err_Ok:            return "PS3 audio ok.";
        case PS3Err_Uninitialised: return "PS3 audio uninitialised.";
        case PS3Err_AudioInit:     return "PS3 audio: audioInit failed.";
        case PS3Err_PortOpen:      return "PS3 audio: the audio port did not open.";
        case PS3Err_Thread:        return "PS3 audio: the audio thread did not start.";
        default:                   return "Unknown PS3 audio error code.";
    }
}

static void
ClosePort(int stage)
{
    if (stage >= 4) audioPortStop(port);
    if (stage >= 3) audioRemoveNotifyEventQueue(queue_key);
    if (stage >= 2) sysEventQueueDestroy(queue, 0);
    if (stage >= 1) audioPortClose(port);
    audioQuit();
}

int PS3Drv_PCM_Init(int *mixrate, int *numchannels, void *initdata)
{
    (void)initdata;

    if (Initialised)
        PS3Drv_PCM_Shutdown();

    if (!lock_ok)
    {
        sys_mutex_attr_t attr;

        memset(&attr, 0, sizeof(attr));
        attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
        attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
        attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
        attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
        strcpy(attr.name, "pcmlock");

        /* created once and never destroyed (3.8) */
        lock_ok = (sysMutexCreate(&lock, &attr) == 0);
    }

    if (audioInit() != 0)
    {
        PS3_Log("[audio] audioInit failed");
        ErrorCode = PS3Err_AudioInit;
        return PS3Err_Error;
    }

    audioPortParam params;

    memset(&params, 0, sizeof(params));
    params.numChannels = AUDIO_PORT_2CH;
    params.numBlocks   = AUDIO_BLOCK_8;
    params.attrib      = 0;
    params.level       = 1;

    if (audioPortOpen(&params, &port) != 0)
    {
        PS3_Log("[audio] audioPortOpen failed");
        ClosePort(0);
        ErrorCode = PS3Err_PortOpen;
        return PS3Err_Error;
    }

    if (audioGetPortConfig(port, &config) != 0)
    {
        PS3_Log("[audio] audioGetPortConfig failed");
        ClosePort(1);
        ErrorCode = PS3Err_PortOpen;
        return PS3Err_Error;
    }

    if (audioCreateNotifyEventQueue(&queue, &queue_key) != 0)
    {
        PS3_Log("[audio] audioCreateNotifyEventQueue failed");
        ClosePort(1);
        ErrorCode = PS3Err_PortOpen;
        return PS3Err_Error;
    }

    if (audioSetNotifyEventQueue(queue_key) != 0)
    {
        PS3_Log("[audio] audioSetNotifyEventQueue failed");
        ClosePort(2);
        ErrorCode = PS3Err_PortOpen;
        return PS3Err_Error;
    }

    sysEventQueueDrain(queue);

    /* silence in the whole ring before it starts */
    memset((void *)(uintptr_t)config.audioDataStart, 0,
           (size_t)config.numBlocks * config.channelCount * AUDIO_BLOCK_SAMPLES * sizeof(float));

    if (audioPortStart(port) != 0)
    {
        PS3_Log("[audio] audioPortStart failed");
        ClosePort(3);
        ErrorCode = PS3Err_PortOpen;
        return PS3Err_Error;
    }

    Playing = 0;
    running = 1;

    /* priority 500, ahead of the game's 1000 (lower runs first on lv2):
       the ring is ~21 ms ahead and must not wait for a busy frame. The
       Vorbis and XMP decoders run in MultiVoc's callback: 256 KB. */
    if (sysThreadCreate(&thread_id, AudioThread, NULL, 500, 256 * 1024,
                        THREAD_JOINABLE, (char *)"duke_audio") != 0)
    {
        PS3_Log("[audio] sysThreadCreate failed");
        running = 0;
        ClosePort(4);
        ErrorCode = PS3Err_Thread;
        return PS3Err_Error;
    }

    *mixrate = PS3_AUDIO_RATE;
    *numchannels = 2;

    PS3_Log("[audio] port open: %d Hz, %d channels, %d blocks",
            PS3_AUDIO_RATE, (int)config.channelCount, (int)config.numBlocks);

    Initialised = 1;
    ErrorCode = PS3Err_Ok;

    return PS3Err_Ok;
}

void PS3Drv_PCM_Shutdown(void)
{
    if (!Initialised)
        return;

    Playing = 0;

    if (running)
    {
        u64 code;

        running = 0;
        sysThreadJoin(thread_id, &code);
    }

    ClosePort(4);

    MixBuffer = NULL;
    MixCallBack = NULL;
    Initialised = 0;

    PS3_Log("[audio] port closed");
}

int PS3Drv_PCM_BeginPlayback(char *BufferStart, int BufferSize, int NumDivisions, void (*CallBackFunc)(void))
{
    if (!Initialised)
    {
        ErrorCode = PS3Err_Uninitialised;
        return PS3Err_Error;
    }

    if (Playing)
        PS3Drv_PCM_StopPlayback();

    PS3Drv_PCM_Lock();

    MixBuffer = BufferStart;
    MixBufferSize = BufferSize;
    MixBufferCount = NumDivisions;
    MixBufferCurrent = 0;
    MixBufferUsed = 0;
    MixCallBack = CallBackFunc;

    // prime the buffer
    MixCallBack();

    Playing = 1;

    PS3Drv_PCM_Unlock();

    PS3_Log("[audio] playback: %d divisions of %d bytes", NumDivisions, BufferSize);

    return PS3Err_Ok;
}

void PS3Drv_PCM_StopPlayback(void)
{
    if (!Initialised || !Playing)
        return;

    PS3Drv_PCM_Lock();
    Playing = 0;
    PS3Drv_PCM_Unlock();
}

// MV_Lock: when the audio thread has the lock (mixing a division), how
// long the game thread waits for it goes to the slow frames' log
// (ps3_prof.c). The audio thread is never counted there.
void PS3Drv_PCM_Lock(void)
{
    if (!lock_ok)
        return;

    if (sysMutexTryLock(lock) == 0)
        return;

    PS3_ProfBegin(PS3P_ALOCK);
    sysMutexLock(lock, 0);
    PS3_ProfEnd(PS3P_ALOCK);
}

void PS3Drv_PCM_Unlock(void)
{
    if (lock_ok)
        sysMutexUnlock(lock);
}
