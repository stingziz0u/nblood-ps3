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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "build.h"
#include "common_game.h"

#include "callback.h"
#include "db.h"
#include "eventq.h"
#include "globals.h"
#include "loadsave.h"
#include "pqueue.h"
#include "triggers.h"
#include "view.h"
#ifdef NOONE_EXTENSIONS
#include "nnexts.h"
#endif

class EventQueue
{
public:
    PriorityQueue<EVENT>* PQueue;
    EventQueue()
    {
        PQueue = NULL;
    }
    bool IsNotEmpty(unsigned int nTime)
    {
        return PQueue->Size() > 0 && nTime >= PQueue->LowestPriority();
    }
    EVENT ERemove(void)
    {
        return PQueue->Remove();
    }
    void Kill(int, int);
    void Kill(int idx, int type, int causer);
    void Kill(int, int, CALLBACK_ID);
};

EventQueue eventQ;
#ifdef __PPU__
// NBlood-PS3: for the slow frames' "[slow events]" lines (evProcess): what
// the kills walked and took, what evSend handed out
static unsigned gEvKills, gEvSends, gEvSendMsgs;
static unsigned long long gEvKillWalked, gEvKillTicks;

struct Ps3KillTimer
{
    unsigned long long const t0;
    explicit Ps3KillTimer(unsigned queued) : t0(PS3_ProfClock()) { gEvKills++; gEvKillWalked += queued; }
    ~Ps3KillTimer() { gEvKillTicks += PS3_ProfClock() - t0; }
};
# define PS3_KILL_TIMER() Ps3KillTimer const ps3KillTimer(PQueue->Size())
#else
# define PS3_KILL_TIMER() do { } while (0)
#endif

void EventQueue::Kill(int a1, int a2)
{
    PS3_KILL_TIMER();
    PQueue->Kill([=](const EVENT &nItem)->bool {return (nItem.index == a1 && nItem.type == a2); });
}

void EventQueue::Kill(int idx, int type, int causer)
{
    PS3_KILL_TIMER();
    PQueue->Kill([=](const EVENT &nItem)->bool { return (nItem.index == idx && nItem.type == type && nItem.causer == causer); });
}

void EventQueue::Kill(int a1, int a2, CALLBACK_ID a3)
{
    PS3_KILL_TIMER();
    PQueue->Kill([=](const EVENT &nItem)->bool {return (nItem.index == a1 && nItem.type == a2 && nItem.cmd == kCmdCallback && nItem.funcID == (unsigned int)a3); });
}

RXBUCKET rxBucket[kChannelMax+1];

int GetBucketChannel(const RXBUCKET *pRX)
{
    switch (pRX->type) {
        case 6: {
            int nIndex = pRX->index;
            int nXIndex = sector[nIndex].extra;
            dassert(nXIndex > 0);
            return xsector[nXIndex].rxID;
        }
        case 0: {
            int nIndex = pRX->index;
            int nXIndex = wall[nIndex].extra;
            dassert(nXIndex > 0);
            return xwall[nXIndex].rxID;
        }
        case 3: {
            int nIndex = pRX->index;
            int nXIndex = sprite[nIndex].extra;
            dassert(nXIndex > 0);
            return xsprite[nXIndex].rxID;
        }
    }
    return 0;
}

#if 0
int CompareChannels(const void *a, const void *b)
{
    return GetBucketChannel((const RXBUCKET*)a)-GetBucketChannel((const RXBUCKET*)b);
}
#else
static int CompareChannels(RXBUCKET *a, RXBUCKET *b)
{
    return GetBucketChannel(a) - GetBucketChannel(b);
}
#endif

static RXBUCKET *SortGetMiddle(RXBUCKET *a1, RXBUCKET *a2, RXBUCKET *a3)
{
    if (CompareChannels(a1, a2) > 0)
    {
        if (CompareChannels(a1, a3) > 0)
        {
            if (CompareChannels(a2, a3) > 0)
                return a2;
            return a3;
        }
        return a1;
    }
    else
    {
        if (CompareChannels(a1, a3) < 0)
        {
            if (CompareChannels(a2, a3) > 0)
                return a3;
            return a2;
        }
        return a1;
    }
}

static void SortSwap(RXBUCKET *a, RXBUCKET *b)
{
    RXBUCKET t = *a;
    *a = *b;
    *b = t;
}

