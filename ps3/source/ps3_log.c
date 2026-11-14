/*
 * Copyright (C) 2026 the ROTT-PS3 and EDuke32-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * Log file. On the PS3 there is no console: a failure looks like a black
 * screen and a trip back to the XMB. The log is the only witness, so:
 *
 *  - it is off unless USRDIR/logs.txt exists (any content, even empty):
 *    nothing is written to the HDD otherwise, the engine's own log
 *    (eduke32.log / voidsw.log) and the fps lines included;
 *  - it is rotated on every boot (eduke32-ps3.log -> eduke32-ps3.old.log),
 *    so it never grows forever and the previous run survives a relaunch;
 *  - every write is flushed: after a crash, the last line is the one that
 *    says where it died;
 *  - writes are serialized: the audio thread logs too.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>

#include "ps3_platform.h"

#ifdef __PPU__
#include <sys/mutex.h>

static sys_mutex_t log_mutex;
static int log_mutex_ok;

static void
LogLockInit(void)
{
	sys_mutex_attr_t attr;

	memset(&attr, 0, sizeof(attr));
	attr.attr_protocol = SYS_MUTEX_PROTOCOL_PRIO;
	attr.attr_recursive = SYS_MUTEX_ATTR_NOT_RECURSIVE;
	attr.attr_pshared = SYS_MUTEX_ATTR_NOT_PSHARED;
	attr.attr_adaptive = SYS_MUTEX_ATTR_NOT_ADAPTIVE;
	strcpy(attr.name, "dukelog");
	log_mutex_ok = (sysMutexCreate(&log_mutex, &attr) == 0);
}

#define LOG_LOCK()   do { if (log_mutex_ok) sysMutexLock(log_mutex, 0); } while (0)
#define LOG_UNLOCK() do { if (log_mutex_ok) sysMutexUnlock(log_mutex); } while (0)

#else
#include <pthread.h>

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;
static void LogLockInit(void) { }
#define LOG_LOCK()   pthread_mutex_lock(&log_mutex)
#define LOG_UNLOCK() pthread_mutex_unlock(&log_mutex)
#endif

static FILE *logfp;

/* One session's log never grows past this; the previous session's is
   kept as eduke32-ps3.old.log. So at most 2 x 512 KB on the HDD. */
#define LOG_MAX_BYTES (512 * 1024)
static long logbytes;
static int logfull;

/* Called with the lock held. Returns 0 once the limit is reached. */
static int
LogRoom(size_t len)
{
	if (logfull)
	{
		return 0;
	}

	if (logbytes + (long)len > LOG_MAX_BYTES)
	{
		fputs("\n=== log size limit reached: the rest of this session is not logged ===\n",
				logfp);
		fflush(logfp);
		logfull = 1;
		return 0;
	}

	logbytes += (long)len;

	return 1;
}
/* The engine's printf hands us fragments; remember whether we are mid-line so the
 * next PS3_Log() starts on a fresh one. */
static int midline;

static void StartDone(void);

void
PS3_LogInit(void)
{
	StartDone();

	if (logfp)
	{
		return;
	}

	/* only with USRDIR/logs.txt */
	{
		FILE *const on = fopen(PS3_USRDIR "/logs.txt", "r");

		if (!on)
		{
			return;
		}

		fclose(on);
	}

	LogLockInit();

	/* back in the launcher from the game (ps3_launcher.c's marker): the
	   same session, the same log, so the game's last lines (an error)
	   stay in it */
	{
		FILE *marker = fopen(PS3_USRDIR "/ps3_return_to_launcher", "r");

		if (marker)
		{
			fclose(marker);
			logfp = fopen(PS3_LOGFILE, "a");

			if (logfp)
			{
				fseek(logfp, 0, SEEK_END);
				logbytes = ftell(logfp);

				/* room for this session too, or a fresh log */
				if (logbytes < LOG_MAX_BYTES / 2)
				{
					fputs("=== back in the launcher ===\n", logfp);
					fflush(logfp);
					return;
				}

				fclose(logfp);
				logfp = NULL;
				logbytes = 0;
			}
		}
	}

	remove(PS3_LOGFILE_OLD);
	rename(PS3_LOGFILE, PS3_LOGFILE_OLD);

	logfp = fopen(PS3_LOGFILE, "w");

	if (logfp)
	{
		fputs("=== " PS3_GAME_NAME " (PS3) log ===\n", logfp);
		fflush(logfp);
	}
}

/* A trace from before main(): the C++ static initializers run first, and
   one of them dying left no log at all (NBlood-PS3 patch 1). Patch 2 opened
   the log there and hung (newlib's stdio and lv2 mutexes aren't ready that
   early): only lv2's own file calls here. With logs.txt, PS3_PROGRAM
   ".start.log" says the program started; the log's "[0]" that main() came. */
#ifdef __PPU__
#include <sys/file.h>

static s32 start_fd = -1;

static void
StartWrite(const char *a, const char *b)
{
	u64 written;

	if (start_fd < 0)
	{
		return;
	}

	sysLv2FsWrite(start_fd, a, strlen(a), &written);
	if (b)
	{
		sysLv2FsWrite(start_fd, b, strlen(b), &written);
	}
	sysLv2FsWrite(start_fd, "\n", 1, &written);
}

__attribute__((constructor(101))) static void
LogEarly(void)
{
	sysFSStat st;

	if (sysLv2FsStat(PS3_USRDIR "/logs.txt", &st) != 0)
	{
		return;
	}

	if (sysLv2FsOpen(PS3_USRDIR "/" PS3_PROGRAM ".start.log",
					 SYS_O_WRONLY | SYS_O_CREAT | SYS_O_TRUNC, &start_fd, 0666, NULL, 0) != 0)
	{
		start_fd = -1;
	}

	StartWrite("static initialization started", NULL);
}

/* compat/ps3_initmark.h: a C++ file's static initializers begin */
int
PS3_InitMark(const char *file)
{
	StartWrite("init ", file);
	return 0;
}

/* main(): the static initialization is over */
static void
StartDone(void)
{
	StartWrite("main() reached", NULL);

	if (start_fd >= 0)
	{
		sysLv2FsClose(start_fd);
		start_fd = -1;
	}
}
#else
int PS3_InitMark(const char *file) { (void)file; return 0; }
static void StartDone(void) { }
#endif

int
PS3_LogsEnabled(void)
{
	return logfp != NULL;
}

void
PS3_LogRaw(const char *text)
{
	size_t len;

	if (!logfp || !text || !text[0])
	{
		return;
	}

	len = strlen(text);

	LOG_LOCK();

	if (LogRoom(len))
	{
		fwrite(text, 1, len, logfp);
		midline = (text[len - 1] != '\n');
		fflush(logfp);
	}

	LOG_UNLOCK();
}

void
PS3_Log(const char *fmt, ...)
{
	char buf[1024];
	va_list ap;

	if (!logfp)
	{
		return;
	}

	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	LOG_LOCK();

	if (LogRoom(strlen(buf) + 2))
	{
		if (midline)
		{
			fputc('\n', logfp);
		}

		fputs(buf, logfp);
		fputc('\n', logfp);
		midline = 0;
		fflush(logfp);
	}

	LOG_UNLOCK();
}

void
PS3_LogShutdown(void)
{
	if (!logfp)
	{
		return;
	}

	LOG_LOCK();

	/* always, even past the limit: tells a clean exit from a crash */
	fputs(midline ? "\n=== end of log ===\n" : "=== end of log ===\n", logfp);

	fclose(logfp);
	logfp = NULL;
	LOG_UNLOCK();
}
