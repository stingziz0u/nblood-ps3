// NBlood-PS3: what the PS3 layer asks of Rednukem (Redneck Rampage and
// Rides Again, game.cpp).
//
// Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later, as
// the rest of NBlood.

#include "duke3d.h"

// The XMB's "Quit Game" (ps3layer.cpp): the settings saved and sound and
// music stopped, as when quitting from the menu, without the screens the
// menu's quit shows. PS3_Exit does the rest.
extern "C" void PS3_GameQuit(void)
{
    G_Shutdown();
}

// The autotest (USRDIR/autotest.txt) is EDuke32-PS3's Duke's: here the
// flag is read and nothing more.
int ps3_autotest;