static void SortRXBucket(int nCount)
{
    RXBUCKET *v144[32];
    int vc4[32];
    int v14 = 0;
    RXBUCKET *pArray = rxBucket;
    while (true)
    {
        while (nCount > 1)
        {
            if (nCount < 16)
            {
                for (int nDist = 3; nDist > 0; nDist -= 2)
                {
                    for (RXBUCKET *pI = pArray+nDist; pI < pArray+nCount; pI += nDist)
                    {
                        for (RXBUCKET *pJ = pI; pJ > pArray && CompareChannels(pJ-nDist, pJ) > 0; pJ -= nDist)
                        {
                            SortSwap(pJ, pJ-nDist);
                        }
                    }
                }
                break;
            }
            RXBUCKET *v30, *vdi, *vsi;
            vdi = pArray + nCount / 2;
            if (nCount > 29)
            {
                v30 = pArray;
                vsi = pArray + nCount-1;
                if (nCount > 42)
                {
                    int v20 = nCount / 8;
                    v30 = SortGetMiddle(v30, v30+v20, v30+v20*2);
                    vdi = SortGetMiddle(vdi-v20, vdi, vdi+v20);
                    vsi = SortGetMiddle(vsi-v20*2, vsi-v20, vsi);
                }
                vdi = SortGetMiddle(v30, vdi, vsi);
            }
            RXBUCKET v44 = *vdi;
            RXBUCKET *vc = pArray;
            RXBUCKET *v8 = pArray+nCount-1;
            RXBUCKET *vbx = vc;
            RXBUCKET *v4 = v8;
            while (true)
            {
                while (vbx <= v4)
                {
                    int nCmp = CompareChannels(vbx, &v44);
                    if (nCmp > 0)
                        break;
                    if (nCmp == 0)
                    {
                        SortSwap(vbx, vc);
                        vc++;
                    }
                    vbx++;
                }
                while (vbx <= v4)
                {
                    int nCmp = CompareChannels(v4, &v44);
                    if (nCmp < 0)
                        break;
                    if (nCmp == 0)
                    {
                        SortSwap(v4, v8);
                        v8--;
                    }
                    v4--;
                }
                if (vbx > v4)
                    break;
                SortSwap(vbx, v4);
                v4--;
                vbx++;
            }
            RXBUCKET *v2c = pArray+nCount;
            int vt = ClipHigh(vbx-vc, vc-pArray);
            for (int i = 0; i < vt; i++)
            {
                SortSwap(&vbx[i-vt], &pArray[i]);
            }
            vt = ClipHigh(v8-v4, v2c-v8-1);
            for (int i = 0; i < vt; i++)
            {
                SortSwap(&v2c[i-vt], &vbx[i]);
            }
            int vvsi = v8-v4;
            int vvdi = vbx-vc;
            if (vvsi >= vvdi)
            {
                vc4[v14] = vvsi;
                v144[v14] = v2c-vvsi;
                nCount = vvdi;
                v14++;
            }
            else
            {
                vc4[v14] = vvdi;
                v144[v14] = pArray;
                nCount = vvsi;
                pArray = v2c - vvsi;
                v14++;
            }
        }
        if (v14 == 0)
            return;
        v14--;
        pArray = v144[v14];
        nCount = vc4[v14];
    }
}

unsigned short bucketHead[1024+1];

void evInit(void)
{
    if (eventQ.PQueue)
        delete eventQ.PQueue;
    if (VanillaMode())
        eventQ.PQueue = new VanillaPriorityQueue<EVENT>();
    else
        eventQ.PQueue = new StdPriorityQueue<EVENT>();
    eventQ.PQueue->Clear();
    int nCount = 0;
    for (int i = 0; i < numsectors; i++)
    {
        int nXSector = sector[i].extra;
        if (nXSector >= kMaxXSectors)
            ThrowError("Invalid xsector reference in sector %d", i);
        if (nXSector > 0 && xsector[nXSector].rxID > 0)
        {
            dassert(nCount < kChannelMax);
            rxBucket[nCount].type = 6;
            rxBucket[nCount].index = i;
            nCount++;
        }
    }
    for (int i = 0; i < numwalls; i++)
    {
        int nXWall = wall[i].extra;
        if (nXWall >= kMaxXWalls)
            ThrowError("Invalid xwall reference in wall %d", i);
        if (nXWall > 0 && xwall[nXWall].rxID > 0)
        {
            dassert(nCount < kChannelMax);
            rxBucket[nCount].type = 0;
            rxBucket[nCount].index = i;
            nCount++;
        }
    }
    for (int i = 0; i < kMaxSprites; i++)
    {
        if (sprite[i].statnum < kMaxStatus)
        {
            int nXSprite = sprite[i].extra;
            if (nXSprite >= kMaxXSprites)
                ThrowError("Invalid xsprite reference in sprite %d", i);
            if (nXSprite > 0 && xsprite[nXSprite].rxID > 0)
            {
                dassert(nCount < kChannelMax);
                rxBucket[nCount].type = 3;
                rxBucket[nCount].index = i;
                nCount++;
            }
        }
    }
    SortRXBucket(nCount);
    int i, j = 0;
    for (i = 0; i < 1024; i++)
    {
        bucketHead[i] = j;
        while(j < nCount && GetBucketChannel(&rxBucket[j]) == i)
            j++;
    }
    bucketHead[i] = j;
}

