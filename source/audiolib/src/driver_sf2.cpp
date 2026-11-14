//-------------------------------------------------------------------------
/*
Copyright (C) 2020 EDuke32 developers and contributors

This file is part of EDuke32.

EDuke32 is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------

#include "driver_sf2.h"

#include "_multivc.h"
#include "common.h"
#include "midi.h"

int SF2_EffectSampleBlockSize = 16;

#define TSF_IMPLEMENTATION
#define TSF_NO_STDIO
#define TSF_RENDER_EFFECTSAMPLEBLOCK SF2_EffectSampleBlockSize
#define TSF_MALLOC  Xmalloc
#define TSF_REALLOC Xrealloc
#define TSF_FREE    Xfree
#define TSF_MEMCPY Bmemcpy
#define TSF_MEMSET Bmemset
#ifdef __PPU__
#define TSF_SAMPLES_INT16   // PS3: the font's samples stay 16 bit (half the memory)
#endif

#include "tsf.h"

static tsf *sf2_synth;
char        SF2_BankFile[BMAX_PATH];

#ifdef __PPU__
/*
 * PS3: the voices render on an SPU when the build has one (PS3_SF2_SPU,
 * ps3/source/ps3_sf2spu.c and ps3/spu/sf2spu.c), on the PPU otherwise.
 * tsf keeps everything else here: the font, the channels, note on/off.
 * The time the synth takes goes to the log every 10 s.
 */
extern "C" unsigned long long PS3_Micros(void);
extern "C" void PS3_Log(const char *fmt, ...);
#include <malloc.h>

/* the synth's lock and the music rendered ahead (see SF2Drv_MIDI_Service) */
extern "C" {
void PS3_SF2_LockInit(void);
void PS3_SF2_Lock(void);
int  PS3_SF2_TryLock(void);
void PS3_SF2_Unlock(void);
}

static int16_t      sf2_stage[MV_MIXBUFFERSIZE * 2];
static volatile int sf2_staged;     /* sf2_stage holds the next buffer */
static int          sf2_active;     /* hooked and playing (the lock) */

#ifdef PS3_SF2_SPU
#include "sf2spu_wire.h"

extern "C" {
int           PS3_SF2SPU_Available(void);
sf2spu_job *  PS3_SF2SPU_Job(void);
sf2spu_voice *PS3_SF2SPU_Voices(void);
float const * PS3_SF2SPU_Out(void);
int           PS3_SF2SPU_Run(void);
void          PS3_SF2SPU_TakeStats(unsigned *jobs, unsigned *voices, unsigned *misses, unsigned *waits, unsigned *spu_us, unsigned *spu_max_us);
}

static_assert(sizeof(sf2w_voice_envelope) == sizeof(struct tsf_voice_envelope), "tsf_voice_envelope");
static_assert(sizeof(sf2w_voice_lowpass) == sizeof(struct tsf_voice_lowpass), "tsf_voice_lowpass");
static_assert(sizeof(sf2w_voice_lfo) == sizeof(struct tsf_voice_lfo), "tsf_voice_lfo");
static_assert(offsetof(sf2w_voice_envelope, parameters) == offsetof(struct tsf_voice_envelope, parameters), "envelope parameters");
static_assert(offsetof(sf2w_voice_envelope, segmentIsExponential) == offsetof(struct tsf_voice_envelope, segmentIsExponential), "envelope flags");
static_assert(offsetof(sf2w_voice_lowpass, active) == offsetof(struct tsf_voice_lowpass, active), "lowpass active");

static_assert(sizeof(tsf_sample) == 2, "the SPU reads the font as 16 bit samples");

static tsf_sample const *sf2spu_font;

