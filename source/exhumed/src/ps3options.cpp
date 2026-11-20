//-------------------------------------------------------------------------
/*
Copyright (C) 2026 the NBlood-PS3 contributors

This file is part of PCExhumed (NBlood-PS3).

PCExhumed is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.
*/
//-------------------------------------------------------------------------

// NBlood-PS3: PCExhumed's OPTIONS on the PS3. PowerSlave's menu has only
// VOLUME (music and sound); with no keyboard for the console, OPTIONS is
// in its place (menu.cpp), with what NBlood-PS3's options have that the
// game uses: game, display, video mode, sound and the DualShock 3's
// buttons, sticks and vibration. The pages are drawn as the game's own
// screens are (the plasma, its font, the ankh); the d-pad moves, Cross
// (Return) enters / changes, Circle or START (Escape) goes back. The
// settings are written to the .cfg (and the cvars' cfg) on the way out.

#ifdef __PPU__

#include "compat.h"
#include "build.h"
#include "baselayer.h"
#ifdef USE_OPENGL
# include "polymost.h"
#endif
#include "exhumed.h"
#include "typedefs.h"
#include "player.h"
#include "sequence.h"
#include "menu.h"
#include "names.h"
#include "engine.h"
#include "keyboard.h"
#include "control.h"
#include "joystick.h"
#include "status.h"
#include "sound.h"
#include "light.h"
#include "view.h"
#include "cd.h"
#include "config.h"

// ------------------------------------------------------------------------
// the text: the game's font (kSeqFont2, myprintext's), any character it
// doesn't have as a space

static int FontPic(char c)
{
    int const n = Btoupper((unsigned char)c) - 32;

    if (n <= 0 || n >= SeqSize[SeqOffsets[kSeqFont2]])
        return -1;

    return seq_GetSeqPicnum(kSeqFont2, 0, n);
}

static int TextWidth(const char *s)
{
    int w = 0;

    for (; *s; s++)
    {
        int const p = FontPic(*s);
        w += (p < 0) ? 4 : tilesiz[p].x + 1;
    }

    return w;
}

static int TextHeight(void)
{
    int const p = FontPic('A');
    return (p < 0) ? 8 : tilesiz[p].y;
}

static void Text(int x, int y, const char *s, int shade)
{
    for (; *s; s++)
    {
        int const p = FontPic(*s);

        if (p < 0)
        {
            x += 4;
            continue;
        }

        overwritesprite(x, y, p, shade, 2, kPalNormal);
        x += tilesiz[p].x + 1;
    }
}

static void TextCentered(int y, const char *s, int shade)
{
    Text((320 - TextWidth(s)) / 2, y, s, shade);
}

// ------------------------------------------------------------------------
// OPTIONS on the main menu: a tile of the size of VOLUME's, with the
// word in the font made as tall as it fits (made again if the cache
// lets it go)

enum { kTilePS3Options = MAXUSERTILES - 16 };