char evGetSourceState(int nType, int nIndex)
{
    switch (nType)
    {
    case 6:
    {
        int nXIndex = sector[nIndex].extra;
        dassert(nXIndex > 0 && nXIndex < kMaxXSectors);
        return xsector[nXIndex].state;
    }
    case 0:
    {
        int nXIndex = wall[nIndex].extra;
        dassert(nXIndex > 0 && nXIndex < kMaxXWalls);
        return xwall[nXIndex].state;
    }
    case 3:
    {
        int nXIndex = sprite[nIndex].extra;
        dassert(nXIndex > 0 && nXIndex < kMaxXSprites);
        return xsprite[nXIndex].state;
    }
    }
    return 0;
}

void evSend(int nIndex, int nType, int rxId, COMMAND_ID command, int causerID)
{
    switch (command) {
        case kCmdState:
            command = evGetSourceState(nType, nIndex) ? kCmdOn : kCmdOff;
            break;
        case kCmdNotState:
            command = evGetSourceState(nType, nIndex) ? kCmdOff : kCmdOn;
            break;
        default:
            break;
    }
    
    EVENT event;
    event.index = nIndex;
    event.type = nType;
    event.cmd = command;
    #ifdef NOONE_EXTENSIONS
        event.causer = (gModernMap) ? causerID : kCauserGame;
    #else
        event.causer = kCauserGame;
    #endif

    switch (rxId) {
    case kChannelTextOver:
        if (command >= kCmdNumberic) trTextOver(command - kCmdNumberic);
        else viewSetSystemMessage("Invalid TextOver command by xobject #%d (object type %d)", nIndex, nType);
        return;
    case kChannelLevelExitNormal:
        levelEndLevel(kLevelExitNormal);
        return;
    case kChannelLevelExitSecret:
        levelEndLevel(kLevelExitSecret);
        return;
    #ifdef NOONE_EXTENSIONS
    // finished level and load custom level ¹ via numbered command.
    case kChannelModernEndLevelCustom:
        if (command >= kCmdNumberic) levelEndLevelCustom(command - kCmdNumberic);
        else viewSetSystemMessage("Invalid Level-Exit# command by xobject #%d (object type %d)", nIndex, nType);
        return;
    #endif
    case kChannelSetTotalSecrets:
        if (command >= kCmdNumberic) levelSetupSecret(command - kCmdNumberic);
        else viewSetSystemMessage("Invalid Total-Secrets command by xobject #%d (object type %d)", nIndex, nType);
        break;
    case kChannelSecretFound:
        if (command >= kCmdNumberic) levelTriggerSecret(command - kCmdNumberic);
        else viewSetSystemMessage("Invalid Trigger-Secret command by xobject #%d (object type %d)", nIndex, nType);
        break;
    case kChannelRemoteBomb0:
    case kChannelRemoteBomb1:
    case kChannelRemoteBomb2:
    case kChannelRemoteBomb3:
    case kChannelRemoteBomb4:
    case kChannelRemoteBomb5:
    case kChannelRemoteBomb6:
    case kChannelRemoteBomb7:
        for (int nSprite = headspritestat[kStatThing]; nSprite >= 0; nSprite = nextspritestat[nSprite])
        {
            spritetype* pSprite = &sprite[nSprite];
            if (pSprite->flags & 32)
                continue;
            int nXSprite = pSprite->extra;
            if (nXSprite > 0)
            {
                XSPRITE* pXSprite = &xsprite[nXSprite];
                if (pXSprite->rxID == rxId)
                    trMessageSprite(nSprite, event);
            }
        }
        return;
    case kChannelTeamAFlagCaptured:
    case kChannelTeamBFlagCaptured:
        for (int nSprite = headspritestat[kStatItem]; nSprite >= 0; nSprite = nextspritestat[nSprite])
        {
            spritetype* pSprite = &sprite[nSprite];
            if (pSprite->flags & 32)
                continue;
            int nXSprite = pSprite->extra;
            if (nXSprite > 0)
            {
                XSPRITE* pXSprite = &xsprite[nXSprite];
                if (pXSprite->rxID == rxId)
                    trMessageSprite(nSprite, event);
            }
        }
        return;
    default:
        break;
    }

    #ifdef NOONE_EXTENSIONS
    if (gModernMap)
    {       
        // allow to send commands on player sprites
        PLAYER* pPlayer = NULL;
        if (playerRXRngIsFine(rxId))
        {
            if ((pPlayer = getPlayerById((rxId - kChannelPlayer7) + kMaxPlayers)) != NULL)
            {
                if (command == kCmdEventKillFull)
                    evKill(pPlayer->nSprite, OBJ_SPRITE);
                else
                    trMessageSprite(pPlayer->nSprite, event);
            }
           
            return;

        }
        else if (rxId == kChannelAllPlayers)
        {
            for (int i = 0; i < kMaxPlayers; i++)
            {
                if ((pPlayer = getPlayerById(i)) != NULL)
                {
                    if (command == kCmdEventKillFull)
                        evKill(pPlayer->nSprite, OBJ_SPRITE);
                    else
                        trMessageSprite(pPlayer->nSprite, event);
                }
            }
            
            return;
        }
        // send command on sprite which created the event sequence
        else if (rxId == kChannelEventCauser && event.causer < kCauserGame)
        {
            spritetype* pSpr = &sprite[event.causer];
            if (!(pSpr->flags & kHitagFree) && !(pSpr->flags & kHitagRespawn))
            {
                if (command == kCmdEventKillFull)
                    evKill(causerID, OBJ_SPRITE);
                else
                    trMessageSprite(event.causer, event);
            }
            
            return;
        }
        else if (command == kCmdEventKillFull)
        {
            killEvents(rxId, command);
            return;
        }
    }
    #endif

#ifdef __PPU__
    gEvSends++;
    gEvSendMsgs += bucketHead[rxId+1] - bucketHead[rxId];
#endif
    for (int i = bucketHead[rxId]; i < bucketHead[rxId+1]; i++) {
        if (event.type != rxBucket[i].type || event.index != rxBucket[i].index) {
            switch (rxBucket[i].type) {
                case 6:
                    trMessageSector(rxBucket[i].index, event);
                    break;
                case 0:
                    trMessageWall(rxBucket[i].index, event);
                    break;
                case 3:
                {
                    int nSprite = rxBucket[i].index;
                    spritetype *pSprite = &sprite[nSprite];
                    if (pSprite->flags&32)
                        continue;
                    int nXSprite = pSprite->extra;
                    if (nXSprite > 0)
                    {
                        XSPRITE *pXSprite = &xsprite[nXSprite];
                        if (pXSprite->rxID > 0)
                            trMessageSprite(nSprite, event);
                    }
                    break;
                }
            }
        }
    }
}

