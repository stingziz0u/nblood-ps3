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
 * What the PPU and the SPU share to render TinySoundFont's voices on an
 * SPU (ps3/spu/sf2spu.c, ps3/source/ps3_sf2spu.c, driver_sf2.cpp).
 *
 * The PPU keeps all of tsf (the font, the presets, the MIDI channels,
 * note on/off); for every chunk it renders it copies the voices that
 * play into an array of sf2spu_voice (no pointers: what the voice needs
 * of its region travels with it), the SPU renders them into the output
 * buffer and writes back what rendering changes (the sample position,
 * the envelopes, the LFOs, the filter, and whether the voice ended).
 *
 * Both sides are big-endian with the same sizes and alignments for char,
 * short, int, float and double, so the structs below lay out the same on
 * both; the tsf structs they mirror (tsf.h) are checked against them
 * where the PPU copies (driver_sf2.cpp).
 *
 * =======================================================================
 */

#ifndef SF2SPU_WIRE_H
#define SF2SPU_WIRE_H

#include <stdint.h>

#define SF2SPU_MAX_VOICES   96
#define SF2SPU_MAX_SAMPLES  256          /* MV_MIXBUFFERSIZE */
#define SF2SPU_SIGNAL_RUN   1u
#define SF2SPU_SIGNAL_QUIT  2u
#define SF2SPU_PORT         21           /* SPU thread user event port */

/* tsf_envelope, tsf_voice_envelope, tsf_voice_lowpass, tsf_voice_lfo */
typedef struct sf2w_envelope { float delay, attack, hold, decay, sustain, release, keynumToHold, keynumToDecay; } sf2w_envelope;
typedef struct sf2w_voice_envelope { float level, slope; int samplesUntilNextSegment; short segment, midiVelocity; sf2w_envelope parameters; char segmentIsExponential, isAmpEnv; } sf2w_voice_envelope;
typedef struct sf2w_voice_lowpass { double QInv, a0, a1, b1, b2, z1, z2; char active; } sf2w_voice_lowpass;
typedef struct sf2w_voice_lfo { int samplesUntil; float level, delta; } sf2w_voice_lfo;

typedef struct
{
    /* from the voice */
    double pitchInputTimecents, pitchOutputFactor;
    double sourceSamplePosition;                    /* in/out */
    float  noteGainDB, panFactorLeft, panFactorRight;
    uint32_t loopStart, loopEnd;

    /* from its region */
    uint32_t regionEnd;
    int32_t initialFilterFc, modEnvToPitch, modEnvToFilterFc;
    int32_t modLfoToFilterFc, modLfoToVolume, modLfoToPitch, vibLfoToPitch;

    int32_t index;                                  /* tsf voice number */
    int32_t ended;                                  /* out: tsf_voice_kill */

    sf2w_voice_envelope ampenv, modenv;             /* in/out */
    sf2w_voice_lowpass  lowpass;                    /* in/out */
    sf2w_voice_lfo      modlfo, viblfo;             /* in/out */
} __attribute__((aligned(16))) sf2spu_voice;

typedef struct
{
    uint64_t fontSamples;   /* EA of tsf's float samples */
    uint64_t voices;        /* EA of sf2spu_voice[numVoices], 128-aligned */
    uint64_t out;           /* EA of the output floats, 128-aligned */
    uint32_t numVoices;
    uint32_t numSamples;
    uint32_t stereo;        /* 1: TSF_STEREO_INTERLEAVED, 0: TSF_MONO */
    uint32_t interp;        /* TSFInterpolateMode */
    float    outSampleRate;
    uint32_t flush;         /* forget the sample cache (a new font) */
    uint32_t effectBlock;   /* TSF_RENDER_EFFECTSAMPLEBLOCK (SF2_EffectSampleBlockSize) */
    uint32_t pad0[3];

    /* back from the SPU (16 bytes, 16-aligned: one DMA) */
    uint32_t done;          /* job sequence number */
    uint32_t misses;        /* sample cache lines fetched */
    uint32_t ticks;         /* SPU decrementer ticks spent */
    uint32_t waits;         /* of the misses, how many the SPU waited for alone */
    uint32_t pad1[12];
} __attribute__((aligned(128))) sf2spu_job;

#endif