int PS3_OptionsTile(void)
{
    static const char word[] = "OPTIONS";
    int const nVolume = kMenuNewGameTile + 3;   // VOLUME (menu.cpp, kMenuVolume)
    int const nTile = kTilePS3Options;

    if (waloff[nTile])
        return nTile;

    int const w = tilesiz[nVolume].x > 0 ? tilesiz[nVolume].x : 140;
    int const h = tilesiz[nVolume].y > 0 ? tilesiz[nVolume].y : 20;

    if (!tileCreate(nTile, w, h))
        return nVolume;

    uint8_t *const pDst = (uint8_t *)waloff[nTile];
    Bmemset(pDst, 255, w * h);

    int const tw = TextWidth(word) - 1;
    int const th = TextHeight();

    // the scale (16.16): the height, unless the width runs out first; at
    // most 3 times the font
    int scale = divscale16(h, th);
    scale = min(scale, divscale16(w - 4, tw));
    scale = min(scale, 3 << 16);
    scale = max(scale, 1 << 16);

    int x0 = (w - mulscale16(tw, scale)) / 2;
    int const y0 = (h - mulscale16(th, scale)) / 2;

    for (char const *c = word; *c; c++)
    {
        int const p = FontPic(*c);

        if (p < 0)
            continue;

        if (!waloff[p])
            tileLoad(p);

        uint8_t const *const pSrc = (uint8_t const *)waloff[p];
        int const sw = tilesiz[p].x, sh = tilesiz[p].y;
        int const dw = mulscale16(sw, scale), dh = mulscale16(sh, scale);

        if (pSrc)
        {
            for (int dx = 0; dx < dw; dx++)
            {
                int const x = x0 + dx;
                int const sx = divscale16(dx, scale);

                if (x < 0 || x >= w || sx >= sw)
                    continue;

                for (int dy = 0; dy < dh; dy++)
                {
                    int const y = y0 + dy;
                    int const sy = divscale16(dy, scale);

                    if (y < 0 || y >= h || sy >= sh)
                        continue;

                    uint8_t const v = pSrc[sx * sh + sy];

                    if (v != 255)
                        pDst[x * h + y] = v;
                }
            }
        }

        x0 += mulscale16(sw + 1, scale);
    }

    tileInvalidate(nTile, -1, -1);
    return nTile;
}

// ------------------------------------------------------------------------
// the vibration (cvar in_rumble): the DualShock 3 shakes when the player
// is hurt, harder the more health went, and while the view shakes (the
// guns' kick, an explosion, the ground). ps3layer.cpp drives the motors
// (large motor = rumbleLow, the small one from rumbleHigh 0x4000), as
// NBlood-PS3's and Rednukem-PS3's. From GetLocalInput, every tic played.

int32_t ps3_rumble = 1;

void PS3_Rumble(void)
{
    static int lastHealth = -1;
    static int lastSprite = -1;
    Player const &p = PlayerList[nLocalPlayer];
    int const health = p.nHealth;
    int strength = 0;

    if (bInDemo || p.nSprite != lastSprite)
    {
        lastSprite = p.nSprite;
        lastHealth = health;
        return;
    }

    if (lastHealth > 0 && health < lastHealth)
        strength = (lastHealth - health) * 3 / 2;   // health is out of 800 here

    lastHealth = health;

    int const quake = klabs(nQuake[nLocalPlayer]);

    if (quake > 0)
        strength = max(strength, min(quake >> 2, 160));

    if (strength <= 0 || !ps3_rumble || !CONTROL_JoystickEnabled || !joystick.hasRumble)
        return;

    strength = min(strength, 600);

    joystick.rumbleLow  = max<int>(joystick.rumbleLow, min(0x7000 + strength * 0x90, 0xFFFF));
    joystick.rumbleHigh = max<int>(joystick.rumbleHigh, strength >= 64 ? 0x8000 : 0);
    joystick.rumbleTime = max<int>(joystick.rumbleTime, (quake > 0 && strength < 64) ? 120 : 300 + strength * 500 / 600);
}

// ------------------------------------------------------------------------
// the pages

enum { kItemLink, kItemBool, kItemCycle, kItemRange, kItemAction };

struct OptMenu;

struct OptItem
{
    const char *label;
    int type;
    int arg;
    int (*get)(int arg);
    void (*set)(int arg, int value);
    int min, max, step;
    const char *const *names;     // a cycle's (names[value - min])
    const OptMenu *child;         // a link's
    void (*run)(void);            // an action's
    const char *hint;             // on the bottom line, while selected
};

struct OptMenu
{
    const char *title;
    OptItem *items;
    int count;
};

