// NBlood-PS3: the PS3 port's pieces of the game (ps3_blood.cpp).
//
// Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later, as
// the rest of NBlood.

#pragma once

#ifdef __PPU__

class CGameMenuItem;

// The DualShock 3 button a game function is on (the first one found:
// buttons, then R2 / L2), in capitals; NULL if none.
const char *PS3_FunctionButton(int nFunction);

// The DS3's name of joystick button nButton (0..14) or of L2 / R2 (15 / 16).
const char *PS3_ButtonName(int nButton);

// Dead in a single player game, the death prompt on screen (view.cpp):
// Open restarts the level, Weapon_Fire opens Load Game (controls.cpp).
bool PS3_DeathPromptShown(void);
void PS3_DrawDeathPrompt(void);
void PS3_OpenLoadMenu(void);
#ifdef PS3_DEBUG_CHEATS
void PS3_DebugCheats(void);
#endif

// Save pictures: the view drawn by the classic renderer at 320x200 into
// a tile (view.cpp), kept next to the save (gameNNNN.pic) and shown in
// the Load / Save menus.
void PS3_SaveShot(int nSlot);
void PS3_LoadSavePictures(void);
int  PS3_SavePicTile(int nSlot);   // -1: none
void viewPS3SaveShot(int nTile);

// HELP: the DualShock 3's buttons (menu.cpp's menuOrder).
CGameMenuItem *PS3_HelpItem(void);

#endif
