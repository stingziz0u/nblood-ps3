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
 * What the engine's C library calls turn into on the PS3. ps3/Makefile
 * renames these symbols in the engine's objects (objcopy --redefine-sym),
 * so the engine's sources stay as they are:
 *
 *  - stdout and stderr (the engine prints what it's doing, and its
 *    errors) go to the log: on the PS3 nobody reads the TTY.
 *  - Paths: PSL1GHT's file functions only understand absolute paths
 *    (handoff notes 3.50). Relative ones (the config, the savegames, the
 *    engine's own search path ".") are taken from USRDIR, and doubled
 *    slashes ("dir/" + "/" + "file") are collapsed.
 *  - There is no working directory: getcwd() says USRDIR, chdir() only
 *    checks that the directory exists.
 *
 * =======================================================================
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "ps3_platform.h"

/* ================================================================ */
/* stdout / stderr                                                    */
/* ================================================================ */

static int
IsConsole(FILE *f)
{
	return f == stdout || f == stderr;
}

int
PS3_vfprintf(FILE *f, const char *fmt, va_list ap)
{
	char buf[2048];
	int n;

	if (!IsConsole(f))
	{
		return vfprintf(f, fmt, ap);
	}

	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	PS3_LogRaw(buf);

	return n;
}

int
PS3_vprintf(const char *fmt, va_list ap)
{
	return PS3_vfprintf(stdout, fmt, ap);
}

int
PS3_fprintf(FILE *f, const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = PS3_vfprintf(f, fmt, ap);
	va_end(ap);

	return n;
}

int
PS3_printf(const char *fmt, ...)
{
	va_list ap;
	int n;

	va_start(ap, fmt);
	n = PS3_vfprintf(stdout, fmt, ap);
	va_end(ap);

	return n;
}

int
PS3_puts(const char *s)
{
	PS3_LogRaw(s);
	PS3_LogRaw("\n");
	return 1;
}

int
PS3_putchar(int c)
{
	char s[2] = {(char)c, 0};

	PS3_LogRaw(s);
	return c;
}

int
PS3_fputs(const char *s, FILE *f)
{
	if (!IsConsole(f))
	{
		return fputs(s, f);
	}

	PS3_LogRaw(s);
	return 1;
}

int
PS3_fputc(int c, FILE *f)
{
	if (!IsConsole(f))
	{
		return fputc(c, f);
	}

	return PS3_putchar(c);
}

int
PS3_putc(int c, FILE *f)
{
	return PS3_fputc(c, f);
}

size_t
PS3_fwrite(const void *p, size_t size, size_t n, FILE *f)
{
	char buf[1024];
	size_t total;

	if (!IsConsole(f))
	{
		return fwrite(p, size, n, f);
	}

	total = size * n;

	if (total >= sizeof(buf))
	{
		total = sizeof(buf) - 1;
	}

	memcpy(buf, p, total);
	buf[total] = '\0';
	PS3_LogRaw(buf);

	return n;
}

int
PS3_fflush(FILE *f)
{
	if (f && IsConsole(f))
	{
		return 0;   /* the log flushes every write */
	}

	return fflush(f);
}

void
PS3_perror(const char *s)
{
	PS3_Log("%s%s%s", s ? s : "", s ? ": " : "", strerror(errno));
}

/* ================================================================ */
/* Paths                                                              */
/* ================================================================ */

/* Absolute, without "//", "/./" or a leading "./". */
static const char *
FixPath(const char *path, char *out, size_t size)
{
	char tmp[1024];
	const char *s;
	size_t o = 0;

	if (!path)
	{
		return NULL;
	}

	if (path[0] == '/')
	{
		snprintf(tmp, sizeof(tmp), "%s", path);
	}
	else
	{
		while (path[0] == '.' && path[1] == '/')
		{
			path += 2;
		}

		if (!strcmp(path, "."))
		{
			path = "";
		}

		snprintf(tmp, sizeof(tmp), "%s/%s", PS3_USRDIR, path);
	}

	for (s = tmp; *s && o + 1 < size; s++)
	{
		if (*s == '/' && o > 0 && out[o - 1] == '/')
		{
			continue;   /* "//" */
		}

		if (*s == '.' && o > 0 && out[o - 1] == '/' && (s[1] == '/' || s[1] == '\0'))
		{
			if (s[1] == '/')
			{
				s++;    /* "/./" */
			}

			continue;
		}

		out[o++] = *s;
	}

	/* no trailing slash, except for "/" itself */
	while (o > 1 && out[o - 1] == '/')
	{
		o--;
	}

	out[o] = '\0';

	return out;
}

/* ---------------------------------------------------------------- */
/* Names in any case                                                  */
/*                                                                    */
/* The HDD's file system tells "Track02.ogg" from "TRACK02.OGG", and  */
/* the games ask for their files in the case of their DOS days, while */
/* the copies sold today (GOG, Steam, a CD copied on a PC) come in    */
/* any. When a file or folder isn't there as asked, each name in the  */
/* path is looked up again in its folder, the case ignored. Only on a */
/* miss (most lookups find the file, or nothing in any case), and the */
/* folders read are kept (a few), forgotten when anything is written. */
/* ---------------------------------------------------------------- */

#define CI_DIRS 8

typedef struct
{
	char dir[512];
	char *names;        /* "a\0b\0...\0\0" */
	unsigned used;      /* for the least recently used */
} ci_dir_t;

static ci_dir_t ci_dirs[CI_DIRS];
static unsigned ci_clock;

static void
CI_Forget(void)
{
	int i;

	for (i = 0; i < CI_DIRS; i++)
	{
		free(ci_dirs[i].names);
		ci_dirs[i].names = NULL;
		ci_dirs[i].dir[0] = '\0';
	}
}

/* The names in a folder, or NULL when it can't be read. */
static const char *
CI_List(const char *dir)
{
	ci_dir_t *slot = &ci_dirs[0];
	struct dirent *de;
	size_t size = 0, cap = 1024;
	char *names;
	DIR *d;
	int i;

	for (i = 0; i < CI_DIRS; i++)
	{
		if (ci_dirs[i].names && !strcmp(ci_dirs[i].dir, dir))
		{
			ci_dirs[i].used = ++ci_clock;
			return ci_dirs[i].names;
		}

		if (ci_dirs[i].used < slot->used)
		{
			slot = &ci_dirs[i];
		}
	}

	if (strlen(dir) >= sizeof(slot->dir) || !(d = opendir(dir)))
	{
		return NULL;
	}

	if (!(names = malloc(cap)))
	{
		closedir(d);
		return NULL;
	}

	while ((de = readdir(d)) != NULL)
	{
		size_t const len = strlen(de->d_name) + 1;

		if (size + len + 1 > cap)
		{
			char *const more = realloc(names, cap * 2 + len);

			if (!more)
			{
				break;
			}

			names = more;
			cap = cap * 2 + len;
		}

		memcpy(names + size, de->d_name, len);
		size += len;
	}

	closedir(d);
	names[size] = '\0';

	free(slot->names);
	slot->names = names;
	strcpy(slot->dir, dir);
	slot->used = ++ci_clock;

	return names;
}

/* p (absolute, FixPath's): the name on disk of what it names in another
   case. 1 if found (p changed), 0 if not (p as it was). */
static int
CI_Fix(char *p, size_t size)
{
	char out[1024];
	const char *s = p;
	size_t o = 0;
	int changed = 0;

	/* only under the HDD and the USB drives: /dev_hdd0/..., /dev_usb000/... */
	if (strncmp(p, "/dev_hdd0/", 10) && strncmp(p, "/dev_usb", 8))
	{
		return 0;
	}

	/* the device itself as it is */
	if (!(s = strchr(p + 1, '/')))
	{
		return 0;
	}

	o = (size_t)(s - p);
	memcpy(out, p, o);
	out[o] = '\0';

	while (*s == '/')
	{
		const char *const comp = s + 1;
		const char *end = strchr(comp, '/');
		size_t const len = end ? (size_t)(end - comp) : strlen(comp);
		const char *names, *n, *match = NULL;

		if (!end)
		{
			end = comp + len;
		}

		if (o + 1 + len + 1 > sizeof(out))
		{
			return 0;
		}

		names = CI_List(out);

		if (names)
		{
			for (n = names; *n; n += strlen(n) + 1)
			{
				if (strlen(n) != len)
				{
					continue;
				}

				if (!strncmp(n, comp, len))
				{
					match = n;   /* as asked */
					break;
				}

				if (!match && !strncasecmp(n, comp, len))
				{
					match = n;
				}
			}

			if (!match)
			{
				return 0;   /* not there in any case */
			}

			if (strncmp(match, comp, len))
			{
				changed = 1;
			}
		}

		out[o++] = '/';
		memcpy(out + o, match ? match : comp, len);
		o += len;
		out[o] = '\0';
		s = end;
	}

	if (!changed || o + 1 > size)
	{
		return 0;
	}

	memcpy(p, out, o + 1);
	return 1;
}

int
PS3_open(const char *path, int flags, ...)
{
	char p[1024];
	int mode = 0, fd;

	if (flags & O_CREAT)
	{
		va_list ap;

		va_start(ap, flags);
		mode = va_arg(ap, int);
		va_end(ap);
	}

	if ((flags & O_ACCMODE) != O_RDONLY || (flags & (O_CREAT | O_TRUNC)))
	{
		CI_Forget();
	}

	fd = open(FixPath(path, p, sizeof(p)), flags, mode);

	if (fd < 0 && !(flags & O_CREAT))
	{
		int const err = errno;

		if (!CI_Fix(p, sizeof(p)) || (fd = open(p, flags, mode)) < 0)
		{
			errno = err;
		}
	}

	return fd;
}

FILE *
PS3_fopen(const char *path, const char *mode)
{
	char p[1024];
	FILE *f;

	if (mode[0] != 'r' || strchr(mode, '+'))
	{
		CI_Forget();
	}

	f = fopen(FixPath(path, p, sizeof(p)), mode);

	if (!f && mode[0] == 'r')
	{
		int const err = errno;

		if (!CI_Fix(p, sizeof(p)) || !(f = fopen(p, mode)))
		{
			errno = err;
		}
	}

	return f;
}

int
PS3_stat(const char *path, struct stat *st)
{
	char p[1024];
	int r = stat(FixPath(path, p, sizeof(p)), st);

	if (r != 0)
	{
		int const err = errno;

		if (!CI_Fix(p, sizeof(p)) || (r = stat(p, st)) != 0)
		{
			errno = err;
		}
	}

	return r;
}

/* PSL1GHT's newlib has no access(): a stat() is as good for what the
   engine asks (does it exist?). */
int
PS3_access(const char *path, int mode)
{
	struct stat st;

	(void)mode;

	if (PS3_stat(path, &st) != 0)
	{
		errno = ENOENT;
		return -1;
	}

	return 0;
}

int
PS3_mkdir(const char *path, mode_t mode)
{
	char p[1024];

	CI_Forget();
	return mkdir(FixPath(path, p, sizeof(p)), mode);
}

DIR *
PS3_opendir(const char *path)
{
	char p[1024];
	DIR *d = opendir(FixPath(path, p, sizeof(p)));

	if (!d)
	{
		int const err = errno;

		if (!CI_Fix(p, sizeof(p)) || !(d = opendir(p)))
		{
			errno = err;
		}
	}

	return d;
}

int
PS3_remove(const char *path)
{
	char p[1024];

	CI_Forget();
	return remove(FixPath(path, p, sizeof(p)));
}

int
PS3_unlink(const char *path)
{
	char p[1024];

	CI_Forget();
	return unlink(FixPath(path, p, sizeof(p)));
}

int
PS3_rename(const char *from, const char *to)
{
	char a[1024], b[1024];

	CI_Forget();
	return rename(FixPath(from, a, sizeof(a)), FixPath(to, b, sizeof(b)));
}

/* There is no working directory on the PS3. The engine only changes to a
   directory to read the levels of a mod (always given with an absolute
   path) and back: succeed when the directory exists. */
int
PS3_chdir(const char *path)
{
	DIR *d = PS3_opendir(path);

	if (!d)
	{
		errno = ENOENT;
		return -1;
	}

	closedir(d);

	return 0;
}

char *
PS3_getcwd(char *buf, size_t size)
{
	if (!buf || size < sizeof(PS3_USRDIR))
	{
		errno = ERANGE;
		return NULL;
	}

	memcpy(buf, PS3_USRDIR, sizeof(PS3_USRDIR));
	return buf;
}