#define ITEM_LINK(l, m)                 { l, kItemLink, 0, NULL, NULL, 0, 0, 0, NULL, &m, NULL, NULL }
#define ITEM_BOOL(l, a, g, s, h)        { l, kItemBool, a, g, s, 0, 1, 1, NULL, NULL, NULL, h }
#define ITEM_CYCLE(l, a, g, s, n, h)    { l, kItemCycle, a, g, s, 0, ARRAY_SSIZE(n) - 1, 1, n, NULL, NULL, h }
#define ITEM_RANGE(l, a, g, s, mn, mx, st, h) { l, kItemRange, a, g, s, mn, mx, st, NULL, NULL, NULL, h }
#define ITEM_ACTION(l, r, h)            { l, kItemAction, 0, NULL, NULL, 0, 0, 0, NULL, NULL, r, h }

static const char *const pzOffOn[] = { "OFF", "ON" };

static char sMessage[64];
static int nMessageClock;

static void Message(const char *s)
{
    Bstrncpyz(sMessage, s, sizeof(sMessage));
    nMessageClock = (int)totalclock + 2 * 120;
}

// --- GAME SETUP

static const char *const pzRunMode[] = { "CLASSIC", "MODERN" };

static OptItem itemsGame[] =
{
    ITEM_BOOL("AUTO RUN", 0, [](int) { return auto_run ? 1 : 0; }, [](int, int v) { auto_run = v; }, NULL),
    ITEM_CYCLE("RUN BUTTON", 0, [](int) { return runkey_mode ? 1 : 0; }, [](int, int v) { runkey_mode = v; }, pzRunMode,
        "CLASSIC SWITCHES AUTO RUN"),
};

static OptMenu menuGame = { "GAME SETUP", itemsGame, ARRAY_SSIZE(itemsGame) };

// --- DISPLAY SETUP / VIDEO MODE

static const char *const pzFrameLimit[] = { "60 FPS", "30 FPS" };
#ifdef USE_OPENGL
static const char *const pzTexFilter[] = { "NONE", "BILINEAR", "MIPMAPS", "BILIN MIP", "SMOOTH", "TRILINEAR" };
#endif

static int GetScreenSize(int)
{
    return bFullScreen ? 15 : 14 - clamp(screensize, 0, 14);
}

static void SetScreenSize(int, int v)
{
    if (v >= 15)
    {
        screensize = 0;

        if (!bFullScreen)
        {
            bFullScreen = kTrue;
            UnMaskStatus();
        }
    }
    else
    {
        bFullScreen = kFalse;
        screensize = 14 - v;
    }

    UpdateScreenSize();
}

static OptItem itemsVideo[] =
{
    ITEM_CYCLE("FPS LIMIT", 0, [](int) { return r_maxfps == 30 ? 1 : 0; },
        [](int, int v) { r_maxfps = v ? 30 : -1; g_frameDelay = calcFrameDelay(r_maxfps); }, pzFrameLimit, NULL),
    ITEM_BOOL("WIDESCREEN", 0, [](int) { return r_usenewaspect ? 1 : 0; }, [](int, int v) { r_usenewaspect = v; }, NULL),
    ITEM_BOOL("SHOW FPS", 0, [](int) { return r_showfps ? 1 : 0; }, [](int, int v) { r_showfps = v; }, NULL),
#ifdef USE_OPENGL
    // as NBlood-PS3's: the art's textures made again for the new filter
    // (the unfiltered ones are the palette-indexed kind), then applied
    ITEM_CYCLE("TEXTURE FILTER", 0, [](int) { return clamp(gltexfiltermode, 0, 5); },
        [](int, int v) { gltexfiltermode = v; gltexinvalidatetype(INVALIDATE_ART); gltexapplyprops(); }, pzTexFilter,
        "FILTERS NEED MORE VIDEO MEMORY"),
#endif
    // ps3layer.cpp gives it to the video every frame
    ITEM_RANGE("TV SCREEN FIT", 0, [](int) { return vid_ps3_screenfit; },
        [](int, int v) { vid_ps3_screenfit = v; }, 70, 100, 1, "HOW MUCH OF THE TV THE PICTURE USES"),
};

static OptMenu menuVideo = { "VIDEO MODE", itemsVideo, ARRAY_SSIZE(itemsVideo) };