void evPost(int nIndex, int nType, unsigned int nDelta, COMMAND_ID command, int causerID) {
    dassert(command != kCmdCallback);
    if (command == kCmdState) command = evGetSourceState(nType, nIndex) ? kCmdOn : kCmdOff;
    else if (command == kCmdNotState) command = evGetSourceState(nType, nIndex) ? kCmdOff : kCmdOn;
    EVENT evn = {};
    evn.index = nIndex;
    evn.type = nType;
    evn.cmd = command;
    #ifdef NOONE_EXTENSIONS
        evn.causer = (gModernMap) ? causerID : kCauserGame;
    #else
        evn.causer = kCauserGame;
    #endif

    eventQ.PQueue->Insert((int)gFrameClock+nDelta, evn);
}

void evPost(int nIndex, int nType, unsigned int nDelta, CALLBACK_ID callback) {
    EVENT evn = {};
    evn.index = nIndex;
    evn.type = nType;
    evn.cmd = kCmdCallback;
    evn.funcID = callback;
    evn.causer = kCauserGame;
    eventQ.PQueue->Insert((int)gFrameClock+nDelta, evn);
}

#ifdef __PPU__
extern "C" void PS3_Log(const char *fmt, ...);
extern unsigned long long ps3_sfxTicks;   // sfx.cpp, sound.cpp: the sound starts
extern unsigned ps3_sfxCalls;