static void SF2_RenderSPU(tsf *f, float *buffer, int samples)
{
    if (samples <= 0 || samples > SF2SPU_MAX_SAMPLES
        || (f->outputmode != TSF_STEREO_INTERLEAVED && f->outputmode != TSF_MONO) || !PS3_SF2SPU_Available())
    {
        tsf_render_float(f, buffer, samples, 0);
        return;
    }

    sf2spu_job *const   job = PS3_SF2SPU_Job();
    sf2spu_voice *const w   = PS3_SF2SPU_Voices();
    struct tsf_voice *  v = f->voices, *vEnd = v + f->voiceNum, *rest = vEnd;
    int                 n = 0;

    for (; v != vEnd; v++)
    {
        if (v->playingPreset == -1)
            continue;

        if (n == SF2SPU_MAX_VOICES)
        {
            rest = v;   /* the ones that don't fit: on the PPU, after */
            break;
        }

        sf2spu_voice *const o = &w[n++];
        struct tsf_region const *const r = v->region;

        o->pitchInputTimecents  = v->pitchInputTimecents;
        o->pitchOutputFactor    = v->pitchOutputFactor;
        o->sourceSamplePosition = v->sourceSamplePosition;
        o->noteGainDB           = v->noteGainDB;
        o->panFactorLeft        = v->panFactorLeft;
        o->panFactorRight       = v->panFactorRight;
        o->loopStart            = v->loopStart;
        o->loopEnd              = v->loopEnd;
        o->regionEnd            = r->end;
        o->initialFilterFc      = r->initialFilterFc;
        o->modEnvToPitch        = r->modEnvToPitch;
        o->modEnvToFilterFc     = r->modEnvToFilterFc;
        o->modLfoToFilterFc     = r->modLfoToFilterFc;
        o->modLfoToVolume       = r->modLfoToVolume;
        o->modLfoToPitch        = r->modLfoToPitch;
        o->vibLfoToPitch        = r->vibLfoToPitch;
        o->index                = (int32_t)(v - f->voices);
        o->ended                = 0;
        memcpy(&o->ampenv, &v->ampenv, sizeof(o->ampenv));
        memcpy(&o->modenv, &v->modenv, sizeof(o->modenv));
        memcpy(&o->lowpass, &v->lowpass, sizeof(o->lowpass));
        memcpy(&o->modlfo, &v->modlfo, sizeof(o->modlfo));
        memcpy(&o->viblfo, &v->viblfo, sizeof(o->viblfo));
    }

    int const floats = samples * (f->outputmode == TSF_MONO ? 1 : 2);

    if (n == 0)
    {
        memset(buffer, 0, floats * sizeof(float));
        return;
    }

    job->fontSamples   = (uint64_t)(uintptr_t)f->fontSamples;
    job->numVoices     = n;
    job->numSamples    = samples;
    job->stereo        = (f->outputmode == TSF_STEREO_INTERLEAVED);
    job->interp        = f->interpolatemode;
    job->outSampleRate = f->outSampleRate;
    job->flush         = (sf2spu_font != f->fontSamples);
    job->effectBlock   = TSF_RENDER_EFFECTSAMPLEBLOCK;
    sf2spu_font        = f->fontSamples;

    if (PS3_SF2SPU_Run() != 0)
    {
        /* the voices here are as they were: render them here */
        tsf_render_float(f, buffer, samples, 0);
        return;
    }

    for (int i = 0; i < n; i++)
    {
        sf2spu_voice const *const o = &w[i];
        struct tsf_voice *const   d = &f->voices[o->index];

        if (o->ended)
        {
            tsf_voice_kill(d);
            continue;
        }

        d->sourceSamplePosition = o->sourceSamplePosition;
        memcpy(&d->ampenv, &o->ampenv, sizeof(d->ampenv));
        memcpy(&d->modenv, &o->modenv, sizeof(d->modenv));
        memcpy(&d->lowpass, &o->lowpass, sizeof(d->lowpass));
        memcpy(&d->modlfo, &o->modlfo, sizeof(d->modlfo));
        memcpy(&d->viblfo, &o->viblfo, sizeof(d->viblfo));
    }

    memcpy(buffer, PS3_SF2SPU_Out(), floats * sizeof(float));

    for (v = rest; v != vEnd; v++)
        if (v->playingPreset != -1)
            tsf_voice_render(f, v, buffer, samples);
}
#define SF2_RENDER(f, buf, n) SF2_RenderSPU(f, buf, n)
#else
#define SF2_RENDER(f, buf, n) tsf_render_float(f, buf, n, 0)
#endif

static int      sf2_tempo_bpm, sf2_tempo_division;
static unsigned sf2_tempo_changes;

static unsigned long long sf2_stat_last;
static unsigned sf2_stat_calls, sf2_stat_us, sf2_stat_max;

/* written here, inside the mix (the driver locked); logged by the audio
   thread after it (driver_ps3.cpp), so the log's file I/O never runs with
   the driver locked */
static char         sf2_stat_msg[448];
static volatile int sf2_stat_ready;

extern "C" const char *SF2_PS3_TakeStatsMessage(void)
{
    if (!sf2_stat_ready)
        return nullptr;

    sf2_stat_ready = 0;
    return sf2_stat_msg;
}