static OptItem itemsDisplay[] =
{
    ITEM_RANGE("SCREEN SIZE", 0, GetScreenSize, SetScreenSize, 0, 15, 1, "15 IS FULL SCREEN WITH NO STATUS BAR"),
    ITEM_RANGE("BRIGHTNESS", 0, [](int) { return clamp(nGamma, 0, 4); },
        [](int, int v) { nGamma = v; mysetbrightness((uint8_t)nGamma); }, 0, 4, 1, NULL),
    ITEM_BOOL("CROSSHAIR", 0, [](int) { return gShowCrosshair ? 1 : 0; }, [](int, int v) { gShowCrosshair = v; }, NULL),
    ITEM_RANGE("FIELD OF VIEW", 0, [](int) { return gFov; }, [](int, int v) { gFov = v; }, 60, 140, 5, NULL),
    ITEM_LINK("VIDEO MODE", menuVideo),
};

static OptMenu menuDisplay = { "DISPLAY SETUP", itemsDisplay, ARRAY_SSIZE(itemsDisplay) };

// --- SOUND SETUP (the volumes in steps of 5%, 0..255 underneath)

static int PercentFromVolume(int v) { return (clamp(v, 0, 255) * 100 + 127) / 255; }
static int VolumeFromPercent(int p) { return clamp((p * 255 + 50) / 100, 0, 255); }

static bool bFXVolumeChanged;

static OptItem itemsSound[] =
{
    ITEM_BOOL("SOUND", 0, [](int) { return SoundToggle ? 1 : 0; },
        [](int, int v) { SoundToggle = v; if (!v) StopAllSounds(); }, NULL),
    ITEM_BOOL("MUSIC", 0, [](int) { return MusicToggle ? 1 : 0; },
        [](int, int v) { MusicToggle = v; PS3_MusicToggled(); }, NULL),
    ITEM_RANGE("SOUND VOLUME", 0, [](int) { return PercentFromVolume(FXVolume); },
        [](int, int v) { FXVolume = VolumeFromPercent(v); SetMasterFXVolume(FXVolume); bFXVolumeChanged = true; },
        0, 100, 5, NULL),
    ITEM_RANGE("MUSIC VOLUME", 0, [](int) { return PercentFromVolume(MusicVolume); },
        [](int, int v) { MusicVolume = VolumeFromPercent(v); setCDaudiovolume(MusicVolume); }, 0, 100, 5, NULL),
};

static OptMenu menuSound = { "SOUND SETUP", itemsSound, ARRAY_SSIZE(itemsSound) };

// --- CONTROL SETUP: the buttons (START is the menu, PS the XMB); L2 / R2
// are the triggers' axes, their "digital +" (config.cpp's defaults)

enum { kAxisButton = 100 };   // arg: a button, or kAxisButton + an axis

static const char *pzFuncNames[kMaxGameFunctions + 1];
static int nFuncValues[kMaxGameFunctions + 1];
static int nFuncCount;
static char sFuncNames[kMaxGameFunctions][kMaxGameFuncLen];

static void SetupFuncNames(void)
{
    if (nFuncCount)
        return;

    pzFuncNames[0] = "NONE";
    nFuncValues[0] = -1;
    nFuncCount = 1;

    for (int i = 0; i < kMaxGameFunctions; i++)
    {
        // nothing that needs a keyboard, a mouse or the console
        if (i == gamefunc_Strafe || i == gamefunc_SendMessage || i == gamefunc_Escape
            || i == gamefunc_Show_Console || i == gamefunc_Mouse_Aiming || gamefunctions[i][0] == '\0')
            continue;

        for (int j = 0; j < kMaxGameFuncLen; j++)
        {
            char const c = gamefunctions[i][j];
            sFuncNames[i][j] = (c == '_') ? ' ' : Btoupper(c);
        }

        pzFuncNames[nFuncCount] = sFuncNames[i];
        nFuncValues[nFuncCount] = i;
        nFuncCount++;
    }
}

