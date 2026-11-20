/*
 * Copyright (C) 2026 the EDuke32-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * TinySoundFont (tsf.h) by Bernhard Schelling, MIT license: the voice
 * rendering below is tsf_voice_render and its helpers, with the voice's
 * region carried in the voice and the font's samples read through a
 * cache in local store.
 *
 * =======================================================================
 *
 * The SoundFont synth's voices on an SPU (sf2spu_wire.h has the protocol).
 *
 * One SPU thread, started once, waits on signal notification 1. Every
 * signal is a job: the job block (its EA came as the thread's argument),
 * the voices, the samples they read; the voices render into a buffer in
 * local store, the buffer and the voices go back, and a user event on
 * SF2SPU_PORT wakes the PPU's audio thread.
 *
 * The font's samples (16 bit, in main memory) go through a direct-mapped
 * cache of 128 byte lines; before rendering, the lines each voice is
 * going to read in this chunk are asked for all at once, so a job waits
 * for main memory about once, not once per line.
 *
 * =======================================================================
 */

#include <stdint.h>
#include <string.h>
#include <math.h>
#include <spu_intrinsics.h>
#include <spu_mfcio.h>

#include "sf2spu_wire.h"

/* tsf.h's names, so the code below stays tsf's */
#define TSF_BOOL char
#define TSF_TRUE 1
#define TSF_FALSE 0
#define TSF_FASTRELEASETIME 0.01f
#define TSF_RENDER_EFFECTSAMPLEBLOCK effect_block
#define TSF_PI 3.14159265358979323846264338327950288
#define TSF_POWF powf
#define TSF_EXPF expf
#define TSF_LOG  logf

enum { TSF_SEGMENT_NONE, TSF_SEGMENT_DELAY, TSF_SEGMENT_ATTACK, TSF_SEGMENT_HOLD, TSF_SEGMENT_DECAY, TSF_SEGMENT_SUSTAIN, TSF_SEGMENT_RELEASE, TSF_SEGMENT_DONE };
enum { TSF_INTERP_NEAREST, TSF_INTERP_LINEAR, TSF_INTERP_CUBIC };

#define tsf_voice_envelope sf2w_voice_envelope
#define tsf_voice_lowpass  sf2w_voice_lowpass
#define tsf_voice_lfo      sf2w_voice_lfo

#define TAG_IO   1
#define TAG_PRE  2
#define TAG_SYNC 3

/* ------------------------------------------------------------------------
   The sample cache
   ------------------------------------------------------------------------ */

#define LINE_SAMPLES 64                 /* 128 bytes of 16 bit samples */
#define CACHE_LINES 1024                /* 128 KB */
#define PREFETCH_MAX_LINES 48           /* per voice and range */

static int16_t  cache[CACHE_LINES][LINE_SAMPLES] __attribute__((aligned(128)));
static uint32_t cache_tag[CACHE_LINES];     /* EA >> 7 of the line, or ~0 */
static uint32_t cache_round[CACHE_LINES];   /* asked for in this job's prefetch */
static uint32_t round_no;
static uint64_t font_ea;
static uint32_t misses;
static uint32_t waits;
static int      effect_block = 64;

static void cache_flush(void)
{
    for (int i = 0; i < CACHE_LINES; i++)
        cache_tag[i] = ~0u, cache_round[i] = 0;
}

static void __attribute__((noinline)) cache_fetch(uint32_t slot, uint32_t line)
{
    mfc_get(cache[slot], (uint64_t)line << 7, 128, TAG_SYNC, 0, 0);
    mfc_write_tag_mask(1 << TAG_SYNC);
    mfc_read_tag_status_all();
    cache_tag[slot] = line;
    misses++;
    waits++;
}

/* sample idx of the font, as tsf_sample (16 bit) */
static inline int S(uint32_t idx)
{
    uint64_t const ea = font_ea + ((uint64_t)idx << 1);
    uint32_t const line = (uint32_t)(ea >> 7);
    uint32_t const slot = line & (CACHE_LINES - 1);

    if (__builtin_expect(cache_tag[slot] != line, 0))
        cache_fetch(slot, line);

    return cache[slot][((uint32_t)ea & 127) >> 1];
}

