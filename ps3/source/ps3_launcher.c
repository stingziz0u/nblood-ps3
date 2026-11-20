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
 * The game picker shown before the engine starts, as TyrQuakeCell's,
 * ECellWolf's and CrispyCell's: one package for the base game and its
 * add-ons.
 *
 * The game data can be in USRDIR, in USRDIR/addons, or in a folder of its
 * own inside addons (addons/nwinter/NWINTER.GRP...). The known group
 * files (the table below) are the GAMES tab; any other .grp/.zip/.pk3 in
 * addons is on a MODS tab, played over the base game. The choice becomes
 * the engine's command line:
 *
 *   -j <folder>          the folders the files are in (search paths)
 *   -gamegrp <file>      the game: its grpinfo (CRC) brings its scripts
 *                        and the base game it needs
 *   -grp <path>          a mod, over the base game
 *   -game_dir saves/<id> the game's saves (and its texture cache), so the
 *                        games don't mix their savegames
 *
 * With one thing to play the menu is skipped. Quit from the game's menu
 * comes back here with a fresh process (the marker
 * USRDIR/ps3_return_to_launcher makes the menu show even with one entry;
 * CIRCLE then goes to the XMB), and so does a fatal error, its message in
 * the marker and shown under the list: RELOAD.SELF, the same program as
 * a plain SELF, since sysProcessExitSpawn2 cannot start the NPDRM
 * EBOOT.BIN. "Quit Game" from the XMB quits for good.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <sys/stat.h>

#include <sys/process.h>
#include <sysutil/sysutil.h>

#include "ps3_launcher.h"
#include "ps3_pad.h"
#include "ps3_platform.h"
#include "ps3_video.h"

#define ADDONS_DIR    PS3_USRDIR "/addons"
#define SAVES_DIR     PS3_USRDIR "/saves"
#define RETURN_MARKER PS3_USRDIR "/ps3_return_to_launcher"
#define RELOAD_SELF   PS3_USRDIR "/RELOAD.SELF"

/* The games this program plays, by their group file's name (or, for an
   add-on that is loose files over the base game's group, one of its
   files: Suckin' Grits' USER66.CON) */
typedef struct
{
    const char *file;
    const char *label;
    const char *id;         /* saves/<id> */
    int needs_base;         /* an add-on: the base game's file too */
} known_t;

#if defined PS3_GAME_BLOOD
/* NBlood: the game is BLOOD.RFF and the files next to it (no group file
   of its own); an episode set is an .INI, Cryptic Passage's included */
#define LAUNCHER_TITLE "NBLOOD"
#define PROGRAM_NAME   "nblood"
#define BASE_FILE      "BLOOD.RFF"
static const known_t known[] =
{
    { "BLOOD.RFF",   "Blood",           "blood",   0 },
    { "CRYPTIC.INI", "Cryptic Passage", "cryptic", 1 },
};
#elif defined PS3_GAME_RR
/* Rednukem: Redneck Rampage's and Rides Again's data are both a
   REDNECK.GRP (grpscan.cpp tells them apart by its checksum): one game
   per package, each in its own USRDIR */
#define LAUNCHER_TITLE PS3_LAUNCHER_NAME
#define PROGRAM_NAME   "rednukem"
#define BASE_FILE      "REDNECK.GRP"
#define MUSIC_DIR      PS3_USRDIR "/music"
static const known_t known[] =
{
#ifdef PS3_GAME_RRRA
    { "REDNECK.GRP", "Redneck Rampage Rides Again", "rrra", 0 },
#else
    { "REDNECK.GRP", "Redneck Rampage",             "rr",   0 },
    /* Suckin' Grits on Route 66: loose files (its GAME.CON, the *66.CON,
       two ART files, its maps in ROUTE66/) in a folder of addons
       (addons/route66/), over REDNECK.GRP. Its folder first in the search
       path, so its GAME.CON and art are found before the group's. */
    { "USER66.CON",  "Suckin' Grits on Route 66",   "route66", 1 },
#endif
};
#elif defined PS3_GAME_EXHUMED
/* PCExhumed: STUFF.DAT, PowerSlave's or Exhumed's (grpscan.cpp tells
   them apart by its checksum); the CD soundtrack in music */
