// NBlood-PS3: what the PS3 layer asks of PCExhumed (PowerSlave, exhumed.cpp).
//
// Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later, as
// the rest of NBlood.

#include "build.h"
#include "config.h"
#include "cd.h"
#include "sound.h"
#include "status.h"

// The XMB's "Quit Game" (ps3layer.cpp): the settings saved and the music
// and sound stopped, as ShutDown does on the way out, without the rest
// of it. PS3_Exit does the rest.
extern "C" void PS3_GameQuit(void)
{
    CONFIG_WriteSetup(0);
    StopCD();
    UnInitFX();
}

// The texture filter turned off (ps3layer.cpp: the RSX memory ran out
// with it on): the status line says so.
extern "C" void PS3_GameFilterOff(void)
{
    StatusMessage(300, "Not enough video memory: texture filter off");
}

// The autotest (USRDIR/autotest.txt) is EDuke32-PS3's: here the flag is
// read and nothing more.
int ps3_autotest;