/* asks for the lines of samples first..last (no waiting) */
static void prefetch(uint32_t first, uint32_t last)
{
    if (last < first)
        return;

    uint32_t line = (uint32_t)((font_ea + ((uint64_t)first << 1)) >> 7);
    uint32_t const end = (uint32_t)((font_ea + ((uint64_t)last << 1)) >> 7);
    int budget = PREFETCH_MAX_LINES;

    for (; line <= end && budget > 0; line++, budget--)
    {
        uint32_t const slot = line & (CACHE_LINES - 1);

        /* there already, or this job asked for another line here: the
           second one comes on demand, after the prefetch has landed */
        if (cache_tag[slot] == line || cache_round[slot] == round_no)
            continue;

        mfc_get(cache[slot], (uint64_t)line << 7, 128, TAG_PRE, 0, 0);
        cache_tag[slot] = line;
        cache_round[slot] = round_no;
        misses++;
    }
}

/* ------------------------------------------------------------------------
   tsf's voice rendering
   ------------------------------------------------------------------------ */

static float tsf_timecents2Secsf(float timecents) { return TSF_POWF(2.0f, timecents / 1200.0f); }
static float tsf_cents2Hertz(float cents) { return 8.176f * TSF_POWF(2.0f, cents / 1200.0f); }
static float tsf_decibelsToGain(float db) { return (db > -100.f ? TSF_POWF(10.0f, db * 0.05f) : 0); }

static int tsf_voice_envelope_release_samples(struct tsf_voice_envelope* e, float outSampleRate)
{
	return (int)((e->parameters.release <= 0 ? TSF_FASTRELEASETIME : e->parameters.release) * outSampleRate);
}