struct Ps3SlowEv { double ms; int type, cmd, index, func; };

// one event's object, for the log: a sector's, sprite's or wall's type
// and channels
static void Ps3EvDescribe(char *buf, size_t size, Ps3SlowEv const &e)
{
    if (e.cmd == kCmdCallback)
        snprintf(buf, size, "callback %d on %s %d", e.func, e.type == 6 ? "sector" : e.type == 0 ? "wall" : "sprite", e.index);
    else if (e.type == 6 && e.index >= 0 && e.index < numsectors)
    {
        int const nX = sector[e.index].extra;
        snprintf(buf, size, "sector %d (type %d, rx %d tx %d) cmd %d", e.index, (int)sector[e.index].type,
                 nX > 0 ? (int)xsector[nX].rxID : -1, nX > 0 ? (int)xsector[nX].txID : -1, e.cmd);
    }
    else if (e.type == 3 && e.index >= 0 && e.index < kMaxSprites)
    {
        int const nX = sprite[e.index].extra;
        snprintf(buf, size, "sprite %d (type %d, rx %d tx %d) cmd %d", e.index, (int)sprite[e.index].type,
                 nX > 0 ? (int)xsprite[nX].rxID : -1, nX > 0 ? (int)xsprite[nX].txID : -1, e.cmd);
    }
    else if (e.type == 0 && e.index >= 0 && e.index < numwalls)
        snprintf(buf, size, "wall %d (type %d) cmd %d", e.index, (int)wall[e.index].type, e.cmd);
    else
        snprintf(buf, size, "type %d index %d cmd %d", e.type, e.index, e.cmd);
}
#endif