static int FuncIndex(int nFunc)
{
    for (int i = 0; i < nFuncCount; i++)
        if (nFuncValues[i] == nFunc)
            return i;

    return 0;
}

static int GetButton(int arg)
{
    if (arg >= kAxisButton)
        return FuncIndex(JoystickDigitalFunctions[arg - kAxisButton][1]);

    return FuncIndex(JoystickFunctions[arg][0]);
}

static void SetButton(int arg, int v)
{
    int const nFunc = nFuncValues[clamp(v, 0, nFuncCount - 1)];

    if (arg >= kAxisButton)
    {
        JoystickDigitalFunctions[arg - kAxisButton][1] = nFunc;
        CONTROL_MapDigitalAxis(arg - kAxisButton, nFunc, 1);
    }
    else
    {
        JoystickFunctions[arg][0] = nFunc;
        CONTROL_MapButton(nFunc, arg, 0, controldevice_joystick);
    }
}

#define ITEM_BUTTON(l, a) { l, kItemCycle, a, GetButton, SetButton, 0, 0, 1, pzFuncNames, NULL, NULL, NULL }

static OptItem itemsButtons[] =
{
    ITEM_BUTTON("CROSS",    CONTROLLER_BUTTON_A),
    ITEM_BUTTON("CIRCLE",   CONTROLLER_BUTTON_B),
    ITEM_BUTTON("SQUARE",   CONTROLLER_BUTTON_X),
    ITEM_BUTTON("TRIANGLE", CONTROLLER_BUTTON_Y),
    ITEM_BUTTON("L1",       CONTROLLER_BUTTON_LEFTSHOULDER),
    ITEM_BUTTON("R1",       CONTROLLER_BUTTON_RIGHTSHOULDER),
    ITEM_BUTTON("L2",       kAxisButton + CONTROLLER_AXIS_TRIGGERLEFT),
    ITEM_BUTTON("R2",       kAxisButton + CONTROLLER_AXIS_TRIGGERRIGHT),
    ITEM_BUTTON("L3",       CONTROLLER_BUTTON_LEFTSTICK),
    ITEM_BUTTON("R3",       CONTROLLER_BUTTON_RIGHTSTICK),
    ITEM_BUTTON("SELECT",   CONTROLLER_BUTTON_BACK),
    ITEM_BUTTON("UP",       CONTROLLER_BUTTON_DPAD_UP),
    ITEM_BUTTON("DOWN",     CONTROLLER_BUTTON_DPAD_DOWN),
    ITEM_BUTTON("LEFT",     CONTROLLER_BUTTON_DPAD_LEFT),
    ITEM_BUTTON("RIGHT",    CONTROLLER_BUTTON_DPAD_RIGHT),
};

static OptMenu menuButtons = { "BUTTON SETUP", itemsButtons, ARRAY_SSIZE(itemsButtons) };

// --- the sticks, one page per axis (arg: the axis)

static const char *const pzAnalog[] = { "NONE", "TURNING", "STRAFING", "MOVING", "LOOKING" };
static const int nAnalogValues[] = { -1, analog_turning, analog_strafing, analog_moving, analog_lookingupanddown };

static void ApplyAxis(int a)
{
    CONTROL_MapAnalogAxis(a, JoystickAnalogueAxes[a]);
    CONTROL_SetAnalogAxisScale(a, JoystickAnalogueScale[a], controldevice_joystick);
    CONTROL_SetAnalogAxisInvert(a, JoystickAnalogueInvert[a]);

    if (a < joystick.numAxes)
        JOYSTICK_SetDeadZone(a, JoystickAnalogueDead[a], JoystickAnalogueSaturate[a]);
}

static int GetAnalog(int a)
{
    for (int i = 0; i < ARRAY_SSIZE(nAnalogValues); i++)
        if (nAnalogValues[i] == JoystickAnalogueAxes[a])
            return i;

    return 0;
}

