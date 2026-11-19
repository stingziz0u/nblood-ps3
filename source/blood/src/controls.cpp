//-------------------------------------------------------------------------
/*
Copyright (C) 2010-2019 EDuke32 developers and contributors
Copyright (C) 2019 Nuke.YKT

This file is part of NBlood.

NBlood is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License version 2
as published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
*/
//-------------------------------------------------------------------------
#include "compat.h"
#include "baselayer.h"
#include "keyboard.h"
#include "mouse.h"
#include "joystick.h"
#include "control.h"
#include "function.h"
#include "common_game.h"
#include "blood.h"
#include "config.h"
#include "controls.h"
#include "globals.h"
#include "levels.h"
#include "map2d.h"
#include "view.h"
#ifdef __PPU__
#include "actor.h"
#include "demo.h"
#include "player.h"
#include "ps3_blood.h"
#endif


int32_t ctrlCheckAllInput(void)
{
    return (
        KB_KeyWaiting() ||
        MOUSE_GetButtons() ||
        JOYSTICK_GetButtons()
        );
}

void ctrlClearAllInput(void)
{
    KB_FlushKeyboardQueue();
    KB_FlushKeyboardQueueScans();
    KB_ClearKeysDown();
    MOUSE_ClearAllButtons();
    JOYSTICK_ClearAllButtons();
}

GINPUT gInput, gNetInput;
bool bSilentAim = false;

int32_t GetTime(void)
{
    return (int32_t)totalclock;
}

