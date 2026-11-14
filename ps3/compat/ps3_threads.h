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
 * The PPU's gcc 7.2 is built with the "single" thread model: its
 * libstdc++ has no std::mutex, std::recursive_mutex nor std::thread.
 * The logger (loguru.cpp) needs a recursive mutex, the thread id and,
 * only when a flush interval is set (never here), a thread. These are
 * the few pieces it uses, on top of lv2's mutexes and PPU threads.
 *
 * Included only in loguru.cpp (ps3/Makefile, -include).
 *
 * =======================================================================
 */

#ifndef PS3_THREADS_H
#define PS3_THREADS_H

#ifdef __cplusplus

#include <chrono>
#include <cstring>
#include <functional>
#include <mutex>

#include <ppu-types.h>
#include <sys/mutex.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <lv2/thread.h>

namespace std
{
	class recursive_mutex
	{
	public:
		recursive_mutex()
		{
			sys_mutex_attr_t attr;

			memset(&attr, 0, sizeof(attr));
			attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
			attr.attr_recursive = SYS_MUTEX_ATTR_RECURSIVE;
			attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
			attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
			strcpy(attr.name, "logmtx");
			ok = (sysMutexCreate(&m, &attr) == 0);
		}

		/* never destroyed: the logger's mutex is a static that other
		   statics' destructors may still lock at exit */
		~recursive_mutex() { }

		recursive_mutex(const recursive_mutex &) = delete;
		recursive_mutex &operator=(const recursive_mutex &) = delete;

		void lock() { if (ok) sysMutexLock(m, 0); }
		bool try_lock() { return ok ? sysMutexTryLock(m) == 0 : true; }
		void unlock() { if (ok) sysMutexUnlock(m); }

	private:
		sys_mutex_t m;
		bool ok;
	};

	class thread
	{
	public:
		class id
		{
		public:
			id() : v(0) { }
			explicit id(sys_ppu_thread_t t) : v(t) { }
			bool operator==(const id &o) const { return v == o.v; }
			bool operator!=(const id &o) const { return v != o.v; }
			sys_ppu_thread_t v;
		};

		thread() : tid(0), started(false) { }

		template <class F>
		explicit thread(F f) : tid(0), started(false)
		{
			auto *fn = new std::function<void()>(f);

			if (sysThreadCreate(&tid, Entry, fn, 1500, 64 * 1024,
					THREAD_JOINABLE, (char *)"stdthread") == 0)
			{
				started = true;
			}
			else
			{
				delete fn;
			}
		}

		thread(const thread &) = delete;
		thread &operator=(const thread &) = delete;

		bool joinable() const { return started; }

		void join()
		{
			u64 code;

			if (started)
			{
				sysThreadJoin(tid, &code);
				started = false;
			}
		}

		void detach() { started = false; }

		id get_id() const { return id(tid); }

	private:
		static void Entry(void *arg)
		{
			auto *fn = static_cast<std::function<void()> *>(arg);

			(*fn)();
			delete fn;
			sysThreadExit(0);
		}

		sys_ppu_thread_t tid;
		bool started;
	};

	template <>
	struct hash<thread::id>
	{
		size_t operator()(const thread::id &i) const { return (size_t)i.v; }
	};

	namespace this_thread
	{
		inline thread::id get_id()
		{
			sys_ppu_thread_t t = 0;

			sysThreadGetId(&t);
			return thread::id(t);
		}

		template <class Rep, class Period>
		inline void sleep_for(const chrono::duration<Rep, Period> &d)
		{
			long long us = chrono::duration_cast<chrono::microseconds>(d).count();

			if (us > 0)
			{
				sysUsleep((u32)us);
			}
		}

		inline void yield() { sysThreadYield(); }
	}
}

#endif /* __cplusplus */

#endif