// the scale in 16.16 (65536 = 100%), shown in %; the dead zone and the
// saturation in mact's units (out of 10000, JOYSTICK_SetDeadZone), in %
#define AXIS_ITEMS(a) \
    { "FUNCTION", kItemCycle, a, GetAnalog, \
        [](int x, int v) { JoystickAnalogueAxes[x] = nAnalogValues[v]; ApplyAxis(x); }, \
        0, ARRAY_SSIZE(pzAnalog) - 1, 1, pzAnalog, NULL, NULL, NULL }, \
    ITEM_RANGE("SENSITIVITY", a, [](int x) { return (JoystickAnalogueScale[x] * 100 + 32768) >> 16; }, \
        [](int x, int v) { JoystickAnalogueScale[x] = (v << 16) / 100; ApplyAxis(x); }, 25, 400, 25, NULL), \
    ITEM_BOOL("INVERT", a, [](int x) { return JoystickAnalogueInvert[x] ? 1 : 0; }, \
        [](int x, int v) { JoystickAnalogueInvert[x] = v; ApplyAxis(x); }, NULL), \
    ITEM_RANGE("DEAD ZONE", a, [](int x) { return (JoystickAnalogueDead[x] + 50) / 100; }, \
        [](int x, int v) { JoystickAnalogueDead[x] = v * 100; ApplyAxis(x); }, 0, 50, 5, NULL), \
    ITEM_RANGE("SATURATE", a, [](int x) { return (JoystickAnalogueSaturate[x] + 50) / 100; }, \
        [](int x, int v) { JoystickAnalogueSaturate[x] = v * 100; ApplyAxis(x); }, 50, 100, 5, \
        "WHERE THE STICK IS ALL THE WAY") \

static OptItem itemsAxis0[] = { AXIS_ITEMS(CONTROLLER_AXIS_LEFTX) };
static OptItem itemsAxis1[] = { AXIS_ITEMS(CONTROLLER_AXIS_LEFTY) };
static OptItem itemsAxis2[] = { AXIS_ITEMS(CONTROLLER_AXIS_RIGHTX) };
static OptItem itemsAxis3[] = { AXIS_ITEMS(CONTROLLER_AXIS_RIGHTY) };

static OptMenu menuAxis0 = { "LEFT STICK X", itemsAxis0, ARRAY_SSIZE(itemsAxis0) };
static OptMenu menuAxis1 = { "LEFT STICK Y", itemsAxis1, ARRAY_SSIZE(itemsAxis1) };
static OptMenu menuAxis2 = { "RIGHT STICK X", itemsAxis2, ARRAY_SSIZE(itemsAxis2) };
static OptMenu menuAxis3 = { "RIGHT STICK Y", itemsAxis3, ARRAY_SSIZE(itemsAxis3) };

static OptItem itemsSticks[] =
{
    ITEM_LINK("LEFT STICK X", menuAxis0),
    ITEM_LINK("LEFT STICK Y", menuAxis1),
    ITEM_LINK("RIGHT STICK X", menuAxis2),
    ITEM_LINK("RIGHT STICK Y", menuAxis3),
};

static OptMenu menuSticks = { "STICK SETUP", itemsSticks, ARRAY_SSIZE(itemsSticks) };

static OptItem itemsControl[] =
{
    ITEM_LINK("BUTTON SETUP", menuButtons),
    ITEM_LINK("STICK SETUP", menuSticks),
    ITEM_BOOL("VIBRATION", 0, [](int) { return ps3_rumble ? 1 : 0; }, [](int, int v) { ps3_rumble = v; },
        "THE CONTROLLER SHAKES WHEN YOU ARE HURT"),
    ITEM_ACTION("RESET CONTROLLER", [] { CONFIG_PS3_ResetController(); Message("THE DEFAULT BUTTONS AND STICKS"); },
        "BACK TO THE DEFAULT BUTTONS AND STICKS"),
};

static OptMenu menuControl = { "CONTROL SETUP", itemsControl, ARRAY_SSIZE(itemsControl) };

