/*
 * ps3_osk.c -- NBlood-PS3: the XMB's on-screen keyboard, for a game that
 * has no keyboard of its own on the screen (PCExhumed's save names; Blood
 * draws its own, gamemenu.cpp).
 *
 * TyrQuakeCell's recipe (ps3_osk.c there, itself CrispyCell's and
 * ioRTWC-PS3's, from Apollo Save Tool): a memory container, a sysutil
 * callback on slot 1 (slot 0 is the layer's, ps3layer.cpp), oskLoadAsync
 * and a state machine run by that callback -- sysUtilCheckCallback runs
 * every frame (PS3_Pump, from handleevents) while the game keeps drawing
 * underneath. While it's up the pad isn't read for the game
 * (ps3layer.cpp, PollPad), and what's still held when it closes (the X
 * that accepted) isn't a press.
 *
 * Copyright (C) 2026 the NBlood-PS3 contributors. GPL v2 or later.
 */

#include <string.h>

#include <ppu-types.h>
#include <sys/memory.h>
#include <sysutil/sysutil.h>
#include <sysutil/osk.h>

#include "ps3_platform.h"

void PS3_Log(const char *fmt, ...);

#define OSK_MAX_CHARS       64
#define OSK_CONTAINER_BIG   (8 * 1024 * 1024)   /* CrispyCell's, proven */
#define OSK_CONTAINER_SMALL (4 * 1024 * 1024)   /* "typical" per osk.h */

enum { OSK_IDLE, OSK_LOADING, OSK_OPEN, OSK_UNLOADING };

static volatile int osk_state = OSK_IDLE;
static int osk_accepted;
static int osk_result;                  /* 0 none yet, 1 accepted, -1 canceled */
static int osk_maxlen;
static char osk_text_ascii[OSK_MAX_CHARS + 1];
static sys_mem_container_t osk_container;
static u16 osk_title[64];
static u16 osk_start[OSK_MAX_CHARS + 1];
static u16 osk_text[OSK_MAX_CHARS + 1];
static oskCallbackReturnParam osk_ret;

int PS3_OSK_Active(void)
{
    return osk_state != OSK_IDLE;
}

static void ToUtf16(const char *in, u16 *out, int max)
{
    int i;

    for (i = 0; in[i] && i < max; i++)
        out[i] = (u16)(unsigned char)in[i];

    out[i] = 0;
}

/* the full-width panel writes U+FF01..U+FF5E; U+3000 is its space */
static void ToAscii(const u16 *in, int len, char *out, int size)
{
    int i, n = 0;

    for (i = 0; i < len && in[i] && n < size - 1; i++)
    {
        u16 c = in[i];

        if (c >= 0xFF01 && c <= 0xFF5E)
            c -= 0xFEE0;
        else if (c == 0x3000)
            c = ' ';

        if (c >= 32 && c < 127)
            out[n++] = (char)c;
    }

    out[n] = 0;
}

static void Finish(void)
{
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);

    if (osk_container)
    {
        sysMemContainerDestroy(osk_container);
        osk_container = 0;
    }

    osk_state = OSK_IDLE;
}

static void Callback(u64 status, u64 param, void *userdata)
{
    (void)param;
    (void)userdata;

    switch (status)
    {
        case SYSUTIL_OSK_LOADED:
            osk_state = OSK_OPEN;
            break;

        case SYSUTIL_OSK_INPUT_ENTERED:
            osk_accepted = 1;
            oskGetInputText(&osk_ret);
            break;

        case SYSUTIL_OSK_INPUT_CANCELED:
            osk_accepted = 0;
            oskAbort();
            osk_state = OSK_UNLOADING;
            oskUnloadAsync(&osk_ret);
            break;

        case SYSUTIL_OSK_DONE:
            if (osk_state != OSK_UNLOADING)
            {
                osk_state = OSK_UNLOADING;
                oskUnloadAsync(&osk_ret);
            }
            break;

        case SYSUTIL_OSK_UNLOADED:
            if (osk_accepted || osk_ret.res == OSK_OK || osk_ret.res == OSK_NO_TEXT)
            {
                ToAscii(osk_text, osk_maxlen, osk_text_ascii, sizeof(osk_text_ascii));
                osk_result = 1;
                PS3_Log("[osk] accepted \"%s\"", osk_text_ascii);
            }
            else
            {
                osk_result = -1;
                PS3_Log("[osk] canceled");
            }

            Finish();
            break;

        default:
            break;
    }
}

