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
 * PS3 (PSL1GHT) platform layer for EDuke32 -- shared declarations.
 *
 * The engine talks to the platform through the Build "render layer"
 * contract (baselayer.h), implemented for the PS3 in ps3layer.cpp, and
 * through the audiolib's sound driver table (driver_ps3.cpp). What it
 * calls from the C library that has to behave differently here (paths,
 * stdout/stderr, exit) is renamed in its objects to the PS3_* versions
 * of ps3_stdio.c (ps3/Makefile, objcopy --redefine-sym).
 *
 * Everything lives in USRDIR: the game data the user copies by FTP, the
 * config, the savegames and the log.
 *
 * =======================================================================
 */

#ifndef PS3_PLATFORM_H
#define PS3_PLATFORM_H

#include <stddef.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The game this build is (ps3/Makefile: EDuke32-PS3's GAME=duke3d or
   GAME=sw, NBlood-PS3's GAME=blood) */
#if defined PS3_GAME_BLOOD
#define PS3_GAME_NAME     "NBlood"
#define PS3_PROGRAM       "nblood-ps3"
#define PS3_LAUNCHER_NAME "NBLOOD"
#define PS3_THREAD_NAME   "bloodgame"
#define PS3_GAME_SW_OR(sw, duke) (duke)
#ifndef PS3_APPID
#define PS3_APPID "NBLOODPS3"
#endif
#elif defined PS3_GAME_SW
#define PS3_GAME_NAME   "VoidSW"
#define PS3_PROGRAM     "voidsw-ps3"
#define PS3_LAUNCHER_NAME "VOIDSW"
#define PS3_THREAD_NAME   "swgame"
#define PS3_GAME_SW_OR(sw, duke) (sw)
#ifndef PS3_APPID
#define PS3_APPID "SHADOWPS3"
#endif
#else
#define PS3_GAME_NAME   "EDuke32"
#define PS3_PROGRAM     "eduke32-ps3"
#define PS3_LAUNCHER_NAME "EDUKE32"
#define PS3_THREAD_NAME   "dukegame"
#define PS3_GAME_DUKE3D
#define PS3_GAME_SW_OR(sw, duke) (duke)
#ifndef PS3_APPID
#define PS3_APPID "DUKE3DPS3"
#endif
#endif

#ifndef PS3_USRDIR
#define PS3_USRDIR "/dev_hdd0/game/" PS3_APPID "/USRDIR"
#endif

#define PS3_LOGFILE     PS3_USRDIR "/" PS3_PROGRAM ".log"
#define PS3_LOGFILE_OLD PS3_USRDIR "/" PS3_PROGRAM ".old.log"

/* ps3_log.c */
void PS3_LogInit(void);
int  PS3_LogsEnabled(void);          /* USRDIR/logs.txt there: the log is on */
void PS3_Log(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void PS3_LogRaw(const char *text);   /* as is, no newline added */
void PS3_LogShutdown(void);

/* ps3_debug.c: the trail of the game thread, written to the log when it
   dies (see the file) */
void PS3_Crumb(const char *tag, int val);
void PS3_Debug_ThreadStart(unsigned stack_size);
void PS3_Debug_Dump(const char *why);

/* ps3layer.cpp */
void PS3_Pump(void);                 /* sysutil queue; cheap, call often */
int  PS3_ExitRequested(void);        /* "Quit Game" from the XMB */
void PS3_Exit(int code) __attribute__((noreturn));
void PS3_FatalScreen(const char *title, const char *message);
unsigned long long PS3_Micros(void);

/* ps3_stdio.c: the engine's C library calls (see the top of this file) */
int PS3_printf(const char *fmt, ...);
int PS3_fprintf(FILE *f, const char *fmt, ...);
int PS3_vfprintf(FILE *f, const char *fmt, va_list ap);
int PS3_vprintf(const char *fmt, va_list ap);
int PS3_puts(const char *s);
int PS3_putchar(int c);
int PS3_fputs(const char *s, FILE *f);
int PS3_fputc(int c, FILE *f);
int PS3_putc(int c, FILE *f);
size_t PS3_fwrite(const void *p, size_t size, size_t n, FILE *f);
int PS3_fflush(FILE *f);
void PS3_perror(const char *s);
void PS3_abort(void) __attribute__((noreturn));
void PS3_exit(int code) __attribute__((noreturn));

/* The display (RSX init, flips, the software image and the OpenGL render
   target) is ps3_video.c / ps3_video.h, shared with TyrQuakeCell. */

#ifdef __cplusplus
}
#endif

#endif