void ctrlInit(void)
{
    KB_ClearKeysDown();
    KB_FlushKeyboardQueue();
    KB_FlushKeyboardQueueScans();
    CONTROL_Startup(controltype_keyboardandmouse, &GetTime, 120);
    CONFIG_SetupMouse();
    CONFIG_SetupJoystick();

    CONTROL_JoystickEnabled = (gSetup.usejoystick && CONTROL_JoyPresent);
    CONTROL_MouseEnabled = (gSetup.usemouse && CONTROL_MousePresent);

    // JBF 20040215: evil and nasty place to do this, but joysticks are evil and nasty too
    for (int i = 0; i < joystick.numAxes; i++)
        JOYSTICK_SetDeadZone(i, JoystickAnalogueDead[i], JoystickAnalogueSaturate[i]);
    CONTROL_DefineFlag(gamefunc_Move_Forward, false);
    CONTROL_DefineFlag(gamefunc_Move_Backward, false);
    CONTROL_DefineFlag(gamefunc_Turn_Left, false);
    CONTROL_DefineFlag(gamefunc_Turn_Right, false);
    CONTROL_DefineFlag(gamefunc_Turn_Around, false);
    CONTROL_DefineFlag(gamefunc_Strafe, false);
    CONTROL_DefineFlag(gamefunc_Strafe_Left, false);
    CONTROL_DefineFlag(gamefunc_Strafe_Right, false);
    CONTROL_DefineFlag(gamefunc_Jump, false);
    CONTROL_DefineFlag(gamefunc_Crouch, false);
    CONTROL_DefineFlag(gamefunc_Run, false);
    CONTROL_DefineFlag(gamefunc_AutoRun, false);
    CONTROL_DefineFlag(gamefunc_Open, false);
    CONTROL_DefineFlag(gamefunc_Weapon_Fire, false);
    CONTROL_DefineFlag(gamefunc_Weapon_Special_Fire, false);
    CONTROL_DefineFlag(gamefunc_Aim_Up, false);
    CONTROL_DefineFlag(gamefunc_Aim_Down, false);
    CONTROL_DefineFlag(gamefunc_Aim_Center, false);
    CONTROL_DefineFlag(gamefunc_Look_Up, false);
    CONTROL_DefineFlag(gamefunc_Look_Down, false);
    CONTROL_DefineFlag(gamefunc_Tilt_Left, false);
    CONTROL_DefineFlag(gamefunc_Tilt_Right, false);
    CONTROL_DefineFlag(gamefunc_Weapon_1, false);
    CONTROL_DefineFlag(gamefunc_Weapon_2, false);
    CONTROL_DefineFlag(gamefunc_Weapon_3, false);
    CONTROL_DefineFlag(gamefunc_Weapon_4, false);
    CONTROL_DefineFlag(gamefunc_Weapon_5, false);
    CONTROL_DefineFlag(gamefunc_Weapon_6, false);
    CONTROL_DefineFlag(gamefunc_Weapon_7, false);
    CONTROL_DefineFlag(gamefunc_Weapon_8, false);
    CONTROL_DefineFlag(gamefunc_Weapon_9, false);
    CONTROL_DefineFlag(gamefunc_Weapon_10, false);
    CONTROL_DefineFlag(gamefunc_Inventory_Use, false);
    CONTROL_DefineFlag(gamefunc_Inventory_Left, false);
    CONTROL_DefineFlag(gamefunc_Inventory_Right, false);
    CONTROL_DefineFlag(gamefunc_Map_Toggle, false);
    CONTROL_DefineFlag(gamefunc_Map_Follow_Mode, false);
    CONTROL_DefineFlag(gamefunc_Shrink_Screen, false);
    CONTROL_DefineFlag(gamefunc_Enlarge_Screen, false);
    CONTROL_DefineFlag(gamefunc_Send_Message, false);
    CONTROL_DefineFlag(gamefunc_See_Coop_View, false);
    CONTROL_DefineFlag(gamefunc_See_Chase_View, false);
    CONTROL_DefineFlag(gamefunc_Mouse_Aiming, false);
    CONTROL_DefineFlag(gamefunc_Toggle_Crosshair, false);
    CONTROL_DefineFlag(gamefunc_Next_Weapon, false);
    CONTROL_DefineFlag(gamefunc_Previous_Weapon, false);
    CONTROL_DefineFlag(gamefunc_Last_Weapon, false);
    CONTROL_DefineFlag(gamefunc_Holster_Weapon, false);
    CONTROL_DefineFlag(gamefunc_Show_Opponents_Weapon, false);
    CONTROL_DefineFlag(gamefunc_BeastVision, false);
    CONTROL_DefineFlag(gamefunc_CrystalBall, false);
    CONTROL_DefineFlag(gamefunc_JumpBoots, false);
    CONTROL_DefineFlag(gamefunc_MedKit, false);
    CONTROL_DefineFlag(gamefunc_ProximityBombs, false);
    CONTROL_DefineFlag(gamefunc_RemoteBombs, false);
    CONTROL_DefineFlag(gamefunc_Lock_On, false);
}

void ctrlTerm(void)
{
    CONTROL_Shutdown();
}

int32_t mouseyaxismode = -1;

fix16_t gViewLook, gViewAngle;
float gViewAngleAdjust;
float gViewLookAdjust;
int gViewLookRecenter;
int gCrouchToggleState = 0;

#ifdef __PPU__
// Lock-on (gamefunc_Lock_On, held; the PS3 port's, as in EDuke32-PS3): the
// view turns to the nearest enemy in front that can be seen, and stays on
// it while the button is held and the enemy lives. Moving and strafing
// stay free (circling a target).

// the middle of a sprite, height wise
static int ctrlLockOnZ(spritetype const *pSprite)
{
    if (pSprite->cstat & CSTAT_SPRITE_YCENTER)
        return pSprite->z;
    return pSprite->z - ((pSprite->yrepeat * tilesiz[pSprite->picnum].y) << 1);
}