// --- OPTIONS

static OptItem itemsOptions[] =
{
    ITEM_LINK("GAME SETUP", menuGame),
    ITEM_LINK("DISPLAY SETUP", menuDisplay),
    ITEM_LINK("SOUND SETUP", menuSound),
    ITEM_LINK("CONTROL SETUP", menuControl),
};

static OptMenu menuOptions = { "OPTIONS", itemsOptions, ARRAY_SSIZE(itemsOptions) };

// ------------------------------------------------------------------------
// the loop

static int ItemMax(OptItem const &it)
{
    return (it.names == pzFuncNames) ? nFuncCount - 1 : it.max;
}

// a press, then again every 1/15 s after 1/3 s held: 1 the press, 2 a repeat
static int KeyRepeat(int sc)
{
    static int nNext[256];
    static bool bHeld[256];
    int const now = (int)totalclock;

    if (!KB_KeyDown[sc])
    {
        bHeld[sc] = false;
        return 0;
    }

    if (!bHeld[sc])
    {
        bHeld[sc] = true;
        nNext[sc] = now + 40;
        return 1;
    }

    if (now - nNext[sc] >= 0)
    {
        nNext[sc] = now + 8;
        return 2;
    }

    return 0;
}

static void ValueText(OptItem const &it, char *buf, int size)
{
    int const v = it.get(it.arg);

    switch (it.type)
    {
    case kItemBool:
        Bstrncpyz(buf, pzOffOn[v ? 1 : 0], size);
        break;
    case kItemCycle:
        Bstrncpyz(buf, it.names[clamp(v, it.min, ItemMax(it)) - it.min], size);
        break;
    case kItemRange:
        Bsnprintf(buf, size, "%d", v);
        break;
    default:
        buf[0] = '\0';
        break;
    }
}

static void Draw(OptMenu const &m, int nSel, int nTop, int nRows, int nLineH, int y0)
{
    menu_DoPlasma();

    int const shadeSel = Sin((int)totalclock << 4) >> 9;

    TextCentered(y0 - nLineH - 4, m.title, 0);

    for (int i = nTop; i < m.count && i < nTop + nRows; i++)
    {
        OptItem const &it = m.items[i];
        int const y = y0 + (i - nTop) * nLineH;
        int const shade = (i == nSel) ? shadeSel : 8;
        char val[64];

        Text(48, y, it.label, shade);
        ValueText(it, val, sizeof(val));

        if (val[0])
            Text(276 - TextWidth(val), y, val, shade);
    }

    // the ankh by the selected line, as tall as a line
    {
        int const nCursor = kMenuCursorTile;
        int const h = max<int>(tilesiz[nCursor].y, 1);
        int const zoom = divscale16(nLineH, h);
        int const w = mulscale16(tilesiz[nCursor].x, zoom);
        uint8_t const animbak = picanm[nCursor].sf;

        picanm[nCursor].sf = 0;
        rotatesprite((44 - w) << 16, (y0 + (nSel - nTop) * nLineH - 1) << 16, zoom, 0, nCursor, 0, kPalNormal,
                     2 | 16, 0, 0, xdim - 1, ydim - 1);
        picanm[nCursor].sf = animbak;
    }

    // more above / below
    if (nTop > 0)
        TextCentered(y0 - 6, "...", 16);
    if (nTop + nRows < m.count)
        TextCentered(y0 + nRows * nLineH - 2, "...", 16);

    // the bottom line: a message, or the selected line's hint; the
    // filter's warning while one is on (VIDEO MODE)
    char const *pBottom = NULL;

    if (sMessage[0] && (int)totalclock - nMessageClock < 0)
        pBottom = sMessage;
    else if (m.items[nSel].hint)
        pBottom = m.items[nSel].hint;
#ifdef USE_OPENGL
    else if (&m == &menuVideo && gltexfiltermode != 0)
        pBottom = "FILTERS NEED MORE VIDEO MEMORY";
#endif

    if (pBottom)
        TextCentered(190, pBottom, 4);

    videoNextPage();
}

