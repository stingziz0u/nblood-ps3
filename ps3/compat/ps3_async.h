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
 * libasync (Async++) needs std::thread, std::mutex and
 * std::condition_variable, which the PPU's libstdc++ doesn't have (gcc
 * built with the "single" thread model). The audiolib only uses it to
 * open Ogg Vorbis and module (XMP) voices in the background: the work is
 * the same done right away, as EDuke32 did before it had libasync.
 *
 * async::spawn runs the function at once and gives back a task that is
 * already done; MultiVoc picks the result up from it (valid / ready /
 * get / wait) exactly as from a real one. Used by libasync_config.h on
 * the PS3 instead of libasync.h.
 *
 * The audio thread reads voice->task while the game thread assigns it
 * (the voice is already playing by then: the function itself started
 * it). "Done" and the result are one 64 bit word, which the PPU writes
 * and reads in one go, so the audio thread can never see "done" with
 * the result of an earlier voice.
 *
 * =======================================================================
 */

#ifndef PS3_ASYNC_H
#define PS3_ASYNC_H

#include <stdint.h>
#include <type_traits>

namespace async
{
	template <class T>
	class task
	{
		static_assert(std::is_integral<T>::value && sizeof(T) <= 4,
				"the PS3 async shim only carries int results");

	public:
		task() : word(0) { }
		explicit task(T v) : word(DONE | (uint32_t)v) { }

		task(const task &o) : word(o.word) { }
		task &operator=(const task &o)
		{
			word = o.word;
			return *this;
		}

		bool valid() const { return (word & DONE) != 0; }
		bool ready() const { return true; }
		void wait() const { }

		/* like a std::future: the result is taken once */
		T get()
		{
			uint64_t w = word;

			word = 0;
			return (T)(int32_t)(uint32_t)w;
		}

	private:
		static const uint64_t DONE = 1ull << 32;
		volatile uint64_t word;
	};

	template <class F>
	auto spawn(F &&f) -> task<decltype(f())>
	{
		return task<decltype(f())>(f());
	}
}

#endif