static void SF2_Stats(unsigned us)
{
    unsigned long long const now = PS3_Micros();

    sf2_stat_calls++;
    sf2_stat_us += us;
    if (us > sf2_stat_max)
        sf2_stat_max = us;

    if (sf2_stat_last == 0)
        sf2_stat_last = now;

    if (now - sf2_stat_last < 10000000ull)
        return;

#ifdef PS3_SF2_SPU
    unsigned jobs, voices, misses, waits, spu_us, spu_max;
    PS3_SF2SPU_TakeStats(&jobs, &voices, &misses, &waits, &spu_us, &spu_max);
    snprintf(sf2_stat_msg, sizeof(sf2_stat_msg), "[sf2] %u buffers, %u us avg, %u us max (%.1f%% of real time); SPU: %u jobs, %.1f voices, %u lines (%u waited), %u us avg, %u us max; "
            "MIDI %d bpm x %d ppq = %d ticks/s (%u tempo changes), %d voices allocated",
            sf2_stat_calls, sf2_stat_us / sf2_stat_calls, sf2_stat_max,
            sf2_stat_us * 100.0 / (double)(now - sf2_stat_last), jobs, jobs ? voices / (double)jobs : 0.0, misses, waits,
            jobs ? spu_us / jobs : 0, spu_max, sf2_tempo_bpm, sf2_tempo_division, MV_MIDIRenderTempo, sf2_tempo_changes,
            sf2_synth ? sf2_synth->voiceNum : 0);
    sf2_tempo_changes = 0;
#else
    snprintf(sf2_stat_msg, sizeof(sf2_stat_msg), "[sf2] %u buffers, %u us avg, %u us max (%.1f%% of real time, PPU)",
            sf2_stat_calls, sf2_stat_us / sf2_stat_calls, sf2_stat_max,
            sf2_stat_us * 100.0 / (double)(now - sf2_stat_last));
#endif
    sf2_stat_ready = 1;
    sf2_stat_last = now;
    sf2_stat_calls = sf2_stat_us = sf2_stat_max = 0;
}
#else
#define SF2_RENDER(f, buf, n) tsf_render_float(f, buf, n, 0)
#endif
static int  SF2_Volume = MIDI_MaxVolume;
static int  ErrorCode  = SF2_Ok;

static inline int SF2_SetError(int const status) { return (ErrorCode = status); }

int SF2Drv_GetError(void) { return ErrorCode; }

static void SF2_NoteOff(int channel, int key, int velocity)
{
    UNREFERENCED_PARAMETER(velocity);
    tsf_channel_note_off(sf2_synth, channel, key);
}

static void SF2_NoteOn(int channel, int key, int velocity)        { tsf_channel_note_on(sf2_synth, channel, key, velocity * (1.f / 127.f)); }
static void SF2_ControlChange(int channel, int number, int value) { tsf_channel_midi_control(sf2_synth, channel, number, value); }
static void SF2_ProgramChange(int channel, int program)           { tsf_channel_set_presetnumber(sf2_synth, channel, program, channel == 9); }
static void SF2_SetPitchBend(int channel, int lsb, int msb)       { tsf_channel_set_pitchwheel(sf2_synth, channel, (msb << 7) | lsb); }
static void SF2_SetVolume(int volume)                             { SF2_Volume = clamp(volume, 0, MIDI_MaxVolume); }

const char *SF2Drv_ErrorString(int ErrorNumber)
{
    switch (ErrorNumber)
    {
        case SF2_Error:     return SF2Drv_ErrorString(ErrorCode);
        case SF2_Ok:        return "SF2 ok.";
        case SF2_BankError: return "SF2 bank error.";
        default:            return "Unknown SF2 error.";
    }
}

static int sf2_stream_read(void *handle, void *ptr, unsigned int size) { return kread(*(buildvfs_kfd *)handle, ptr, size); };
static int sf2_stream_skip(void *handle, unsigned int size)            { return !klseek(*(buildvfs_kfd *)handle, size, SEEK_CUR); };