static void RunMenu(OptMenu const &m)
{
    int const nLineH = max(TextHeight() + 2, 9);
    int const y0 = 92;
    int const nRows = (184 - y0) / nLineH;
    int nSel = 0, nTop = 0;

    KB_ClearKeysDown();
    KB_FlushKeyboardQueue();

    while (1)
    {
        HandleAsync();

        if (nSel < nTop)
            nTop = nSel;
        if (nSel >= nTop + nRows)
            nTop = nSel - nRows + 1;

        Draw(m, nSel, nTop, nRows, nLineH, y0);

        if (KB_KeyDown[sc_Escape])
        {
            KB_KeyDown[sc_Escape] = 0;
            PlayLocalSound(StaticSound[kSoundSwitchFoot], 0);
            KB_FlushKeyboardQueue();
            return;
        }

        if (KeyRepeat(sc_UpArrow))
        {
            nSel = (nSel > 0) ? nSel - 1 : m.count - 1;
            PlayLocalSound(StaticSound[kSoundSwitchWtr1], 0);
        }

        if (KeyRepeat(sc_DownArrow))
        {
            nSel = (nSel < m.count - 1) ? nSel + 1 : 0;
            PlayLocalSound(StaticSound[kSoundSwitchWtr1], 0);
        }

        OptItem &it = m.items[nSel];
        int nDir = 0;

        int const nLeft = KeyRepeat(sc_LeftArrow);
        int const nRight = KeyRepeat(sc_RightArrow);

        // a bool changes once a press; the rest repeat while held
        if (nLeft && (nLeft == 1 || it.type != kItemBool))
            nDir = -1;
        else if (nRight && (nRight == 1 || it.type != kItemBool))
            nDir = 1;

        bool const bEnter = KB_KeyDown[sc_Return] || KB_KeyDown[sc_Space];

        if (bEnter)
        {
            KB_KeyDown[sc_Return] = 0;
            KB_KeyDown[sc_Space] = 0;

            if (it.type == kItemLink)
            {
                PlayLocalSound(StaticSound[kSoundSwitchFoot], 0);
                RunMenu(*it.child);
                KB_ClearKeysDown();
                continue;
            }

            if (it.type == kItemAction)
            {
                PlayLocalSound(StaticSound[kSoundSwitchFoot], 0);
                it.run();
                continue;
            }

            nDir = 1;   // Cross: the next value
        }

        if (nDir && it.set)
        {
            int const nMax = ItemMax(it);
            int const nOld = it.get(it.arg);
            int v = nOld + nDir * it.step;

            if (it.type == kItemRange)
                v = clamp(v, it.min, nMax);   // a range stops at its ends
            else if (v > nMax)
                v = it.min;                   // the rest go round
            else if (v < it.min)
                v = nMax;

            if (v != nOld)
            {
                it.set(it.arg, v);

                // the sound volume: a sample to hear it by (as VOLUME did)
                if (bFXVolumeChanged)
                {
                    bFXVolumeChanged = false;

                    if (LocalSoundPlaying())
                        UpdateLocalSound();
                    else
                        PlayLocalSound(StaticSound[kSoundAmbientStone], 0);
                }
                else
                    PlayLocalSound(StaticSound[kSoundSwitchWtr1], 0);
            }
        }
        else if (!nLeft && !nRight && GetLocalSound() == 23)
            StopLocalSound();
    }
}

void PS3_OptionsMenu(void)
{
    SetupFuncNames();

    sMessage[0] = '\0';
    RunMenu(menuOptions);

    if (GetLocalSound() == 23)
        StopLocalSound();

    // the settings kept: the .cfg (the controller's too) and the cvars'
    CONFIG_WriteSetup(2);

    KB_ClearKeysDown();
    KB_FlushKeyboardQueue();
}

#endif // __PPU__
