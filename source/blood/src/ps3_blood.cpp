// NBlood-PS3: the PS3 port's pieces of the game: the DualShock 3's button
// names, the death prompt, the save pictures and the HELP page.
//
// Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later, as
// the rest of NBlood.

#ifdef __PPU__

#include "compat.h"
#include "build.h"
#include "mmulti.h"
#include "control.h"
#include "function.h"
#include "common_game.h"
#include "blood.h"
#include "config.h"
#include "demo.h"
#include "gamemenu.h"
#include "globals.h"
#include "levels.h"
#include "menu.h"
#include "player.h"
#include "tile.h"
#include "view.h"
#include "ps3_blood.h"

// --------------------------------------------------------------------
// Buttons

// ps3layer.cpp's order (SDL's game controller), then L2 / R2: the
// triggers are the axes 4 / 5, their digital "+" (JoystickDigitalFunctions
// [axis][1]) the button.
static const char *const s_buttonNames[] =
{
    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE",
    "SELECT", "PS", "START",
    "L3", "R3", "L1", "R1",
    "UP", "DOWN", "LEFT", "RIGHT",
    "L2", "R2",
};

const char *PS3_ButtonName(int nButton)
{
    if ((unsigned)nButton >= ARRAY_SIZE(s_buttonNames))
        return "";
    return s_buttonNames[nButton];
}

static int PS3_ButtonFunction(int nButton)
{
    if (nButton == 15)
        return JoystickDigitalFunctions[4][1];
    if (nButton == 16)
        return JoystickDigitalFunctions[5][1];
    return JoystickFunctions[nButton][0];
}

const char *PS3_FunctionButton(int nFunction)
{
    // the triggers first: the usual place of firing
    if (JoystickDigitalFunctions[5][1] == nFunction)
        return "R2";
    if (JoystickDigitalFunctions[4][1] == nFunction)
        return "L2";
    for (int i = 0; i < 15; i++)
        if (JoystickFunctions[i][0] == nFunction)
            return s_buttonNames[i];
    return NULL;
}

// "Previous_Weapon" -> "PREVIOUS WEAPON"
static void PS3_FunctionText(int nFunction, char *pBuffer, int nSize)
{
    if ((unsigned)nFunction >= NUMGAMEFUNCTIONS || !gamefunctions[nFunction][0])
    {
        Bstrncpyz(pBuffer, "-", nSize);
        return;
    }
    int i = 0;
    for (char const *p = gamefunctions[nFunction]; *p && i < nSize - 1; p++)
        pBuffer[i++] = (*p == '_') ? ' ' : Btoupper(*p);
    pBuffer[i] = 0;
}

// --------------------------------------------------------------------
// Dead: restart or load

bool PS3_DeathPromptShown(void)
{
    return gGameStarted && gGameOptions.nGameType == kGameTypeSinglePlayer && numplayers == 1
        && gMe && gMe->pXSprite && gMe->pXSprite->health == 0 && gMe->deathTime > 90
        && !gDemo.at1 && !gGameMenuMgr.m_bActive && gView == gMe;
}

void PS3_DrawDeathPrompt(void)
{
    if (!PS3_DeathPromptShown())
        return;

    char buffer[64];
    const char *pOpen = PS3_FunctionButton(gamefunc_Open);
    const char *pFire = PS3_FunctionButton(gamefunc_Weapon_Fire);

    Bsnprintf(buffer, sizeof(buffer), "%s: RESTART LEVEL", pOpen ? pOpen : "OPEN");
    viewDrawText(3, buffer, 160, 110, -128, 0, 1, 1);
    if (pFire)
    {
        Bsnprintf(buffer, sizeof(buffer), "%s: LOAD GAME", pFire);
        viewDrawText(3, buffer, 160, 120, -128, 0, 1, 1);
    }
}

void PS3_OpenLoadMenu(void)
{
    if (!gGameMenuMgr.m_bActive)
        gGameMenuMgr.Push(&menuLoadGame, -1);
}

#ifdef PS3_DEBUG_CHEATS
// --------------------------------------------------------------------
// The debug build (make -C ps3 DEBUG=1), for testing: always immortal;
// every weapon (when the player starts) and infinite ammo; all the keys;
// L3 + R3 together ends the level (as CALGON does). Each frame, from
// ctrlGetInput.

extern "C" int32_t PS3_PadHeldBits(void);   // ps3layer.cpp

void PS3_DebugCheats(void)
{
    static int  nGivenTo = -1;
    static bool bSkipHeld;

    if (!gGameStarted || gGameOptions.nGameType != kGameTypeSinglePlayer || numplayers != 1
        || !gMe || !gMe->pXSprite || gDemo.at1 || gGameMenuMgr.m_bActive)
        return;

    if (gMe->pXSprite->health <= 0)
    {
        nGivenTo = -1;
        return;
    }

    if (!gMe->godMode)
        playerSetGodMode(gMe, 1);

    gInfiniteAmmo = true;

    // the player's sprite changes with each level (and life): the
    // weapons again then
    if (nGivenTo != gMe->nSprite)
    {
        nGivenTo = gMe->nSprite;

        for (int i = 0; i < kWeaponMax; i++)
            gMe->hasWeapon[i] = 1;
        for (int i = 0; i < 12; i++)
            gMe->ammoCount[i] = gAmmoInfo[i].max;
    }

    for (int i = 1; i <= 6; i++)
        gMe->hasKey[i] = 1;

    int32_t const nHeld = PS3_PadHeldBits();
    bool const bSkip = (nHeld & (1 << CONTROLLER_BUTTON_LEFTSTICK)) && (nHeld & (1 << CONTROLLER_BUTTON_RIGHTSTICK));

    if (bSkip && !bSkipHeld)
        levelEndLevel(kLevelExitNormal);

    bSkipHeld = bSkip;
}
#endif