/* Opens the keyboard: the title, the text it starts with, at most maxlen
   characters. 0 if it can't (no memory for the dialog). */
int PS3_OSK_Open(const char *title, const char *start, int maxlen)
{
    oskParam param;
    oskInputFieldInfo info;

    if (osk_state != OSK_IDLE)
        return 0;

    if (maxlen < 1 || maxlen > OSK_MAX_CHARS)
        maxlen = OSK_MAX_CHARS;

    if (sysMemContainerCreate(&osk_container, OSK_CONTAINER_BIG) != 0
        && sysMemContainerCreate(&osk_container, OSK_CONTAINER_SMALL) != 0)
    {
        osk_container = 0;
        PS3_Log("[osk] no memory for the keyboard");
        return 0;
    }

    osk_maxlen = maxlen;
    ToUtf16(title, osk_title, 63);
    ToUtf16(start ? start : "", osk_start, maxlen);
    memset(osk_text, 0, sizeof(osk_text));
    memset(&osk_ret, 0, sizeof(osk_ret));
    osk_ret.len = maxlen;
    osk_ret.str = osk_text;
    osk_accepted = 0;
    osk_result = 0;
    osk_text_ascii[0] = 0;

    memset(&param, 0, sizeof(param));
    param.firstViewPanel = OSK_PANEL_TYPE_ALPHABET_FULL_WIDTH;
    param.allowedPanels = OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_NUMERAL | OSK_PANEL_TYPE_ENGLISH;
    param.prohibitFlags = OSK_PROHIBIT_RETURN;
    param.controlPoint.x = 0;
    param.controlPoint.y = 0;

    memset(&info, 0, sizeof(info));
    info.message = osk_title;
    info.startText = osk_start;
    info.maxLength = maxlen;

    oskSetKeyLayoutOption(OSK_10KEY_PANEL | OSK_FULLKEY_PANEL);
    oskAddSupportLanguage(OSK_PANEL_TYPE_ALPHABET | OSK_PANEL_TYPE_ENGLISH);
    oskSetLayoutMode(OSK_LAYOUTMODE_HORIZONTAL_ALIGN_CENTER | OSK_LAYOUTMODE_VERTICAL_ALIGN_CENTER);
    oskSetInitialInputDevice(OSK_DEVICE_PAD);

    sysUtilRegisterCallback(SYSUTIL_EVENT_SLOT1, Callback, NULL);

    if (oskLoadAsync(osk_container, &param, &info) != 0)
    {
        PS3_Log("[osk] oskLoadAsync failed");
        Finish();
        return 0;
    }

    osk_state = OSK_LOADING;
    PS3_Log("[osk] open: %s", title);
    return 1;
}

/* 0 while it's up (or never opened); then once: 1 accepted (the text in
   buf), -1 canceled */
int PS3_OSK_Poll(char *buf, int size)
{
    int r;

    if (osk_state != OSK_IDLE || !osk_result)
        return 0;

    r = osk_result;
    osk_result = 0;

    if (r > 0 && buf && size > 0)
    {
        strncpy(buf, osk_text_ascii, size - 1);
        buf[size - 1] = 0;
    }

    return r;
}

/* Leaving with the keyboard up (the XMB's Quit Game): nothing of ours may
   stay registered with the system */
void PS3_OSK_Shutdown(void)
{
    if (osk_state == OSK_IDLE)
        return;

    oskAbort();
    sysUtilUnregisterCallback(SYSUTIL_EVENT_SLOT1);
    osk_state = OSK_IDLE;
}