static int SF2_LoadBank(char const *const filename)
{
    buildvfs_kfd sf2_kfd    = kopen4loadfrommod(filename, 0);
    tsf_stream   sf2_stream = { &sf2_kfd, &sf2_stream_read, &sf2_stream_skip };

    if (sf2_kfd != buildvfs_kfd_invalid)
    {
#ifdef __PPU__
        PS3_SF2_Lock();
        tsf_close(sf2_synth);
        sf2_synth = nullptr;
        PS3_SF2_Unlock();
        size_t const heap0 = mallinfo().uordblks;
#else
        tsf_close(sf2_synth);
#endif
        sf2_synth = tsf_load(&sf2_stream);
        kclose(sf2_kfd);

        if (sf2_synth)
        {
#ifdef __PPU__
            PS3_Log("[sf2] \"%s\" takes %u KB of memory", filename, (unsigned)((mallinfo().uordblks - heap0) / 1024));
#endif
            VLOG_F(LOG_ASS, "Loaded \"%s\"", filename);
            return SF2_Ok;
        }
    }

    LOG_F(ERROR, "Unable to load \"%s\"!", filename);
    return SF2_Error;
}

int SF2Drv_MIDI_Init(midifuncs* const funcs)
{
#ifdef __PPU__
    PS3_SF2_LockInit();
#endif
    SF2Drv_MIDI_Shutdown();

    auto filename = SF2_BankFile;

    if (!filename[0])
    {
        fnlist_t fnl = FNLIST_INITIALIZER;

        // default to the first .sf2 we find if cvar mus_sf2_bank is unset
        fnlist_getnames(&fnl, g_modDir, "*.sf2", 0, 0);

        if (!fnl.findfiles)
            fnlist_getnames(&fnl, "/", "*.sf2", 0, 0);

        if (fnl.findfiles)
            filename = Xstrdup(fnl.findfiles->name);

        fnlist_clearnames(&fnl);

        if (!filename[0])
        {
            LOG_F(WARNING, "No .sf2 data found!");
            return SF2_SetError(SF2_BankError);
        }
    }

    int const loaded = SF2_LoadBank(filename);

    if (filename != SF2_BankFile)
        Xfree(filename);

    if (loaded != SF2_Ok || !sf2_synth)
        return SF2_SetError(SF2_BankError);

    Bmemset(funcs, 0, sizeof(midifuncs));

    funcs->NoteOff           = SF2_NoteOff;
    funcs->NoteOn            = SF2_NoteOn;
    funcs->PolyAftertouch    = nullptr;
    funcs->ControlChange     = SF2_ControlChange;
    funcs->ProgramChange     = SF2_ProgramChange;
    funcs->ChannelAftertouch = nullptr;
    funcs->PitchBend         = SF2_SetPitchBend;
    funcs->SetVolume         = SF2_SetVolume;

    SF2_Volume = MIDI_MaxVolume;

    return SF2_Ok;
}

#ifdef __PPU__
static void SF2_SetActive(int active)
{
    PS3_SF2_Lock();
    sf2_active = active;
    sf2_staged = 0;
    PS3_SF2_Unlock();
}
#endif

void SF2Drv_MIDI_HaltPlayback(void)
{
    MV_UnhookMusicRoutine();
#ifdef __PPU__
    SF2_SetActive(0);
#endif
}

void SF2Drv_MIDI_Shutdown(void)
{
    SF2Drv_MIDI_HaltPlayback();

#ifdef __PPU__
    PS3_SF2_Lock();
#endif
    tsf_close(sf2_synth);
    sf2_synth = nullptr;
    ErrorCode = SF2_Ok;
#ifdef __PPU__
    PS3_SF2_Unlock();
#endif
}

int SF2Drv_MIDI_StartPlayback(void)
{
    SF2Drv_MIDI_HaltPlayback();

    tsf_set_output(sf2_synth, MV_Channels == 1 ? TSF_MONO : TSF_STEREO_INTERLEAVED,
#ifdef __PPU__
                   TSF_INTERP_LINEAR, /* PS3: cubic costs too much for what it adds */
#else
                   TSF_INTERP_CUBIC,
#endif
                   MV_MixRate, 0);
#ifdef __PPU__
    // PS3: at most as many voices as the SPU takes in one job (the rest
    // rendered on the PPU, slowly); past that tsf takes the voice furthest
    // into its release. Without a limit tsf adds voices for as long as
    // notes keep coming without ending (96 and more, seen in the autotest).
    tsf_set_max_voices(sf2_synth, 96);
#endif
    tsf_channel_set_bank_preset(sf2_synth, 9, 128, 0);
    tsf_reset(sf2_synth);

    for (int channel = 0; channel < 16; channel++)
        SF2_ProgramChange(channel, 0);

#ifdef __PPU__
    SF2_SetActive(1);
#endif
    MV_HookMusicRoutine(SF2Drv_MIDI_Service);

    return MIDI_Ok;
}