// --------------------------------------------------------------------
// Save pictures

// High above anything the game or nblood.pk3's defs use (they stop near
// 9300): one per slot.
enum { kSavePicTile0 = kMaxTiles - 10 };
enum { kSavePicW = 200, kSavePicH = 320, kSavePicSize = kSavePicW * kSavePicH };
static const char s_savePicMagic[4] = { 'B', 'P', 'I', 'C' };
static bool s_savePicLoaded[10];

int PS3_SavePicTile(int nSlot)
{
    if ((unsigned)nSlot >= 10 || !s_savePicLoaded[nSlot])
        return -1;
    return kSavePicTile0 + nSlot;
}

static bool PS3_SavePicName(int nSlot, char *pBuffer)
{
    return !G_ModDirSnprintf(pBuffer, BMAX_PATH, "game00%02d.pic", nSlot);
}

void PS3_SaveShot(int nSlot)
{
    if ((unsigned)nSlot >= 10 || !gView)
        return;

    int const nTile = kSavePicTile0 + nSlot;

    viewPS3SaveShot(nTile);
    if (!waloff[nTile])
        return;
    tileInvalidate(nTile, -1, -1);
    s_savePicLoaded[nSlot] = true;

    char name[BMAX_PATH];
    if (!PS3_SavePicName(nSlot, name))
        return;
    FILE *f = fopen(name, "wb");
    if (!f)
        return;
    fwrite(s_savePicMagic, 1, sizeof(s_savePicMagic), f);
    fwrite((void *)waloff[nTile], 1, kSavePicSize, f);
    fclose(f);
}

void PS3_LoadSavePictures(void)
{
    int const bakpathsearchmode = pathsearchmode;
    pathsearchmode = 1;

    for (int nSlot = 0; nSlot < 10; nSlot++)
    {
        s_savePicLoaded[nSlot] = false;

        char name[BMAX_PATH];
        if (!PS3_SavePicName(nSlot, name))
            continue;
        int const hFile = kopen4loadfrommod(name, 0);
        if (hFile == -1)
            continue;

        char magic[4];
        int const nTile = kSavePicTile0 + nSlot;
        if (kread(hFile, magic, 4) == 4 && !memcmp(magic, s_savePicMagic, 4) && kfilelength(hFile) == 4 + kSavePicSize)
        {
            char *pData = waloff[nTile] ? (char *)waloff[nTile] : tileAllocTile(nTile, kSavePicW, kSavePicH, 0, 0);
            if (pData && kread(hFile, pData, kSavePicSize) == kSavePicSize)
            {
                tileInvalidate(nTile, -1, -1);
                s_savePicLoaded[nSlot] = true;
            }
        }
        kclose(hFile);
    }

    pathsearchmode = bakpathsearchmode;
}

// --------------------------------------------------------------------
// HELP: the DualShock 3's buttons, as they are bound now

class CGameMenuItemPS3Help : public CGameMenuItem
{
public:
    CGameMenuItemPS3Help() { bCanSelect = 1; bEnable = 1; }
    virtual void Draw(void);
    virtual bool Event(CGameMenuEvent &event);
};

static void PS3_HelpRow(int nX, int nY, const char *pButton, const char *pText)
{
    viewDrawText(3, pButton, nX, nY, -128, 2, 0, 1);
    viewDrawText(3, pText, nX + 52, nY, -128, 0, 0, 1);
}

static const char *PS3_StickText(int nAxis)
{
    switch (JoystickAnalogueAxes[nAxis])
    {
    case analog_turning: return "TURN";
    case analog_strafing: return "STRAFE";
    case analog_moving: return "MOVE";
    case analog_lookingupanddown: return "LOOK";
    }
    return "-";
}

void CGameMenuItemPS3Help::Draw(void)
{
    viewLoadingScreenWide();

    // the buttons in two columns, then the sticks
    static const int nLeft[] = { 0, 1, 2, 3, 9, 10, 15, 16, 7 };
    static const int nRight[] = { 8, 11, 12, 13, 14, 4 };
    char buffer[MAXGAMEFUNCLEN];

    int y = 42;
    for (int nButton : nLeft)
    {
        PS3_FunctionText(PS3_ButtonFunction(nButton), buffer, sizeof(buffer));
        PS3_HelpRow(14, y, s_buttonNames[nButton], buffer);
        y += 10;
    }

    y = 42;
    for (int nButton : nRight)
    {
        PS3_FunctionText(PS3_ButtonFunction(nButton), buffer, sizeof(buffer));
        PS3_HelpRow(164, y, s_buttonNames[nButton], buffer);
        y += 10;
    }
    PS3_HelpRow(164, y, "START", "MENU");
    y += 10;

    char sticks[32];
    Bsnprintf(sticks, sizeof(sticks), "%s / %s", PS3_StickText(0), PS3_StickText(1));
    PS3_HelpRow(164, y, "L STICK", sticks);
    y += 10;
    Bsnprintf(sticks, sizeof(sticks), "%s / %s", PS3_StickText(2), PS3_StickText(3));
    PS3_HelpRow(164, y, "R STICK", sticks);

    viewDrawText(3, "CHANGE THEM IN OPTIONS, CONTROL SETUP", 160, 150, -128, 0, 1, 1);
}

bool CGameMenuItemPS3Help::Event(CGameMenuEvent &event)
{
    switch (event.at0)
    {
    case kMenuEventEscape:
    case kMenuEventEnter:
        return true;
    }
    return false;
}

CGameMenuItem *PS3_HelpItem(void)
{
    static CGameMenuItemPS3Help item;
    return &item;
}

#endif
