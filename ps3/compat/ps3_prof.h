/*
 * ps3_prof.h -- NBlood-PS3: where a slow frame's time went.
 *
 * The game thread's frame in sections, timed with the timebase (mftb, no
 * syscall). The top level ones add up to the frame (game, rooms, masks,
 * present; what's left is the HUD, menus, status bar); the others are
 * inside them (a tile loaded from disk while the rooms draw, a texture
 * upload inside a tile). ps3/source/ps3_prof.c writes the log lines.
 *
 * In ps3_compat.h, so every engine and game source has it; the PS3
 * layer's C files include it.
 *
 * Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later.
 */

#ifndef PS3_PROF_H
#define PS3_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

enum
{
    /* top level: the frame */
    PS3P_GAME,      /* game logic: Blood's ProcessFrame, Rednukem's G_MoveLoop */
    PS3P_ROOMS,     /* renderDrawRoomsQ16 */
    PS3P_MASKS,     /* renderDrawMasks: sprites, masked walls */
    PS3P_PRESENT,   /* videoShowFrame: close, scale to the TV, flip */

    /* inside the ones above */
    PS3P_TILE,      /* tileLoad: art from the cache or the disk */
    PS3P_DISK,      /* kread */
    PS3P_TEX,       /* Polymost's gloadtile_art: a tile to a texture */
    PS3P_UPLOAD,    /* ps3gl's glTexImage2D / glTexSubImage2D */
    PS3P_ALOCK,     /* waiting for the audio thread's lock (MV_Lock) */
    PS3P_RSXSYNC,   /* waiting for the RSX to go idle (PS3_Video_Finish) */
    PS3P_FLIP,      /* waiting for a free framebuffer (inside present) */

    PS3P_COUNT
};

void PS3_ProfBegin(int id);
void PS3_ProfEnd(int id);
void PS3_ProfBytes(int id, unsigned bytes);
void PS3_ProfFrame(void);   /* the end of a frame: videoShowFrame */

/* a timebase stamp and a length of them in ms (slow-event checks) */
unsigned long long PS3_ProfClock(void);
double PS3_ProfClockMs(unsigned long long ticks);

/* the game logic in parts, for the slow frames' lines: names once (the
   game's own, up to PS3P_MAX_PARTS), then PS3_ProfPart(n) where part n
   starts and PS3_ProfPart(-1) where the last one ends */
#define PS3P_MAX_PARTS 12
void PS3_ProfPartNames(char const *const *names, int count);
void PS3_ProfPart(int part);

#ifdef __cplusplus
}

struct PS3_ProfScope
{
    int const id;
    explicit PS3_ProfScope(int i) : id(i) { PS3_ProfBegin(i); }
    ~PS3_ProfScope() { PS3_ProfEnd(id); }
};

#define PS3_PROF_SCOPE(id) PS3_ProfScope const ps3_prof_scope(id)
#endif

#endif
