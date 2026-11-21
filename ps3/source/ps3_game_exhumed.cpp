// NBlood-PS3: what the PS3 layer asks of PCExhumed (PowerSlave, exhumed.cpp).
//
// Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later, as
// the rest of NBlood.

#include "build.h"
#include "config.h"
#include "cd.h"
#include "sound.h"
#include "status.h"
#include "timer.h"

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

// PCExhumed's menus and screens read only the keyboard (menu.cpp): the
// pad's buttons are keys there (ps3layer.cpp, PadKeys). Playing is when
// the game reads its input (GetLocalInput, input.cpp, every tic): not
// for a fifth of a second, it's a menu or a screen.
uint32_t ps3_ex_inputtime;

extern "C" int PS3_GamePadKeys(void)
{
    return (uint32_t)timerGetTicks() - ps3_ex_inputtime > 200;
}

// The autotest (USRDIR/autotest.txt, with logs.txt): the video memory
// test, every map with the texture filter on (exhumed.cpp, PS3_VramTest).
int ps3_autotest;