void SF2Drv_MIDI_SetTempo(int const tempo, int const division)
{
#ifdef __PPU__
    PS3_SF2_Lock();
    sf2_tempo_bpm = tempo;
    sf2_tempo_division = division;
    sf2_tempo_changes++;
#endif
    MV_MIDIRenderTempo = tempo * division / 60;
    MV_MIDIRenderTimer = 0;
#ifdef __PPU__
    PS3_SF2_Unlock();
#endif
}

/* the next MV_MIXBUFFERSIZE frames of music, MIDI events included */
static void SF2_RenderMusic(int16_t *buffer16)
{
    static float fbuf[MV_MIXBUFFERSIZE * 2];
    float const  fvolume = SF2_Volume * (32768.f / MIDI_MaxVolume);
#ifdef __PPU__
    unsigned long long const t0 = PS3_Micros();
#endif

    for (int i = 0; i < MV_MIXBUFFERSIZE;)
    {
        while (MV_MIDIRenderTimer >= MV_MixRate)
        {
            if (MV_MIDIRenderTempo >= 0)
                MIDI_ServiceRoutine();
            MV_MIDIRenderTimer -= MV_MixRate;
        }

        int samples = MV_MIDIRenderTempo > 0 ? (MV_MixRate - MV_MIDIRenderTimer + MV_MIDIRenderTempo - 1) / MV_MIDIRenderTempo : MV_MIXBUFFERSIZE;
        samples     = min(samples, MV_MIXBUFFERSIZE - i);
        SF2_RENDER(sf2_synth, fbuf, samples);

        int const nsamples = samples * MV_Channels;

        for (int j = 0; j < nsamples; j++)
            *buffer16++ = clamp((fbuf[j] * fvolume), INT16_MIN, INT16_MAX);

        if (MV_MIDIRenderTempo >= 0)
            MV_MIDIRenderTimer += MV_MIDIRenderTempo * samples;

        i += samples;
    }

#ifdef __PPU__
    SF2_Stats((unsigned)(PS3_Micros() - t0));
#endif
}

#ifdef __PPU__
/*
 * PS3: the music renders ahead, outside the PCM driver's lock.
 *
 * MultiVoc mixes with the driver locked (as SDL does), and every sound
 * the game starts or moves (MV_BeginService) waits for that lock: with
 * the synth inside the mix, a frame could wait milliseconds for it. So
 * the audio thread renders the next buffer of music first, holding only
 * the synth's own lock (SF2_PS3_Prerender, from driver_ps3.cpp), and the
 * mix just copies it in (SF2Drv_MIDI_Service), right after: no added
 * latency.
 *
 * The synth's lock (recursive) covers the synth and the MIDI player's
 * state: the prerender, the driver's MIDI_Lock (midi.cpp, the game
 * thread), start/halt/shutdown. Never taken with the driver's lock
 * already held, except as a try (the mix's fallback), so the two never
 * wait for each other in opposite orders.
 */

void SF2Drv_MIDI_Lock(void)   { PS3_SF2_Lock(); }
void SF2Drv_MIDI_Unlock(void) { PS3_SF2_Unlock(); }

extern "C" void SF2_PS3_Prerender(void)
{
    if (sf2_staged)
        return;

    PS3_SF2_Lock();

    if (sf2_active && sf2_synth && !sf2_staged)
    {
        SF2_RenderMusic(sf2_stage);
        sf2_staged = 1;
    }

    PS3_SF2_Unlock();
}

void SF2Drv_MIDI_Service(void)
{
    size_t const bytes = MV_MIXBUFFERSIZE * MV_Channels * sizeof(int16_t);

    if (sf2_staged)
    {
        Bmemcpy(MV_MusicBuffer, sf2_stage, bytes);
        sf2_staged = 0;
    }
    else if (PS3_SF2_TryLock())
    {
        /* not rendered ahead (the mix and the hardware blocks out of
           step): here, as before */
        if (sf2_active && sf2_synth)
            SF2_RenderMusic((int16_t *)MV_MusicBuffer);
        else
            Bmemset(MV_MusicBuffer, 0, bytes);
        PS3_SF2_Unlock();
    }
    else
        Bmemset(MV_MusicBuffer, 0, bytes);   /* the game holds the synth: one buffer of silence */
}
#else
void SF2Drv_MIDI_Service(void) { SF2_RenderMusic((int16_t *)MV_MusicBuffer); }
#endif