static int ctrlLockOnTarget(PLAYER *pPlayer, int nAngle, int nCurrent)
{
    spritetype *pMe = pPlayer->pSprite;
    int nBest = -1;
    int64_t nBestScore = INT64_MAX;

    for (int nSprite = headspritestat[kStatDude]; nSprite >= 0; nSprite = nextspritestat[nSprite])
    {
        spritetype *pSprite = &sprite[nSprite];

        if (pSprite == pMe || pSprite->extra <= 0 || pSprite->extra >= kMaxXSprites || !IsDudeSprite(pSprite)
            || IsPlayerSprite(pSprite) || (pSprite->cstat & CSTAT_SPRITE_INVISIBLE) || xsprite[pSprite->extra].health <= 0)
            continue;

        int const dx = pSprite->x - pMe->x;
        int const dy = pSprite->y - pMe->y;
        int const nDist = approxDist(dx, dy);

        if (nDist < 128 || nDist > 24576)
            continue;

        // in front: 45 degrees either way (90 for the one already locked)
        int const nAngDiff = klabs(((getangle(dx, dy) - nAngle + 1024) & 2047) - 1024);

        if (nAngDiff > (nSprite == nCurrent ? 512 : 256))
            continue;

        if (!cansee(pMe->x, pMe->y, pPlayer->zView, pMe->sectnum, pSprite->x, pSprite->y, ctrlLockOnZ(pSprite), pSprite->sectnum))
            continue;

        int64_t nScore = (int64_t)nAngDiff * 64 + nDist;

        if (nSprite == nCurrent)
            nScore >>= 2;  // stays on it unless another is much better

        if (nScore < nBestScore)
        {
            nBestScore = nScore;
            nBest = nSprite;
        }
    }

    return nBest;
}

static void ctrlLockOn(GINPUT &input, double dtMs)
{
    static int nTarget = -1;

    if (!BUTTON(gamefunc_Lock_On) || !gMe || gMe->pXSprite->health <= 0 || gView != gMe || gViewMode == 4 || gPaused)
    {
        nTarget = -1;
        return;
    }

    // the angle the view has now: with the turns not yet sent
    fix16_t const q16ang = VanillaMode() ? fix16_sadd(gMe->q16ang, gInput.q16turn) : gViewAngle;

    nTarget = ctrlLockOnTarget(gMe, fix16_to_int(q16ang) & 2047, nTarget);

    if (nTarget < 0)
        return;

    spritetype *pMe = gMe->pSprite;
    spritetype *pSprite = &sprite[nTarget];
    float const dx = (float)(pSprite->x - pMe->x);
    float const dy = (float)(pSprite->y - pMe->y);
    float const fDist = max(1.f, sqrtf(dx * dx + dy * dy));

    // a fraction of the way each frame: there in about a tenth of a second
    float const fFollow = (float)min(1.0, dtMs * (14.0 / 1000.0));

    // yaw, in build angle units (2048 a turn)
    float const fWantAng = atan2f(dy, dx) * (1024.f / fPI);
    float const fDiffAng = fmodf(fWantAng - fix16_to_float(q16ang) + 3072.f, 2048.f) - 1024.f;

    input.q16turn = fix16_from_float(fDiffAng * fFollow);

    // pitch: the look angle (same units, up is positive) of Blood's own
    // view (gViewLook, the local one); z is 16 times finer than x/y
    if (!VanillaMode())
    {
        float const fWantLook = atan2f((float)(gMe->zView - ctrlLockOnZ(pSprite)) * (1.f / 16.f), fDist) * (1024.f / fPI);
        float const fDiffLook = fWantLook - fix16_to_float(gViewLook);

        input.q16mlook = fix16_from_float(fDiffLook * fFollow / 8.f);   // the game adds it <<3
    }
}
#endif

