/*
 * Copyright (C) 2026 the EDuke32-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * The game picker shown before the engine starts (ps3_launcher.c).
 */

#ifndef PS3_LAUNCHER_H
#define PS3_LAUNCHER_H

#ifdef __cplusplus
extern "C" {
#endif

/* Before app_main: picks the game (the menu, or straight through when
   there is one thing to play) and gives the engine's command line for it.
   base_only: no menu, the base game (the autotest). 0 when nothing to
   play was found (the caller shows its own instructions). */
int PS3_Launcher_Run(int *argc, char ***argv, int base_only);

/* While the menu is up the XMB's "Quit Game" only asks it to leave. */
extern volatile int ps3_launcher_active;

/* After the engine shut down: back to the launcher (a fresh copy of the
   program, RELOAD.SELF), which shows the error if there is one. Returns
   only when that can't be done. */
void PS3_Launcher_Relaunch(const char *error);

/* A game was started from the launcher (Quit goes back to it). */
int PS3_Launcher_Used(void);

#ifdef __cplusplus
}
#endif

#endif