#define LAUNCHER_TITLE PS3_LAUNCHER_NAME
#define PROGRAM_NAME   "pcexhumed"
#define BASE_FILE      "STUFF.DAT"
#define MUSIC_DIR      PS3_USRDIR "/music"
#define NO_GAME_DIR    /* PCExhumed has no -game_dir: saves in USRDIR */
static const known_t known[] =
{
    { "STUFF.DAT", "PowerSlave", "exhumed", 0 },
};
#elif defined PS3_GAME_SW
#define LAUNCHER_TITLE "VOIDSW"
#define PROGRAM_NAME   "voidsw"
#define BASE_FILE      "SW.GRP"
static const known_t known[] =
{
    { "SW.GRP",     "Shadow Warrior",     "sw", 0 },
    { "WT.GRP",     "Wanton Destruction", "wt", 1 },
    { "TD.GRP",     "Twin Dragon",        "td", 1 },
};
#else
#define LAUNCHER_TITLE "EDUKE32"
#define PROGRAM_NAME   "eduke32"
#define BASE_FILE      "DUKE3D.GRP"
static const known_t known[] =
{
    { "DUKE3D.GRP",   "Duke Nukem 3D",                  "duke3d",   0 },
    { "DUKEDC.GRP",   "Duke It Out in D.C.",            "dukedc",   1 },
    { "NWINTER.GRP",  "Duke: Nuclear Winter",           "nwinter",  1 },
    { "VACATION.GRP", "Duke Caribbean: Life's a Beach", "vacation", 1 },
    { "NAM.GRP",      "NAM",                            "nam",      0 },
    { "NAPALM.GRP",   "Napalm",                         "napalm",   0 },
    { "WW2GI.GRP",    "World War II GI",                "ww2gi",    0 },
};
#endif
#define NUM_KNOWN ((int)(sizeof(known) / sizeof(known[0])))

volatile int ps3_launcher_active;

#define LAUNCHER_W   640
#define LAUNCHER_H   400
#define MAX_ENTRIES  48
#define MAX_NAME     48
#define MAX_PATH_LEN 256
#define MAX_ARGS     20
#define VISIBLE_ROWS 9
#define GLYPH_W      8
#define GLYPH_H      8
#define FONT_FIRST   32
#define FONT_LAST    95

#define COL_BG      0xff101018
#define COL_TEXT    0xffc8c8c0
#define COL_DIM     0xff707068
#define COL_SEL_BG  0xff803018
#define COL_SEL_TX  0xffffffff
#define COL_TITLE   0xffd08040
#define COL_ERROR   0xffe05040

#define MAX_ERROR   512
#define ERROR_LINES 6

/* 8x8 glyphs for ASCII 32..95 (printable, uppercase only -- lowercase
   is folded to uppercase at draw time), CrispyCell's launcher font */