void ctrlGetInput(void)
{
    ControlInfo info;

    if (!gGameStarted || gInputMode != INPUT_MODE_0)
    {
        gInput = {};
        CONTROL_GetInput(&info);
        return;
    }

    GINPUT input = {};
    static double lastInputTicks;
    auto const    currentHiTicks    = timerGetFractionalTicks();
    double const  elapsedInputTicks = currentHiTicks - lastInputTicks;

    static int32_t lastInputClock;  // MED
    int32_t const  elapsedTics = (int32_t)totalclock - lastInputClock;

    lastInputTicks = currentHiTicks;
    lastInputClock = (int32_t) totalclock;

    auto scaleAdjustmentToInterval = [=](double x) { return x * kTicsPerSec / (1000.0 / elapsedInputTicks); };

    CONTROL_ProcessBinds();

#ifdef __PPU__
    // dead in a single player game: a new press of Weapon_Fire's button
    // opens Load Game (Open restarts the level, as always); the one held
    // while dying doesn't count. The prompt: view.cpp.
    {
        static bool bFireHeld = true;
        bool const bFire = BUTTON(gamefunc_Weapon_Fire) != 0;

        if (!PS3_DeathPromptShown())
            bFireHeld = true;
        else if (bFire && !bFireHeld)
        {
            bFireHeld = true;
            CONTROL_ClearButton(gamefunc_Weapon_Fire);
            PS3_OpenLoadMenu();
            return;
        }
        else
            bFireHeld = bFire;
    }
#endif

    if (gMouseAiming)
        gMouseAim = 0;

    if (BUTTON(gamefunc_Mouse_Aiming))
    {
        if (gMouseAiming)
            gMouseAim = 1;
        else
        {
            CONTROL_ClearButton(gamefunc_Mouse_Aiming);
            gMouseAim = !gMouseAim;
            if (gMouseAim)
            {
                if (!bSilentAim)
                    viewSetMessage("Mouse aiming ON");
            }
            else
            {
                if (!bSilentAim)
                    viewSetMessage("Mouse aiming OFF");
                gInput.keyFlags.lookCenter = 1;
            }
        }
    }
    else if (gMouseAiming)
        gInput.keyFlags.lookCenter = 1;

    CONTROL_GetInput(&info);

    if (MouseDeadZone)
    {
        if (info.mousey > 0)
            info.mousey = max(info.mousey - MouseDeadZone, 0);
        else if (info.mousey < 0)
            info.mousey = min(info.mousey + MouseDeadZone, 0);

        if (info.mousex > 0)
            info.mousex = max(info.mousex - MouseDeadZone, 0);
        else if (info.mousex < 0)
            info.mousex = min(info.mousex + MouseDeadZone, 0);
    }

    if (MouseBias)
    {
        if (klabs(info.mousex) > klabs(info.mousey))
            info.mousey = tabledivide32_noinline(info.mousey, MouseBias);
        else
            info.mousex = tabledivide32_noinline(info.mousex, MouseBias);
    }

    if (gQuitRequest)
        gInput.keyFlags.quit = 1;

    if (gGameStarted && gInputMode != INPUT_MODE_2 && gInputMode != INPUT_MODE_1
        && BUTTON(gamefunc_Send_Message))
    {
        CONTROL_ClearButton(gamefunc_Send_Message);
        keyFlushScans();
        gInputMode = INPUT_MODE_2;
    }

    if (BUTTON(gamefunc_AutoRun))
    {
        CONTROL_ClearButton(gamefunc_AutoRun);
        gAutoRun = !gAutoRun;
        if (gAutoRun)
            viewSetMessage("Auto run ON");
        else
            viewSetMessage("Auto run OFF");
    }

    if (BUTTON(gamefunc_Map_Toggle))
    {
        CONTROL_ClearButton(gamefunc_Map_Toggle);
        viewToggle(gViewMode);
    }

    if (gViewMode == 4 && BUTTON(gamefunc_Map_Follow_Mode))
    {
        CONTROL_ClearButton(gamefunc_Map_Follow_Mode);
        gFollowMap = !gFollowMap;
        gViewMap.FollowMode(gFollowMap);
    }

    if (BUTTON(gamefunc_Shrink_Screen))
    {
        if (gViewMode == 3)
        {
            CONTROL_ClearButton(gamefunc_Shrink_Screen);
            viewResizeView(gViewSize + 1);
        }
        if (gViewMode == 2 || gViewMode == 4)
        {
            gZoom = ClipLow(gZoom - (elapsedTics<<5), 64);
            gViewMap.nZoom = gZoom;
        }
    }

    if (BUTTON(gamefunc_Enlarge_Screen))
    {
        if (gViewMode == 3)
        {
            CONTROL_ClearButton(gamefunc_Enlarge_Screen);
            viewResizeView(gViewSize - 1);
        }
        if (gViewMode == 2 || gViewMode == 4)
        {
            gZoom = ClipHigh(gZoom + (elapsedTics<<5), 4096);
            gViewMap.nZoom = gZoom;
        }
    }

    if (BUTTON(gamefunc_Toggle_Crosshair))
    {
        CONTROL_ClearButton(gamefunc_Toggle_Crosshair);
        gAimReticle = !gAimReticle;
    }

    if (BUTTON(gamefunc_Next_Weapon))
    {
        CONTROL_ClearButton(gamefunc_Next_Weapon);
        gInput.keyFlags.nextWeapon = 1;
    }

    if (BUTTON(gamefunc_Previous_Weapon))
    {
        CONTROL_ClearButton(gamefunc_Previous_Weapon);
        gInput.keyFlags.prevWeapon = 1;
    }

    if (BUTTON(gamefunc_Last_Weapon))
    {
        CONTROL_ClearButton(gamefunc_Last_Weapon);
        gInput.keyFlags.lastWeapon = 1;
    }

    if (BUTTON(gamefunc_Show_Opponents_Weapon))
    {
        CONTROL_ClearButton(gamefunc_Show_Opponents_Weapon);
        gShowWeapon = (gShowWeapon+1)%3;
    }

    if (BUTTON(gamefunc_Jump))
        gInput.buttonFlags.jump = 1;

    if (BUTTON(gamefunc_Crouch))
        gInput.buttonFlags.crouch = 1, gCrouchToggleState = 0;

    if (BUTTON(gamefunc_Crouch_Toggle) && !gInput.buttonFlags.jump)
    {
        if (gMe && gMe->isUnderwater)
            gCrouchToggleState = 0, gInput.buttonFlags.crouch = 1;
        else if (gCrouchToggleState <= 1) // start crouching
            gCrouchToggleState = 1, gInput.buttonFlags.crouch = 1;
        else if (gCrouchToggleState == 2) // stop crouching
            gCrouchToggleState = 3;
    }
    else if ((gCrouchToggleState > 0) && (gCrouchToggleState < 3) && !gInput.buttonFlags.jump && gMe && !gMe->isUnderwater)
    {
        gCrouchToggleState = 2;
        gInput.buttonFlags.crouch = 1;
    }
    else
    {
        gCrouchToggleState = 0;
    }

    if (BUTTON(gamefunc_Weapon_Fire))
        gInput.buttonFlags.shoot = 1;

    if (BUTTON(gamefunc_Weapon_Special_Fire))
        gInput.buttonFlags.shoot2 = 1;

    if (BUTTON(gamefunc_Open))
    {
        CONTROL_ClearButton(gamefunc_Open);
        gInput.keyFlags.action = 1;
    }

    gInput.buttonFlags.lookUp |= BUTTON(gamefunc_Look_Up);
    gInput.buttonFlags.lookDown |= BUTTON(gamefunc_Look_Down);

    if (BUTTON(gamefunc_Look_Up) || BUTTON(gamefunc_Look_Down))
        gInput.keyFlags.lookCenter = 1;
    else
    {
        gInput.buttonFlags.lookUp |= BUTTON(gamefunc_Aim_Up);
        gInput.buttonFlags.lookDown |= BUTTON(gamefunc_Aim_Down);
    }

    if (BUTTON(gamefunc_Aim_Center))
    {
        CONTROL_ClearButton(gamefunc_Aim_Center);
        gInput.keyFlags.lookCenter = 1;
    }

    gInput.keyFlags.spin180 |= BUTTON(gamefunc_Turn_Around);

    if (BUTTON(gamefunc_Inventory_Left))
    {
        CONTROL_ClearButton(gamefunc_Inventory_Left);
        gInput.keyFlags.prevItem = 1;
    }

    if (BUTTON(gamefunc_Inventory_Right))
    {
        CONTROL_ClearButton(gamefunc_Inventory_Right);
        gInput.keyFlags.nextItem = 1;
    }

    if (BUTTON(gamefunc_Inventory_Use))
    {
        CONTROL_ClearButton(gamefunc_Inventory_Use);
        gInput.keyFlags.useItem = 1;
    }

    if (BUTTON(gamefunc_BeastVision))
    {
        CONTROL_ClearButton(gamefunc_BeastVision);
        gInput.useFlags.useBeastVision = 1;
    }

    if (BUTTON(gamefunc_CrystalBall))
    {
        CONTROL_ClearButton(gamefunc_CrystalBall);
        gInput.useFlags.useCrystalBall = 1;
    }

    if (BUTTON(gamefunc_JumpBoots))
    {
        CONTROL_ClearButton(gamefunc_JumpBoots);
        gInput.useFlags.useJumpBoots = 1;
    }

    if (BUTTON(gamefunc_MedKit))
    {
        CONTROL_ClearButton(gamefunc_MedKit);
        gInput.useFlags.useMedKit = 1;
    }

    for (int i = 0; i < 10; i++)
    {
        if (BUTTON(gamefunc_Weapon_1 + i))
        {
            CONTROL_ClearButton(gamefunc_Weapon_1 + i);
            gInput.newWeapon = 1 + i;
        }
    }

    if (BUTTON(gamefunc_ProximityBombs))
    {
        CONTROL_ClearButton(gamefunc_ProximityBombs);
        gInput.newWeapon = kWeaponProxyTNT;
    }

    if (BUTTON(gamefunc_RemoteBombs))
    {
        CONTROL_ClearButton(gamefunc_RemoteBombs);
        gInput.newWeapon = kWeaponRemoteTNT;
    }

    if (BUTTON(gamefunc_Holster_Weapon))
    {
        CONTROL_ClearButton(gamefunc_Holster_Weapon);
        gInput.keyFlags.holsterWeapon = 1;
    }

    int const run = gRunKeyMode ? (BUTTON(gamefunc_Run) | gAutoRun) : (BUTTON(gamefunc_Run) ^ gAutoRun);
    int const run2 = BUTTON(gamefunc_Run);
    int const keyMove = (1 + run) << 10;

    gInput.syncFlags.run |= run;

    if (gInput.forward < keyMove && gInput.forward > -keyMove)
    {
        if (BUTTON(gamefunc_Move_Forward))
            input.forward += keyMove;

        if (BUTTON(gamefunc_Move_Backward))
            input.forward -= keyMove;
    }

    if (gInput.strafe < keyMove && gInput.strafe > -keyMove)
    {
        if (BUTTON(gamefunc_Strafe_Left))
            input.strafe += keyMove;
        if (BUTTON(gamefunc_Strafe_Right))
            input.strafe -= keyMove;
    }


    char turnLeft = 0, turnRight = 0;

    if (BUTTON(gamefunc_Strafe))
    {
        if (gInput.strafe < keyMove && gInput.strafe > -keyMove)
        {
            if (BUTTON(gamefunc_Turn_Left))
                input.strafe += keyMove;
            if (BUTTON(gamefunc_Turn_Right))
                input.strafe -= keyMove;
        }
    }
    else
    {
        if (BUTTON(gamefunc_Turn_Left))
            turnLeft = 1;
        if (BUTTON(gamefunc_Turn_Right))
            turnRight = 1;
    }

    static int32_t turnHeldTime;

    if (turnLeft || turnRight)
        turnHeldTime += elapsedTics;
    else
        turnHeldTime = 0;

    if (turnLeft)
        input.q16turn = fix16_ssub(input.q16turn, fix16_from_float(scaleAdjustmentToInterval(ClipHigh(12 * turnHeldTime, gTurnSpeed)>>2)));
    if (turnRight)
        input.q16turn = fix16_sadd(input.q16turn, fix16_from_float(scaleAdjustmentToInterval(ClipHigh(12 * turnHeldTime, gTurnSpeed)>>2)));

    if (!gTurnAcceleration || (((gTurnAcceleration == 2) || run2 || run) && (turnHeldTime > 24)))
        input.q16turn <<= 1;

    if (BUTTON(gamefunc_Strafe))
        input.strafe -= info.mousex;
    else
        input.q16turn = fix16_sadd(input.q16turn, fix16_sdiv(fix16_from_int(info.mousex), F16(32)));

    if (gMouseAim)
        input.q16mlook = fix16_sadd(input.q16mlook, fix16_sdiv(fix16_from_int(gMouseAimingFlipped ? info.mousey : -info.mousey), F16(128)));
    else
        input.forward -= info.mousey;

    if (CONTROL_JoystickEnabled) // controller input
    {
        input.strafe -= int(scaleAdjustmentToInterval(info.dx)/2.f);
        input.forward -= int(scaleAdjustmentToInterval(info.dz)/2.f);
        if (!run) // when autorun is off/run is not held, reduce overall speed for controller
        {
            input.strafe = clamp(input.strafe, -256, 256);
            input.forward = clamp(input.forward, -256, 256);
        }
        if (info.mousey == 0)
        {
            if (gMouseAim)
#ifdef __PPU__
                // PS3: half of PC's, so that a stick's look up/down turns as
                // fast as its turning (below) at the same scale. The game adds
                // q16mlook <<3 to the look angle: dpitch/16/256*8 = dpitch/512
                // angle units, as dyaw/16/32 is for the turn.
                input.q16mlook = fix16_sadd(input.q16mlook, fix16_sdiv(fix16_from_float(scaleAdjustmentToInterval(gMouseAimingFlipped ? info.dpitch : -info.dpitch)/16.f), F16(256)));
#else
                input.q16mlook = fix16_sadd(input.q16mlook, fix16_sdiv(fix16_from_float(scaleAdjustmentToInterval(gMouseAimingFlipped ? info.dpitch : -info.dpitch)/16.f), F16(128)));
#endif
            else
                input.forward -= int(scaleAdjustmentToInterval(info.dpitch)/2.f);
        }
        input.q16turn = fix16_sadd(input.q16turn, fix16_sdiv(fix16_from_float(scaleAdjustmentToInterval(info.dyaw)/16.f), F16(32)));
        if (gCenterViewOnDrop == 2)
        {
            gInput.keyFlags.lookCenter = 1;
            gCenterViewOnDrop = 1;
        }
    }

#ifdef __PPU__
    ctrlLockOn(input, elapsedInputTicks);
#endif

    if (KB_KeyPressed(sc_Pause)) // 0xc5 in disassembly
    {
        gInput.keyFlags.pause = 1;
        KB_ClearKeyDown(sc_Pause);
    }

    if (!gViewMap.bFollowMode && gViewMode == 4)
    {
        const int nForward = gMouseAim ? input.forward : clamp(-info.mousey, -2048, 2048);
        const int nStrafe = input.strafe;
        const int nSpeed = ClipLow((4096-gZoom)>>6, 16);
        gViewMap.turn += input.q16turn<<2;
        gViewMap.forward += gMouseAim ? int(scaleAdjustmentToInterval(nForward))*nSpeed : nForward;
        gViewMap.strafe += int(scaleAdjustmentToInterval(nStrafe))*nSpeed;
        input.q16turn = 0;
        input.forward = 0;
        input.strafe = 0;
    }
    gInput.forward = clamp(gInput.forward + input.forward, -2048, 2048);
    gInput.strafe = clamp(gInput.strafe + input.strafe, -2048, 2048);
    gInput.q16turn = fix16_sadd(gInput.q16turn, input.q16turn);
    gInput.q16mlook = fix16_clamp(fix16_sadd(gInput.q16mlook, input.q16mlook), F16(-127)>>2, F16(127)>>2);
    if (gMe && gMe->pXSprite->health != 0 && !gPaused)
    {
        CONSTEXPR int upAngle = 289;
        CONSTEXPR int downAngle = -347;
        CONSTEXPR double lookStepUp = 4.0*upAngle/60.0;
        CONSTEXPR double lookStepDown = -4.0*downAngle/60.0;
        gViewAngle = (gViewAngle + input.q16turn + fix16_from_float(scaleAdjustmentToInterval(gViewAngleAdjust))) & 0x7ffffff;
        if (gViewLookRecenter)
        {
            if (gViewLook < 0)
                gViewLook = fix16_min(gViewLook+fix16_from_float(scaleAdjustmentToInterval(lookStepDown)), F16(0));
            if (gViewLook > 0)
                gViewLook = fix16_max(gViewLook-fix16_from_float(scaleAdjustmentToInterval(lookStepUp)), F16(0));
        }
        else
        {
            gViewLook = fix16_clamp(gViewLook+fix16_from_float(scaleAdjustmentToInterval(gViewLookAdjust)), F16(downAngle), F16(upAngle));
        }
        gViewLook = fix16_clamp(gViewLook+(input.q16mlook << 3), F16(downAngle), F16(upAngle));
    }
}