static void tsf_voice_envelope_nextsegment(struct tsf_voice_envelope* e, short active_segment, float outSampleRate)
{
	switch (active_segment)
	{
		case TSF_SEGMENT_NONE:
			e->samplesUntilNextSegment = (int)(e->parameters.delay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DELAY;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 0.0;
				e->slope = 0.0;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DELAY:
			e->samplesUntilNextSegment = (int)(e->parameters.attack * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				if (!e->isAmpEnv)
				{
					//mod env attack duration scales with velocity (velocity of 1 is full duration, max velocity is 0.125 times duration)
					e->samplesUntilNextSegment = (int)(e->parameters.attack * ((145 - e->midiVelocity) / 144.0f) * outSampleRate);
				}
				e->segment = TSF_SEGMENT_ATTACK;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 0.0f;
				e->slope = 1.0f / e->samplesUntilNextSegment;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_ATTACK:
			e->samplesUntilNextSegment = (int)(e->parameters.hold * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_HOLD;
				e->segmentIsExponential = TSF_FALSE;
				e->level = 1.0f;
				e->slope = 0.0f;
				return;
			}
			/* fall through */
		case TSF_SEGMENT_HOLD:
			e->samplesUntilNextSegment = (int)(e->parameters.decay * outSampleRate);
			if (e->samplesUntilNextSegment > 0)
			{
				e->segment = TSF_SEGMENT_DECAY;
				e->level = 1.0f;
				if (e->isAmpEnv)
				{
					// I don't truly understand this; just following what LinuxSampler does.
					float mysterySlope = -9.226f / e->samplesUntilNextSegment;
					e->slope = TSF_EXPF(mysterySlope);
					e->segmentIsExponential = TSF_TRUE;
					if (e->parameters.sustain > 0.0f)
					{
						// Again, this is following LinuxSampler's example, which is similar to
						// SF2-style decay, where "decay" specifies the time it would take to
						// get to zero, not to the sustain level.  The SFZ spec is not that
						// specific about what "decay" means, so perhaps it's really supposed
						// to specify the time to reach the sustain level.
						e->samplesUntilNextSegment = (int)(TSF_LOG(e->parameters.sustain) / mysterySlope);
					}
				}
				else
				{
					e->slope = -1.0f / e->samplesUntilNextSegment;
					e->samplesUntilNextSegment = (int)(e->parameters.decay * (1.0f - e->parameters.sustain) * outSampleRate);
					e->segmentIsExponential = TSF_FALSE;
				}
				return;
			}
			/* fall through */
		case TSF_SEGMENT_DECAY:
			e->segment = TSF_SEGMENT_SUSTAIN;
			e->level = e->parameters.sustain;
			e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFFF;
			e->segmentIsExponential = TSF_FALSE;
			return;
		case TSF_SEGMENT_SUSTAIN:
			e->segment = TSF_SEGMENT_RELEASE;
			e->samplesUntilNextSegment = tsf_voice_envelope_release_samples(e, outSampleRate);
			if (e->isAmpEnv)
			{
				// I don't truly understand this; just following what LinuxSampler does.
				float mysterySlope = -9.226f / e->samplesUntilNextSegment;
				e->slope = TSF_EXPF(mysterySlope);
				e->segmentIsExponential = TSF_TRUE;
			}
			else
			{
				e->slope = -e->level / e->samplesUntilNextSegment;
				e->segmentIsExponential = TSF_FALSE;
			}
			return;
		case TSF_SEGMENT_RELEASE:
		default:
			e->segment = TSF_SEGMENT_DONE;
			e->segmentIsExponential = TSF_FALSE;
			e->level = e->slope = 0.0f;
			e->samplesUntilNextSegment = 0x7FFFFFF;
	}
}


static void tsf_voice_envelope_process(struct tsf_voice_envelope* e, int numSamples, float outSampleRate)
{
	if (e->slope)
	{
		if (e->segmentIsExponential) e->level *= TSF_POWF(e->slope, (float)numSamples);
		else e->level += (e->slope * numSamples);
	}
	if ((e->samplesUntilNextSegment -= numSamples) <= 0)
		tsf_voice_envelope_nextsegment(e, e->segment, outSampleRate);
}

static void tsf_voice_lfo_process(struct tsf_voice_lfo* e, int blockSamples)
{
	if (e->samplesUntil > blockSamples) { e->samplesUntil -= blockSamples; return; }
	e->level += e->delta * blockSamples;
	if      (e->level >  1.0f) { e->delta = -e->delta; e->level =  2.0f - e->level; }
	else if (e->level < -1.0f) { e->delta = -e->delta; e->level = -2.0f - e->level; }
}

#define SAMPLE_SCALE (1.0f / 32767.0f)

/*
 * tsf_voice_render for TSF_STEREO_INTERLEAVED (stereo) and TSF_MONO, made
 * for the SPU: the SPU has no double compare or double -> int conversion
 * (software, slow) and libm's double functions are slower still, so
 *  - the sample position is 32.32 fixed point (tsf: a double; the same
 *    positions to well under a millionth of a sample, the same loop and
 *    end tests);
 *  - the pitch, gain and filter math per effect block is float;
 *  - the low-pass filter runs in float (tsf: double).
 * Same envelopes, LFOs and order of operations as tsf.
 */
static void render_voice(sf2spu_voice* v, float* outputBuffer, int numSamples, int stereo, int interpolatemode, float outSampleRate)
{
	float* out = outputBuffer;

	int const updateModEnv = (v->modEnvToPitch || v->modEnvToFilterFc);
	int const updateModLFO = (v->modlfo.delta && (v->modLfoToPitch || v->modLfoToFilterFc || v->modLfoToVolume));
	int const updateVibLFO = (v->viblfo.delta && (v->vibLfoToPitch));
	int const isLooping    = (v->loopStart < v->loopEnd);
	uint32_t const loopStart = v->loopStart, loopEnd = v->loopEnd, regionEnd = v->regionEnd;
	uint32_t const loopLen = loopEnd - loopStart + 1;

	double const startPos = v->sourceSamplePosition;
	uint32_t ipos = (uint32_t)startPos;
	uint32_t frac = (uint32_t)((startPos - (double)ipos) * 4294967296.0);

	int const dynamicLowpass = (v->modLfoToFilterFc || v->modEnvToFilterFc);
	int lpActive = v->lowpass.active;
	float lpQInv = (float)v->lowpass.QInv;
	float a0 = (float)v->lowpass.a0, a1 = (float)v->lowpass.a1, b1 = (float)v->lowpass.b1, b2 = (float)v->lowpass.b2;
	float z1 = (float)v->lowpass.z1, z2 = (float)v->lowpass.z2;

	int const dynamicPitchRatio = (v->modLfoToPitch || v->modEnvToPitch || v->vibLfoToPitch);
	float const pitchInput = (float)v->pitchInputTimecents, pitchOutput = (float)v->pitchOutputFactor;
	float pitchRatio = 0;

	int const dynamicGain = (v->modLfoToVolume != 0);
	float noteGain = 0;

	if (!dynamicPitchRatio) pitchRatio = tsf_timecents2Secsf(pitchInput) * pitchOutput;
	if (!dynamicGain) noteGain = tsf_decibelsToGain(v->noteGainDB);

	while (numSamples)
	{
		int blockSamples = (numSamples > TSF_RENDER_EFFECTSAMPLEBLOCK ? TSF_RENDER_EFFECTSAMPLEBLOCK : numSamples);
		numSamples -= blockSamples;

		if (dynamicLowpass)
		{
			float fres = (float)v->initialFilterFc + v->modlfo.level * (float)v->modLfoToFilterFc + v->modenv.level * (float)v->modEnvToFilterFc;
			float lowpassFc = (fres <= 13500 ? tsf_cents2Hertz(fres) / outSampleRate : 1.0f);
			lpActive = (lowpassFc < 0.499f);
			if (lpActive)
			{
				float K = tanf((float)TSF_PI * lowpassFc), KK = K * K;
				float norm = 1.0f / (1.0f + K * lpQInv + KK);
				a0 = KK * norm;
				a1 = 2.0f * a0;
				b1 = 2.0f * (KK - 1.0f) * norm;
				b2 = (1.0f - K * lpQInv + KK) * norm;
			}
		}

		if (dynamicPitchRatio)
			pitchRatio = tsf_timecents2Secsf(pitchInput + (v->modlfo.level * (float)v->modLfoToPitch + v->viblfo.level * (float)v->vibLfoToPitch + v->modenv.level * (float)v->modEnvToPitch)) * pitchOutput;

		if (dynamicGain)
			noteGain = tsf_decibelsToGain(v->noteGainDB + (v->modlfo.level * ((float)v->modLfoToVolume * 0.1f)));

		float const gainMono = noteGain * v->ampenv.level;
		float const gainLeft = gainMono * v->panFactorLeft, gainRight = gainMono * v->panFactorRight;

		// Update EG.
		tsf_voice_envelope_process(&v->ampenv, blockSamples, outSampleRate);
		if (updateModEnv) tsf_voice_envelope_process(&v->modenv, blockSamples, outSampleRate);

		// Update LFOs.
		if (updateModLFO) tsf_voice_lfo_process(&v->modlfo, blockSamples);
		if (updateVibLFO) tsf_voice_lfo_process(&v->viblfo, blockSamples);

		/* the step in 32.32 */
		uint32_t const istep = (uint32_t)pitchRatio;
		uint32_t const fstep = (uint32_t)((pitchRatio - (float)istep) * 4294967296.0f);

		while (blockSamples-- && ipos < regionEnd)
		{
			float val;

			if (interpolatemode == TSF_INTERP_NEAREST)
				val = (float)S(ipos) * SAMPLE_SCALE;
			else
			{
				uint32_t const p2 = isLooping ? (ipos >= loopEnd ? loopStart : ipos + 1) : (ipos + 1 == regionEnd ? ipos : ipos + 1);
				float const alpha = (float)(frac >> 8) * (1.0f / 16777216.0f);

				if (interpolatemode == TSF_INTERP_CUBIC)
				{
					uint32_t const p0 = (ipos == loopStart && isLooping ? loopEnd : ipos == 0 ? 0 : ipos - 1);
					uint32_t const p3 = isLooping ? (p2 >= loopEnd ? loopStart : p2 + 1) : (p2 + 1 == regionEnd ? p2 : p2 + 1);
					float const pa = (float)S(p0), pb = (float)S(ipos), pc = (float)S(p2), pd = (float)S(p3);
					float const alpha2 = alpha * alpha;
					float const c0 = -0.5f * pa + 1.5f * pb - 1.5f * pc + 0.5f * pd;
					float const c1 = pa - 2.5f * pb + 2.f * pc - 0.5f * pd;
					float const c2 = -0.5f * pa + 0.5f * pc;
					val = (c0 * alpha * alpha2 + c1 * alpha2 + c2 * alpha + pb) * SAMPLE_SCALE;
				}
				else
				{
					float const sa = (float)S(ipos), sb = (float)S(p2);
					val = (sa + (sb - sa) * alpha) * SAMPLE_SCALE;
				}
			}

			// Low-pass filter.
			if (lpActive)
			{
				float const in = val;
				val = in * a0 + z1;
				z1 = in * a1 + z2 - b1 * val;
				z2 = in * a0 - b2 * val;
			}

			if (stereo)
			{
				out[0] += val * gainLeft;
				out[1] += val * gainRight;
				out += 2;
			}
			else
				*out++ += val * gainMono;

			// Next sample.
			uint32_t const f2 = frac + fstep;
			ipos += istep + (f2 < frac);
			frac = f2;
			if (isLooping && ipos > loopEnd) ipos -= loopLen;
		}

		if (ipos >= regionEnd || v->ampenv.segment == TSF_SEGMENT_DONE)
		{
			v->ended = 1;           /* tsf_voice_kill */
			return;
		}
	}

	v->sourceSamplePosition = (double)ipos + (double)frac * (1.0 / 4294967296.0);
	if (lpActive || dynamicLowpass)
	{
		v->lowpass.active = (char)lpActive;
		v->lowpass.a0 = a0, v->lowpass.a1 = a1, v->lowpass.b1 = b1, v->lowpass.b2 = b2;
		v->lowpass.z1 = z1, v->lowpass.z2 = z2;
	}
}

/* the lines a voice reads in the next numSamples, about */
static void prefetch_voice(sf2spu_voice const* v, int numSamples)
{
	float ratio = tsf_timecents2Secsf((float)v->pitchInputTimecents) * (float)v->pitchOutputFactor;
	uint32_t const p0 = (uint32_t)v->sourceSamplePosition;

	if (v->modLfoToPitch || v->modEnvToPitch || v->vibLfoToPitch)
		ratio *= 1.25f;         /* some room for the modulation */

	float span = ratio * (float)numSamples + 3.0f;
	if (!(span < (float)(PREFETCH_MAX_LINES * LINE_SAMPLES)))
		span = (float)(PREFETCH_MAX_LINES * LINE_SAMPLES);

	if (p0 >= v->regionEnd)
		return;

	uint32_t const p1 = p0 + (uint32_t)span;

	if (v->loopStart < v->loopEnd && p1 > v->loopEnd && p0 <= v->loopEnd)
	{
		uint32_t const wrap = p1 - v->loopEnd;
		uint32_t last = v->loopStart + wrap;

		if (last > v->loopEnd)
			last = v->loopEnd;

		prefetch(p0 > 0 ? p0 - 1 : 0, v->loopEnd);
		prefetch(v->loopStart, last);
	}
	else
	{
		uint32_t last = p1;

		if (v->regionEnd && last >= v->regionEnd)
			last = v->regionEnd - 1;

		prefetch(p0 > 0 ? p0 - 1 : 0, last);
	}
}

/* ------------------------------------------------------------------------
   The job loop
   ------------------------------------------------------------------------ */

_Static_assert(__builtin_offsetof(sf2spu_job, done) % 16 == 0, "job.done must be 16-aligned");
_Static_assert(sizeof(sf2spu_voice) % 16 == 0, "voices must be 16 byte multiples");

static sf2spu_job   job __attribute__((aligned(128)));
static sf2spu_voice voices[SF2SPU_MAX_VOICES] __attribute__((aligned(128)));
static float        outbuf[SF2SPU_MAX_SAMPLES * 2 + 32] __attribute__((aligned(128)));

/* mfc_get/put of any size (16 byte multiples, 16 KB at a time) */
static void dma_large(void *ls, uint64_t ea, uint32_t size, int put)
{
	char *p = (char *)ls;

	while (size)
	{
		uint32_t const n = size > 16384 ? 16384 : size;

		if (put)
			mfc_put(p, ea, n, TAG_IO, 0, 0);
		else
			mfc_get(p, ea, n, TAG_IO, 0, 0);

		p += n, ea += n, size -= n;
	}
}

static void wait_tag(int tag)
{
	mfc_write_tag_mask(1 << tag);
	mfc_read_tag_status_all();
}

/* sys_spu_thread_send_event (libsputhread), here so the SPU side needs
   nothing but spu-gcc */
static void send_event(uint32_t port, uint32_t data0, uint32_t data1)
{
	spu_writech(SPU_WrOutMbox, data1);
	spu_writech(SPU_WrOutIntrMbox, (port << 24) | (data0 & 0x00FFFFFFu));
	(void)spu_readch(SPU_RdInMbox);
}

int main(uint64_t job_ea, uint64_t arg2, uint64_t arg3, uint64_t arg4)
{
	uint32_t seq = 0;

	/* the event port this SPU answers on (NBlood-PS3: one per SPU, the
	   PPU tells them apart by it) */
	uint32_t const port = arg2 ? (uint32_t)(arg2 & 0x3F) : SF2SPU_PORT;

	(void)arg3, (void)arg4;

	cache_flush();
	spu_writech(SPU_WrDec, 0xFFFFFFFFu);

	for (;;)
	{
		uint32_t const signal = spu_readch(SPU_RdSigNotify1);

		if (signal & SF2SPU_SIGNAL_QUIT)
			break;

		uint32_t const t0 = spu_readch(SPU_RdDec);

		mfc_get(&job, job_ea, sizeof(job), TAG_IO, 0, 0);
		wait_tag(TAG_IO);

		uint32_t nv = job.numVoices;
		int n = (int)job.numSamples;
		int const stereo = job.stereo ? 1 : 0;

		if (nv > SF2SPU_MAX_VOICES) nv = SF2SPU_MAX_VOICES;
		if (n > SF2SPU_MAX_SAMPLES) n = SF2SPU_MAX_SAMPLES;
		if (n < 0) n = 0;

		if (job.flush || font_ea != job.fontSamples)
			cache_flush();

		font_ea = job.fontSamples;
		effect_block = (int)job.effectBlock;
		if (effect_block < 1 || effect_block > SF2SPU_MAX_SAMPLES)
			effect_block = 64;
		misses = 0;
		waits = 0;
		if (++round_no == 0)
			round_no = 1;

		dma_large(voices, job.voices, nv * (uint32_t)sizeof(sf2spu_voice), 0);

		int const outfloats = n << stereo;
		uint32_t const outbytes = ((uint32_t)outfloats * 4 + 15) & ~15u;
		memset(outbuf, 0, outbytes);

		wait_tag(TAG_IO);

		for (uint32_t i = 0; i < nv; i++)
			prefetch_voice(&voices[i], n);

		wait_tag(TAG_PRE);

		for (uint32_t i = 0; i < nv; i++)
			render_voice(&voices[i], outbuf, n, stereo, (int)job.interp, job.outSampleRate);

		dma_large(voices, job.voices, nv * (uint32_t)sizeof(sf2spu_voice), 1);
		if (outbytes)
			dma_large(outbuf, job.out, outbytes, 1);

		job.done = ++seq;
		job.misses = misses;
		job.ticks = t0 - spu_readch(SPU_RdDec);
		job.waits = waits;

		/* done, misses, ticks, waits: 16 bytes, 16-aligned on both sides */
		mfc_put(&job.done, job_ea + __builtin_offsetof(sf2spu_job, done), 16, TAG_IO, 0, 0);
		wait_tag(TAG_IO);

		send_event(port, seq, misses);
	}

	spu_writech(SPU_WrOutMbox, 0);
	spu_stop(0x102);            /* sys_spu_thread_exit */
	return 0;
}