static const unsigned char launcher_font[FONT_LAST - FONT_FIRST + 1][8] =
{
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // 'space'
    { 0x18, 0x18, 0x18, 0x18, 0x18, 0x00, 0x18, 0x00 },  // '!'
    { 0x36, 0x36, 0x36, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '"'
    { 0x36, 0x36, 0xfe, 0x36, 0xfe, 0x36, 0x36, 0x00 },  // '#'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '$'
    { 0xe2, 0xd2, 0xe4, 0x08, 0x3b, 0x2b, 0x3b, 0x00 },  // '%'
    { 0x38, 0x6c, 0x38, 0x76, 0xdc, 0xcc, 0x3d, 0x00 },  // '&'
    { 0x18, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '''
    { 0x0c, 0x18, 0x30, 0x30, 0x30, 0x18, 0x0c, 0x00 },  // '('
    { 0x30, 0x18, 0x0c, 0x0c, 0x0c, 0x18, 0x30, 0x00 },  // ')'
    { 0x00, 0x2a, 0x1c, 0x3e, 0x1c, 0x2a, 0x00, 0x00 },  // '*'
    { 0x00, 0x18, 0x18, 0x7e, 0x18, 0x18, 0x00, 0x00 },  // '+'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x30 },  // ','
    { 0x00, 0x00, 0x00, 0x7e, 0x00, 0x00, 0x00, 0x00 },  // '-'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x18, 0x18, 0x00 },  // '.'
    { 0x02, 0x06, 0x0c, 0x18, 0x30, 0x60, 0x40, 0x00 },  // '/'
    { 0x3c, 0x66, 0x6e, 0x7e, 0x76, 0x66, 0x3c, 0x00 },  // '0'
    { 0x18, 0x38, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },  // '1'
    { 0x3c, 0x66, 0x06, 0x0c, 0x18, 0x30, 0x7e, 0x00 },  // '2'
    { 0x7e, 0x0c, 0x18, 0x0c, 0x06, 0x66, 0x3c, 0x00 },  // '3'
    { 0x0e, 0x1e, 0x36, 0x66, 0x7f, 0x06, 0x0f, 0x00 },  // '4'
    { 0x7e, 0x60, 0x7c, 0x06, 0x06, 0x66, 0x3c, 0x00 },  // '5'
    { 0x1c, 0x30, 0x60, 0x7c, 0x66, 0x66, 0x3c, 0x00 },  // '6'
    { 0x7e, 0x66, 0x0c, 0x18, 0x18, 0x18, 0x18, 0x00 },  // '7'
    { 0x3c, 0x66, 0x66, 0x3c, 0x66, 0x66, 0x3c, 0x00 },  // '8'
    { 0x3c, 0x66, 0x66, 0x3e, 0x06, 0x0c, 0x38, 0x00 },  // '9'
    { 0x00, 0x18, 0x18, 0x00, 0x00, 0x18, 0x18, 0x00 },  // ':'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // ';'
    { 0x0e, 0x18, 0x30, 0x60, 0x30, 0x18, 0x0e, 0x00 },  // '<'
    { 0x00, 0x00, 0x7e, 0x00, 0x7e, 0x00, 0x00, 0x00 },  // '='
    { 0x70, 0x18, 0x0c, 0x06, 0x0c, 0x18, 0x70, 0x00 },  // '>'
    { 0x3c, 0x66, 0x0c, 0x18, 0x18, 0x00, 0x18, 0x00 },  // '?'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '@'
    { 0x18, 0x3c, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x00 },  // 'A'
    { 0x7c, 0x66, 0x66, 0x7c, 0x66, 0x66, 0x7c, 0x00 },  // 'B'
    { 0x3c, 0x66, 0x60, 0x60, 0x60, 0x66, 0x3c, 0x00 },  // 'C'
    { 0x78, 0x6c, 0x66, 0x66, 0x66, 0x6c, 0x78, 0x00 },  // 'D'
    { 0x7f, 0x63, 0x68, 0x78, 0x68, 0x63, 0x7f, 0x00 },  // 'E'
    { 0x7f, 0x63, 0x68, 0x78, 0x68, 0x60, 0x78, 0x00 },  // 'F'
    { 0x3c, 0x66, 0x60, 0x60, 0x67, 0x66, 0x3c, 0x00 },  // 'G'
    { 0x66, 0x66, 0x66, 0x7e, 0x66, 0x66, 0x66, 0x00 },  // 'H'
    { 0x3c, 0x18, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },  // 'I'
    { 0x07, 0x03, 0x03, 0x03, 0x63, 0x63, 0x3e, 0x00 },  // 'J'
    { 0x63, 0x66, 0x6c, 0x78, 0x6c, 0x66, 0x63, 0x00 },  // 'K'
    { 0x78, 0x60, 0x60, 0x60, 0x63, 0x63, 0x7f, 0x00 },  // 'L'
    { 0x63, 0x77, 0x7f, 0x6b, 0x63, 0x63, 0x63, 0x00 },  // 'M'
    { 0x63, 0x73, 0x7b, 0x6f, 0x67, 0x63, 0x63, 0x00 },  // 'N'
    { 0x3e, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3e, 0x00 },  // 'O'
    { 0x7e, 0x63, 0x63, 0x7e, 0x60, 0x60, 0x78, 0x00 },  // 'P'
    { 0x3e, 0x63, 0x63, 0x63, 0x6b, 0x66, 0x3b, 0x00 },  // 'Q'
    { 0x7e, 0x63, 0x63, 0x7e, 0x6c, 0x66, 0x63, 0x00 },  // 'R'
    { 0x3e, 0x63, 0x70, 0x3e, 0x0e, 0x66, 0x3c, 0x00 },  // 'S'
    { 0x7f, 0x5a, 0x18, 0x18, 0x18, 0x18, 0x3c, 0x00 },  // 'T'
    { 0x63, 0x63, 0x63, 0x63, 0x63, 0x63, 0x3e, 0x00 },  // 'U'
    { 0x63, 0x63, 0x63, 0x63, 0x63, 0x2c, 0x18, 0x00 },  // 'V'
    { 0x63, 0x63, 0x63, 0x6b, 0x7f, 0x77, 0x63, 0x00 },  // 'W'
    { 0x63, 0x63, 0x2c, 0x18, 0x2c, 0x63, 0x63, 0x00 },  // 'X'
    { 0x63, 0x63, 0x63, 0x3e, 0x0c, 0x18, 0x3c, 0x00 },  // 'Y'
    { 0x7f, 0x63, 0x0c, 0x18, 0x30, 0x63, 0x7f, 0x00 },  // 'Z'
    { 0x3c, 0x30, 0x30, 0x30, 0x30, 0x30, 0x3c, 0x00 },  // '['
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '\'
    { 0x3c, 0x0c, 0x0c, 0x0c, 0x0c, 0x0c, 0x3c, 0x00 },  // ']'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 },  // '^'
    { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff },  // '_'
};

typedef struct
{
    char label[MAX_NAME];
    char note[MAX_NAME];            /* small, at the right */
    char dir[MAX_PATH_LEN];         /* the folder its file is in */
    char file[MAX_NAME];            /* the file's name, as it is on disk */
    char id[MAX_NAME];              /* saves/<id> */
    int known;                      /* index into known[], -1 for a mod */
} entry_t;

static entry_t entries[MAX_ENTRIES];
static int num_entries;

/* where each known file was found ("" if not) */
static char found_dir[NUM_KNOWN][MAX_PATH_LEN];
static char found_file[NUM_KNOWN][MAX_NAME];

static char arg_buf[MAX_ARGS][MAX_PATH_LEN];
static char *arg_ptrs[MAX_ARGS + 1];
static int arg_count;

static uint32_t *fb;
static int launcher_used;

/* the error the last game ended with, and the list's rows left by it */
static char last_error[MAX_ERROR];
static int visible_rows = VISIBLE_ROWS;

/*
 * Files
 */

static int
FileExists(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0;
}

static int
IsDir(const char *path)
{
    struct stat st;

    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int
HasExtension(const char *name, const char *ext)
{
    size_t const n = strlen(name), e = strlen(ext);

    return n > e && !strcasecmp(name + n - e, ext);
}

/*
 * The command line
 */

static void
AddArg(const char *arg)
{
    if (arg_count >= MAX_ARGS)
        return;

    snprintf(arg_buf[arg_count], MAX_PATH_LEN, "%s", arg);
    arg_ptrs[arg_count] = arg_buf[arg_count];
    arg_count++;
    arg_ptrs[arg_count] = NULL;
}

/* -j <dir>, once per folder; USRDIR is the working directory already */
static void
AddSearchPath(const char *dir)
{
    int i;

    if (!dir[0] || !strcmp(dir, PS3_USRDIR))
        return;

#ifdef PS3_GAME_SW
    /* VoidSW: -j<dir>, in one */
    {
        char arg[MAX_PATH_LEN];

        snprintf(arg, sizeof(arg), "-j%s", dir);

        for (i = 1; i < arg_count; i++)
            if (!strcmp(arg_ptrs[i], arg))
                return;

        AddArg(arg);
    }
#else
    for (i = 1; i + 1 < arg_count; i++)
        if (!strcmp(arg_ptrs[i], "-j") && !strcmp(arg_ptrs[i + 1], dir))
            return;

    AddArg("-j");
    AddArg(dir);
#endif
}

static int
BaseIndex(void)
{
    int i;

    for (i = 0; i < NUM_KNOWN; i++)
        if (!strcasecmp(known[i].file, BASE_FILE))
            return i;

    return 0;
}

static void
ArgsForEntry(const entry_t *e)
{
    char path[MAX_PATH_LEN];
    const int base = BaseIndex();

    arg_count = 0;
    AddArg(PROGRAM_NAME);

    /* the settings in the "working directory", USRDIR (ps3_stdio.c) */
    AddArg("-usecwd");

    if (IsDir(ADDONS_DIR))
        AddSearchPath(ADDONS_DIR);

    AddSearchPath(e->dir);

    /* the base game, wherever it is, for an add-on or a mod */
    if (e->known < 0 || known[e->known].needs_base)
        AddSearchPath(found_dir[base]);

#ifdef MUSIC_DIR
    /* the CD soundtrack (track02.ogg ...), found by its name */
    if (IsDir(MUSIC_DIR))
        AddSearchPath(MUSIC_DIR);
#endif

#ifdef PS3_GAME_BLOOD
    /* NBlood: the episodes' .INI (BLOOD.INI by default), a group file on
       top for a mod */
    if (HasExtension(e->file, ".ini"))
    {
        AddArg("-ini");
        AddArg(e->file);
    }
    else if (e->known < 0)
    {
        snprintf(path, sizeof(path), "%s/%s", e->dir, e->file);
        AddArg("-g");
        AddArg(path);
    }
#else
    /* the game's group file; an add-on of loose files (a .CON): the base
       game's */
    AddArg("-gamegrp");
    AddArg(e->known >= 0 && !HasExtension(e->file, ".con") ? e->file : found_file[base]);

    if (e->known < 0)
    {
#ifdef PS3_GAME_SW
        /* VoidSW: -g<file>, in one */
        snprintf(path, sizeof(path), "-g%s/%s", e->dir, e->file);
        AddArg(path);
#else
        snprintf(path, sizeof(path), "%s/%s", e->dir, e->file);
        AddArg("-grp");
        AddArg(path);
#endif
    }
#endif

#ifndef NO_GAME_DIR
    /* the game's own saves folder (made here: the engine makes only the
       last level of it) */
    mkdir(SAVES_DIR, 0777);
    snprintf(path, sizeof(path), "%s/%s", SAVES_DIR, e->id);
    mkdir(path, 0777);

    snprintf(path, sizeof(path), "saves/%s", e->id);
    AddArg("-game_dir");
    AddArg(path);
#endif
}

/*
 * Drawing
 */

static void
FillRect(int x, int y, int w, int h, uint32_t colour)
{
    int px, py;

    for (py = y; py < y + h; py++)
    {
        if (py < 0 || py >= LAUNCHER_H)
            continue;
        for (px = x; px < x + w; px++)
            if (px >= 0 && px < LAUNCHER_W)
                fb[py * LAUNCHER_W + px] = colour;
    }
}

static void
DrawChar(int x, int y, char c, uint32_t colour, int scale)
{
    const unsigned char *glyph;
    int row, col;

    if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    if (c < FONT_FIRST || c > FONT_LAST)
        c = '?';

    glyph = launcher_font[c - FONT_FIRST];

    for (row = 0; row < GLYPH_H; row++)
        for (col = 0; col < GLYPH_W; col++)
            if (glyph[row] & (0x80 >> col))
                FillRect(x + col * scale, y + row * scale, scale, scale, colour);
}

static void
DrawText(int x, int y, const char *s, uint32_t colour, int scale)
{
    for (; *s; s++, x += GLYPH_W * scale)
        DrawChar(x, y, *s, colour, scale);
}

static int
TextWidth(const char *s, int scale)
{
    return (int)strlen(s) * GLYPH_W * scale;
}

/* The error, word-wrapped from y down, at most ERROR_LINES lines */
static void
DrawError(int y)
{
    const int cols = (LAUNCHER_W - 48) / GLYPH_W;
    const char *s = last_error;
    char line[128];
    int lines;

    DrawText(24, y, "THE GAME STOPPED WITH AN ERROR:", COL_ERROR, 1);
    y += 14;

    for (lines = 0; *s && lines < ERROR_LINES; lines++, y += 11)
    {
        int len = (int)strlen(s), cut = len;
        const char *nl = strchr(s, '\n');

        if (nl && nl - s < cut)
            cut = (int)(nl - s);

        if (cut > cols)
        {
            for (cut = cols; cut > 0 && s[cut] != ' '; cut--)
                ;
            if (cut == 0)
                cut = cols;
        }

        memcpy(line, s, cut);
        line[cut] = 0;
        DrawText(24, y, line, COL_TEXT, 1);

        s += cut;
        while (*s == ' ' || *s == '\n')
            s++;
    }
}

/*
 * What is installed
 */

static entry_t *
NewEntry(const char *label, const char *dir, const char *file, int known_index)
{
    entry_t *e;
    size_t i;

    if (num_entries >= MAX_ENTRIES)
        return NULL;

    e = &entries[num_entries++];
    memset(e, 0, sizeof(*e));
    snprintf(e->label, sizeof(e->label), "%.*s", MAX_NAME - 1, label);
    snprintf(e->dir, sizeof(e->dir), "%s", dir);
    snprintf(e->file, sizeof(e->file), "%.*s", MAX_NAME - 1, file);
    e->known = known_index;

    if (known_index >= 0)
        snprintf(e->id, sizeof(e->id), "%s", known[known_index].id);
    else
    {
        /* a mod: its file's name, lowercase, without the extension */
        snprintf(e->id, sizeof(e->id), "mod_%.*s", MAX_NAME - 8, file);
        for (i = 0; e->id[i]; i++)
        {
            if (e->id[i] == '.')
            {
                e->id[i] = 0;
                break;
            }
            if (e->id[i] >= 'A' && e->id[i] <= 'Z')
                e->id[i] += 32;
        }
    }

    return e;
}

static int
KnownIndex(const char *name)
{
    int i;

    for (i = 0; i < NUM_KNOWN; i++)
        if (!strcasecmp(name, known[i].file))
            return i;

    return -1;
}

static int
IsModFile(const char *name)
{
#ifdef PS3_GAME_BLOOD
    /* an episode set of its own (user maps): its .INI; BLOOD.INI is the
       base game's */
    if (HasExtension(name, ".ini"))
        return strcasecmp(name, "BLOOD.INI") != 0;
#endif
    return HasExtension(name, ".grp") || HasExtension(name, ".zip") ||
           HasExtension(name, ".pk3");
}

/* The files of one folder: the known ones remembered (the first place
   each is found wins: USRDIR, then addons, then its folders), the others
   (in addons only) as mods */
static void
ScanDir(const char *dir, int mods)
{
    DIR *d = opendir(dir);
    struct dirent *de;

    if (!d)
        return;

    while ((de = readdir(d)) != NULL)
    {
        const int k = KnownIndex(de->d_name);

        if (de->d_name[0] == '.' || strlen(de->d_name) >= MAX_NAME)
            continue;

        if (k >= 0)
        {
            if (!found_dir[k][0])
            {
                snprintf(found_dir[k], MAX_PATH_LEN, "%s", dir);
                snprintf(found_file[k], MAX_NAME, "%.*s", MAX_NAME - 1, de->d_name);
                PS3_Log("Launcher: %s in %s", de->d_name, dir);
            }
        }
        else if (mods && IsModFile(de->d_name))
        {
            /* listed after the games (see ScanGames) */
            NewEntry(de->d_name, dir, de->d_name, -1);
        }
    }

    closedir(d);
}

static int
CompareNames(const void *a, const void *b)
{
    return strcasecmp((const char *)a, (const char *)b);
}

static void
ScanGames(void)
{
    char names[MAX_ENTRIES][MAX_NAME];
    char path[MAX_PATH_LEN];
    entry_t mods[MAX_ENTRIES];
    int num_mods, n = 0, i;
    DIR *d;
    struct dirent *de;

    num_entries = 0;
    memset(found_dir, 0, sizeof(found_dir));
    memset(found_file, 0, sizeof(found_file));

    ScanDir(PS3_USRDIR, 0);
    ScanDir(ADDONS_DIR, 1);

    /* addons/<folder>/: one level down */
    if ((d = opendir(ADDONS_DIR)) != NULL)
    {
        while ((de = readdir(d)) != NULL && n < MAX_ENTRIES)
            if (de->d_name[0] != '.' && strlen(de->d_name) < MAX_NAME)
                snprintf(names[n++], MAX_NAME, "%.*s", MAX_NAME - 1, de->d_name);
        closedir(d);
    }

    qsort(names, n, MAX_NAME, CompareNames);

    for (i = 0; i < n; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", ADDONS_DIR, names[i]);
        if (IsDir(path))
            ScanDir(path, 1);
    }

    /* the mods found so far go after the games */
    num_mods = num_entries;
    memcpy(mods, entries, sizeof(entry_t) * num_mods);
    num_entries = 0;

    for (i = 0; i < NUM_KNOWN; i++)
    {
        const int base = BaseIndex();

        if (!found_dir[i][0])
            continue;

        if (known[i].needs_base && !found_dir[base][0])
        {
            PS3_Log("Launcher: %s needs %s, not found", known[i].file, BASE_FILE);
            continue;
        }

        /* loose add-on files in USRDIR or addons itself (both always in
           the search path) would be the base game's too, its GAME.CON
           found first: only in a folder of addons */
        if (HasExtension(known[i].file, ".con") &&
            (!strcmp(found_dir[i], PS3_USRDIR) || !strcmp(found_dir[i], ADDONS_DIR)))
        {
            PS3_Log("Launcher: %s in %s: it goes in a folder of addons (addons/route66), not played",
                    known[i].file, found_dir[i]);
            continue;
        }

        NewEntry(known[i].label, found_dir[i], found_file[i], i);
    }

    /* mods only with the base game */
    if (found_dir[BaseIndex()][0])
    {
        for (i = 0; i < num_mods && num_entries < MAX_ENTRIES; i++)
        {
            entry_t *e = &entries[num_entries++];

            *e = mods[i];
            /* its folder, small, when it isn't addons itself */
            if (strcmp(e->dir, ADDONS_DIR))
                snprintf(e->note, sizeof(e->note), "%.*s", MAX_NAME - 1,
                         e->dir + strlen(ADDONS_DIR) + 1);
        }
    }
}

/*
 * The menu
 */

#define TAB_GAMES 0
#define TAB_MODS  1

static int tab_items[2][MAX_ENTRIES];
static int tab_count[2];

static void
BuildTabs(void)
{
    int i;

    tab_count[TAB_GAMES] = tab_count[TAB_MODS] = 0;

    for (i = 0; i < num_entries; i++)
    {
        int t = entries[i].known < 0 ? TAB_MODS : TAB_GAMES;
        tab_items[t][tab_count[t]++] = i;
    }
}

static void
DrawMenu(int tab, int selected, int top)
{
    const char *title = LAUNCHER_TITLE;
    const int tabs = tab_count[TAB_MODS] > 0;
    const char *hint = tabs ? "D-PAD CHOOSE  L1/R1 GAMES/MODS  CROSS PLAY  CIRCLE EXIT"
                            : "D-PAD TO CHOOSE    CROSS TO PLAY    CIRCLE TO EXIT";
    const int count = tab_count[tab];
    const int row_h = 26;
    char line[64];
    int i;

    fb = PS3_Video_Source();
    if (!fb)
        return;

    FillRect(0, 0, LAUNCHER_W, LAUNCHER_H, COL_BG);
    DrawText((LAUNCHER_W - TextWidth(title, 3)) / 2, 34, title, COL_TITLE, 3);

    if (tabs)
    {
        static const char *const names[2] = { "GAMES", "MODS" };
        int x = LAUNCHER_W / 2 - TextWidth("GAMES", 2) - 24;
        int t;

        for (t = 0; t < 2; t++)
        {
            int w = TextWidth(names[t], 2);

            DrawText(x, 70, names[t], t == tab ? COL_TITLE : COL_DIM, 2);
            if (t == tab)
                FillRect(x, 88, w, 2, COL_TITLE);
            x += w + 48;
        }
    }

    for (i = 0; i < visible_rows && top + i < count; i++)
    {
        const entry_t *e = &entries[tab_items[tab][top + i]];
        int y = 100 + i * row_h;
        uint32_t colour = COL_TEXT;

        if (top + i == selected)
        {
            FillRect(26, y - 4, LAUNCHER_W - 52, row_h - 4, COL_SEL_BG);
            colour = COL_SEL_TX;
        }

        DrawText(44, y, e->label, colour, 2);

        if (e->note[0])
        {
            int lw = TextWidth(e->label, 2), bw = TextWidth(e->note, 1);

            if (44 + lw + 16 + bw <= LAUNCHER_W - 40)
                DrawText(LAUNCHER_W - 40 - bw, y + 4, e->note,
                         top + i == selected ? COL_SEL_TX : COL_DIM, 1);
        }
    }

    if (last_error[0])
        DrawError(100 + visible_rows * row_h + 4);

    if (count > visible_rows)
    {
        snprintf(line, sizeof(line), "%d / %d", selected + 1, count);
        DrawText(LAUNCHER_W - TextWidth(line, 1) - 24, LAUNCHER_H - 26, line, COL_DIM, 1);
    }

    DrawText(24, LAUNCHER_H - 26, hint, COL_DIM, 1);

    PS3_Video_Present();
}

/* An index into entries[], or -1 to leave for the XMB (CIRCLE, or "Quit
   Game" from the XMB while the menu is up) */
static int
RunMenu(void)
{
    int tab = tab_count[TAB_GAMES] > 0 ? TAB_GAMES : TAB_MODS;
    int selected[2] = { 0, 0 }, top[2] = { 0, 0 };
    /* as if everything were held: the button that started the game from
       the XMB must not pick an entry by itself */
    unsigned last = ~0u;

    for (;;)
    {
        ps3_padstate_t st;
        unsigned now, pressed;
        int count;

        PS3_Pad_Poll(&st);
        now = st.connected ? st.buttons : 0;
        pressed = now & ~last;
        last = now;

        PS3_Pump();   /* PS button, "Quit Game" from the XMB */

        if (PS3_ExitRequested() || (pressed & PS3_PAD_CIRCLE))
            return -1;

        if ((pressed & (PS3_PAD_L1 | PS3_PAD_R1 | PS3_PAD_LEFT | PS3_PAD_RIGHT))
            && tab_count[TAB_GAMES] > 0 && tab_count[TAB_MODS] > 0)
            tab = (pressed & (PS3_PAD_L1 | PS3_PAD_LEFT)) ? TAB_GAMES : TAB_MODS;

        count = tab_count[tab];

        if (pressed & PS3_PAD_UP)
            selected[tab] = (selected[tab] + count - 1) % count;
        if (pressed & PS3_PAD_DOWN)
            selected[tab] = (selected[tab] + 1) % count;
        if (pressed & (PS3_PAD_CROSS | PS3_PAD_START))
            return tab_items[tab][selected[tab]];

        if (selected[tab] < top[tab])
            top[tab] = selected[tab];
        else if (selected[tab] >= top[tab] + visible_rows)
            top[tab] = selected[tab] - visible_rows + 1;

        DrawMenu(tab, selected[tab], top[tab]);
    }
}

/*
 * Entry points
 */

int
PS3_Launcher_Run(int *argc, char ***argv, int base_only)
{
    int returning, chosen = 0, i;

    returning = FileExists(RETURN_MARKER);
    if (returning)
    {
        FILE *marker = fopen(RETURN_MARKER, "r");

        if (marker)
        {
            size_t len = fread(last_error, 1, sizeof(last_error) - 1, marker);

            last_error[len] = 0;
            while (len > 0 && (last_error[len - 1] == '\n' || last_error[len - 1] == ' '))
                last_error[--len] = 0;
            fclose(marker);
        }
        remove(RETURN_MARKER);

        if (last_error[0])
        {
            PS3_Log("Launcher: back from an error");
            visible_rows = VISIBLE_ROWS - 3;
        }
        else
            PS3_Log("Launcher: back from Quit");
    }

    ScanGames();
    BuildTabs();

    if (num_entries == 0 || tab_count[TAB_GAMES] == 0)
    {
        PS3_Log("Launcher: no game data found");
        return 0;
    }

    if (base_only)
    {
        for (i = 0; i < num_entries; i++)
            if (entries[i].known == BaseIndex())
                chosen = i;
    }
    else if (num_entries > 1 || returning)
    {
        if (PS3_Video_Init() != 0 || PS3_Video_SetSource(LAUNCHER_W, LAUNCHER_H) != 0)
            PS3_Log("Launcher: no video, playing the first entry");
        else
        {
            ps3_launcher_active = 1;
            chosen = RunMenu();
            ps3_launcher_active = 0;

            if (chosen < 0)
            {
                PS3_Log("Launcher: exit to the XMB");
                PS3_Exit(0);
            }
        }
    }

    ArgsForEntry(&entries[chosen]);
    launcher_used = 1;

    PS3_Log("Launcher: '%s'", entries[chosen].label);
    for (i = 1; i < arg_count; i++)
        PS3_Log("Launcher:   %s", arg_ptrs[i]);

    *argc = arg_count;
    *argv = arg_ptrs;
    return 1;
}

int
PS3_Launcher_Used(void)
{
    return launcher_used;
}

void
PS3_Launcher_Relaunch(const char *error)
{
    FILE *marker;

    if (!launcher_used)
        return;

    if (!FileExists(RELOAD_SELF))
    {
        PS3_Log("Launcher: %s missing, exiting instead", RELOAD_SELF);
        return;
    }

    marker = fopen(RETURN_MARKER, "w");
    if (!marker)
    {
        PS3_Log("Launcher: cannot write %s, exiting instead", RETURN_MARKER);
        return;
    }
    if (error)
        fputs(error, marker);
    fclose(marker);

    /* the log stays open: every line is flushed, and the new process
       goes on in the same file (ps3_log.c, the marker) */
    PS3_Log(error ? "Launcher: error, back to the launcher" : "Launcher: Quit, back to the launcher");

    sysProcessExitSpawn2(RELOAD_SELF, NULL, NULL, NULL, 0, 1001,
                         SYS_PROCESS_SPAWN_STACK_SIZE_1M);

    PS3_Log("Launcher: sysProcessExitSpawn2 failed, exiting instead");
    remove(RETURN_MARKER);
}