void ctrlJoystickRumble(int nTime)
{
    if (!CONTROL_JoystickEnabled || !joystick.hasRumble || !gSetup.joystickrumble)
        return;

#ifdef __PPU__
    // PS3: what the game asks is how hard it was (a hit's pain, the heat
    // of a fire near, the akimbo guns' kick: up to 600), which upstream
    // also made the length: 8 ms each, so a big hit shook the pad for
    // nearly 5 s and every new one added to it. Here the strength follows
    // it and the length stays short: 150 ms for a light one, 400 ms for
    // the hardest. ps3layer.cpp drives the motors (large = rumbleLow,
    // small on from rumbleHigh 0x4000).
    // Patch 19: the pad got the orders (logged) but nothing was felt: a
    // light hit (strength ~25) gave the large motor 90 of 255 for 160 ms,
    // under what it takes to spin up. Now the large motor starts at 160
    // and runs 300 ms at least (up to 800); the small one (the buzz you
    // feel most) joins from strength 64.
    {
        int const nStrength = ClipRange(nTime, 0, 600);
        int const nLow = ClipRange(0x7000 + nStrength * 0x90, 0, UINT16_MAX);   // large motor 160..255
        int const nHigh = nStrength >= 64 ? 0x8000 : 0;
        int const nLength = 300 + nStrength * 500 / 600;

        joystick.rumbleLow = max<int>(joystick.rumbleLow, nLow);
        joystick.rumbleHigh = max<int>(joystick.rumbleHigh, nHigh);
        joystick.rumbleTime = max<int>(joystick.rumbleTime, nLength);
        return;
    }
#endif
    const int nRumble = nTime<<9;
    nTime = ClipLow(nTime, 28)<<3;
    joystick.rumbleHigh = UINT16_MAX > joystick.rumbleHigh + nRumble ? joystick.rumbleHigh + nRumble : UINT16_MAX;
    joystick.rumbleLow  = UINT16_MAX > joystick.rumbleHigh + nRumble ? joystick.rumbleLow  + nRumble : UINT16_MAX;
    joystick.rumbleHigh >>= 1;
    joystick.rumbleTime = nTime      > joystick.rumbleTime ? nTime : joystick.rumbleTime;
}