void evProcess(unsigned int nTime)
{
#ifdef __PPU__
    unsigned long long const ps3All0 = PS3_ProfClock();
    unsigned const ps3Queued0 = eventQ.PQueue->Size();
    unsigned const ps3Kills0 = gEvKills, ps3Sends0 = gEvSends, ps3Msgs0 = gEvSendMsgs;
    unsigned long long const ps3Walked0 = gEvKillWalked, ps3KillT0 = gEvKillTicks, ps3SfxT0 = ps3_sfxTicks;
    unsigned const ps3SfxN0 = ps3_sfxCalls;
    unsigned ps3Events = 0, ps3ByType[4] = {};   // sector, wall, sprite, callback
    Ps3SlowEv ps3Top[3] = {};
#endif
#if 0
    while (1)
    {
        // Inlined?
        char bDone;
        if (eventQ.fNodeCount > 0 && nTime >= eventQ.queueItems[1])
            bDone = 1;
        else
            bDone = 0;
        if (!bDone)
            break;
#endif
    while(eventQ.IsNotEmpty(nTime))
    {
        EVENT event = eventQ.ERemove();
#ifdef __PPU__
        unsigned long long const ps3T0 = PS3_ProfClock();
#endif
        if (event.cmd == kCmdCallback)
        {
            dassert(event.funcID < kCallbackMax);
            dassert(gCallback[event.funcID] != NULL);
            gCallback[event.funcID](event.index);
        }
        else
        {
            switch (event.type)
            {
            case 6:
                trMessageSector(event.index, event);
                break;
            case 0:
                trMessageWall(event.index, event);
                break;
            case 3:
                trMessageSprite(event.index, event);
                break;
            }
        }
#ifdef __PPU__
        {
            double const ms = PS3_ProfClockMs(PS3_ProfClock() - ps3T0);
            Ps3SlowEv const e = { ms, (int)event.type, (int)event.cmd, (int)event.index, (int)event.funcID };

            ps3Events++;
            ps3ByType[event.cmd == kCmdCallback ? 3 : event.type == 6 ? 0 : event.type == 0 ? 1 : 2]++;

            for (int k = 0; k < 3; k++)
                if (ms > ps3Top[k].ms)
                {
                    for (int m = 2; m > k; m--)
                        ps3Top[m] = ps3Top[m-1];
                    ps3Top[k] = e;
                    break;
                }
        }
#endif
    }
#ifdef __PPU__
    // a slow evProcess (the "events" of the hitch lines): what it did
    double const ps3AllMs = PS3_ProfClockMs(PS3_ProfClock() - ps3All0);
    if (ps3AllMs > 20.0)
    {
        PS3_Log("[slow events] %.1f ms: %u events (sectors %u, walls %u, sprites %u, callbacks %u), queue %u -> %u; "
                "%u sends to %u objects; %u kills walked %llu queued events in %.1f ms; %u sound starts in %.1f ms",
                ps3AllMs, ps3Events, ps3ByType[0], ps3ByType[1], ps3ByType[2], ps3ByType[3], ps3Queued0,
                (unsigned)eventQ.PQueue->Size(), gEvSends - ps3Sends0, gEvSendMsgs - ps3Msgs0,
                gEvKills - ps3Kills0, gEvKillWalked - ps3Walked0, PS3_ProfClockMs(gEvKillTicks - ps3KillT0),
                ps3_sfxCalls - ps3SfxN0, PS3_ProfClockMs(ps3_sfxTicks - ps3SfxT0));

        for (int k = 0; k < 3 && ps3Top[k].ms > 0.0; k++)
        {
            char what[128];
            Ps3EvDescribe(what, sizeof(what), ps3Top[k]);
            PS3_Log("[slow events]   %.1f ms: %s", ps3Top[k].ms, what);
        }
    }
#endif
}

void evKill(int a1, int a2)
{
    eventQ.Kill(a1, a2);
}

void evKill(int idx, int type, int causer)
{
    #ifdef NOONE_EXTENSIONS
    if (gModernMap && isPartOfCauserScript(type, idx))
        eventQ.Kill(idx, type, causer);
    else
    #endif
        eventQ.Kill(idx, type);
}

void evKill(int a1, int a2, CALLBACK_ID a3)
{
    eventQ.Kill(a1, a2, a3);
}

class EventQLoadSave : public LoadSave
{
public:
    virtual void Load();
    virtual void Save();
};

void EventQLoadSave::Load()
{
    if (eventQ.PQueue)
        delete eventQ.PQueue;
    Read(&eventQ, sizeof(eventQ));
    if (VanillaMode())
        eventQ.PQueue = new VanillaPriorityQueue<EVENT>();
    else
        eventQ.PQueue = new StdPriorityQueue<EVENT>();
    int nEvents;
    Read(&nEvents, sizeof(nEvents));
    for (int i = 0; i < nEvents; i++)
    {
        EVENT event = {};
        unsigned int eventtime;
        Read(&eventtime, sizeof(eventtime));
        Read(&event, sizeof(event));
        eventQ.PQueue->Insert(eventtime, event);
    }
    Read(rxBucket, sizeof(rxBucket));
    Read(bucketHead, sizeof(bucketHead));
}

struct EventWithTime {
	EVENT event;
	unsigned int evTime;
};

void EventQLoadSave::Save()
{
    uint32_t nEvents = eventQ.PQueue->Size();
    // DG: support arbitrary number of events instead of just 1024 (fix crashes when saving in WLB)
    EventWithTime* events = new EventWithTime[nEvents];

    Write(&eventQ, sizeof(eventQ)); // FIXME: what's the point of saving this? this only writes a pointer that is invalid on load anyway
    Write(&nEvents, sizeof(nEvents));
    for (uint32_t i = 0; i < nEvents; i++)
    {
        EventWithTime& evt = events[i];
        evt.evTime = eventQ.PQueue->LowestPriority();
        evt.event = eventQ.ERemove();
        Write(&evt.evTime, sizeof(evt.evTime));
        Write(&evt.event, sizeof(evt.event));
    }
    dassert(eventQ.PQueue->Size() == 0);
    for (uint32_t i = 0; i < nEvents; i++)
    {
        eventQ.PQueue->Insert(events[i].evTime, events[i].event);
    }
    Write(rxBucket, sizeof(rxBucket));
    Write(bucketHead, sizeof(bucketHead));
    delete[] events;
}

static EventQLoadSave *myLoadSave;

void EventQLoadSaveConstruct(void)
{
    myLoadSave = new EventQLoadSave();
}
