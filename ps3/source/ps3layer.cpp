// PS3 interface layer for the Build Engine (EDuke32-PS3)
//
// The PS3 side of the "render layer" contract (baselayer.h), in place of
// sdlayer.cpp: the boot, sysutil, the video mode and the frame on the RSX,
// the DualShock 3 as a game controller, message boxes on the TV, the
// clock, and every way out.
//
// Two ways to draw: the classic renderer's 8 bit image, turned into
// A8R8G8B8 here and scaled to the TV by the RSX; or Polymost (OpenGL, the
// default), on the RSX through ps3gl (ps3/source/gl), drawing into a
// render target of the internal resolution that is scaled the same way.
//
// Boot steps, each one a line in USRDIR/eduke32-ps3.log, so a black
// screen can be located from the log alone (handoff notes 4.1):
//
//   [0] main() reached              -> if missing: -lrt -llv2 in the link
//   [1] game thread                 -> the EBOOT's own stack is tiny
//   [2] sysutil callback            -> without it, "Quit Game" reboots
//   [3] file system (USRDIR)
//   [4] CPU (endianness, sizes)
//   [5] DualShock 3
//   [6] RSX
//   [7] game data found
//   [8] the engine takes over (app_main)
//   [9] shutdown (PS3_Exit: every way out goes through it)
//
// Copyright (C) 2026 the EDuke32-PS3 contributors. GPL v2 or later, as
// the rest of EDuke32.

#include "a.h"
#include "build.h"
#include "cache1d.h"
#include "compat.h"
#include "engine_priv.h"
#include "osd.h"
#include "palette.h"
#include "renderlayer.h"
#include "softsurface.h"
#include "communityapi.h"

#include <dirent.h>
#include <malloc.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/time.h>

#include <ppu-lv2.h>
#include <ppu-types.h>
#include <lv2/systime.h>
#include <sys/process.h>
#include <sys/systime.h>
#include <sys/thread.h>
#include <sysutil/sysutil.h>

#ifdef USE_OPENGL
# include "glbuild.h"
# include "polymost.h"
#endif

#include <sys/mman.h>

#include "ps3_platform.h"
#include "ps3_video.h"
#include "ps3_launcher.h"
#include "ps3_font.h"
#include "ps3_pad.h"
#include "driver_ps3.h"
#include <unistd.h>

// The loader sizes the primary thread's stack from this ELF section.
SYS_PROCESS_PARAM(1001, 0x100000);

// The engine and the game run here. Build keeps big arrays on the stack
// and the renderer recurses (mirrors, room over room).
// 2 MB: measured (patch 17's high-water mark) under 68 KB through the
// startup, the menus, two levels, saving (the classic renderer's picture)
// and loading; Blood's big arrays are global. It was 8 MB, 6 of them for
// nothing out of the game's 213.
#define GAME_STACK_SIZE (2 * 1024 * 1024)

static volatile int exit_requested;

// the last error shown (PS3_FatalScreen), for the launcher: the game ends
// after it, and the launcher shows it under its list
static char last_error[512];
static int sysutil_registered;
static int pad_ok;

// ---------------------------------------
//
// Globals of the layer (as sdlayer.cpp)
//
// ---------------------------------------

int32_t inputchecked = 0;

char quitevent = 0, appactive = 1, novideo = 0;

int32_t maxrefreshfreq = 0;
int32_t xres = -1, yres = -1, bpp = 0, fullscreen = 0, bytesperline;
double refreshfreq = 60.0;
intptr_t frameplace = 0;
int32_t lockcount = 0;
char modechange = 1;
char offscreenrendering = 0;
char videomodereset = 0;
int32_t nofog = 0;

bool g_ImGuiCaptureInput = false;
uint8_t g_ImGuiCapturedDevices;

static char modeschecked = 0;

// the faded palette as A8R8G8B8, for the software screen
static uint32_t pal32[256];
static int32_t vsync_renderlayer;

#ifdef USE_OPENGL
// ps3gl (ps3/source/gl). Its header is GL/gl.h's world, this file is
// glad's: only what the layer calls.
extern "C" {
int   PS3GL_Init(int width, int height);
void  PS3GL_BeginFrame(void);
void  PS3GL_EndFrame(void);
void  PS3GL_SetFastShaders(int on);
int   PS3GL_FastShaders(void);
int   PS3GL_TakeOutOfMemory(void);
void *PS3GL_GetProcAddress(const char *name);
}

void polymost_ps3_dropRGBA(void);   // build/src/polymost.cpp
extern "C" void PS3_GameFilterOff(void);    // ps3_game_*.cpp

char nogl = 0;
static int gl_loaded;       // glad has ps3gl's entry points
static int gl_started;      // ps3gl runs on the RSX (an OpenGL mode was set)
static int gl_frame_open;   // PS3GL_BeginFrame done for the frame being drawn

int PS3_GladFill(void);   // ps3_gladfill.cpp

static void *GLGetProc(const char *name)
{
    return PS3GL_GetProcAddress(name);
}

static void GLCloseFrame(void)
{
    if (gl_frame_open)
    {
        PS3GL_EndFrame();
        gl_frame_open = 0;
    }
}

static void GLOpenFrame(void)
{
    GLCloseFrame();
    PS3GL_BeginFrame();
    gl_frame_open = 1;
}
#endif

#if !defined STARTUP_SETUP_WINDOW
int32_t startwin_open(void) { return 0; }
int32_t startwin_close(void) { return 0; }
int32_t startwin_puts(const char *s) { UNREFERENCED_PARAMETER(s); return 0; }
int32_t startwin_idle(void *s) { UNREFERENCED_PARAMETER(s); return 0; }
int32_t startwin_settitle(const char *s) { UNREFERENCED_PARAMETER(s); return 0; }
int32_t startwin_run(void) { return 0; }
bool startwin_isopen(void) { return false; }
#endif

// ---------------------------------------
//
// Time
//
// ---------------------------------------

unsigned long long PS3_Micros(void)
{
    return (unsigned long long)sysGetSystemTime();
}

// SELECT+R3 switches it off and on (to compare)
static int ps3_pacedclock = 1;

// newlib on the PS3 has neither (ps3/compat/ps3_compat.h). The Build timer
// reads CLOCK_MONOTONIC through enet_gettime; the realtime clock is only
// there for completeness.
extern "C" int clock_gettime(clockid_t clock_id, struct timespec *tp)
{
    if (!tp)
    {
        errno = EINVAL;
        return -1;
    }

    if (clock_id == CLOCK_REALTIME)
    {
        struct timeval tv;

        gettimeofday(&tv, nullptr);
        tp->tv_sec  = tv.tv_sec;
        tp->tv_nsec = tv.tv_usec * 1000;
        return 0;
    }

    // the vblank's time (ps3_video.c, PS3_Video_PacedMicros): even steps
    // between frames; never backwards when it takes over from the plain one
    static uint64_t last_us;
    uint64_t us = ps3_pacedclock ? (uint64_t)PS3_Video_PacedMicros() : (uint64_t)sysGetSystemTime();

    if (us < last_us)
        us = last_us;

    last_us = us;

    tp->tv_sec  = (time_t)(us / 1000000u);
    tp->tv_nsec = (long)(us % 1000000u) * 1000;
    return 0;
}

extern "C" int nanosleep(const struct timespec *rqtp, struct timespec *rmtp)
{
    if (rmtp)
    {
        rmtp->tv_sec = 0;
        rmtp->tv_nsec = 0;
    }

    if (!rqtp || rqtp->tv_sec < 0 || rqtp->tv_nsec < 0)
    {
        errno = EINVAL;
        return -1;
    }

    uint64_t us = (uint64_t)rqtp->tv_sec * 1000000u + (uint64_t)rqtp->tv_nsec / 1000u;

    if (us == 0)
        sysThreadYield();
    else
        while (us > 0)
        {
            uint32_t const step = us > 1000000u ? 1000000u : (uint32_t)us;
            sysUsleep(step);
            us -= step;
        }

    return 0;
}

// lv2's sys_memory_get_user_memory_size (syscall 352)
static s32 UserMemorySize(uint32_t *total)
{
    struct { u32 total, available; } info = {};

    lv2syscall1(352, (u64)(uintptr_t)&info);
    *total = info.total;
    return_to_user_prog(s32);
}

// newlib declares sysconf() but has none. The engine asks for the page
// size (cache1d's index alignment) and the amount of memory (tiles.cpp
// sizes its art cache from it).
extern "C" long sysconf(int name)
{
    switch (name)
    {
        case _SC_PAGESIZE:
            return 4096;

        case _SC_PHYS_PAGES:
        {
            uint32_t total = 0;

            if (UserMemorySize(&total) != 0 || total == 0)
                total = 213u << 20;  // what a game gets on a retail PS3

            return (long)(total / 4096);
        }

        default:
            errno = EINVAL;
            return -1;
    }
}

// No mmap on the PS3. Only the OpenGL texture cache would map its file,
// and it's off (no texture compression): a mapping just fails.
extern "C" void *mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    UNREFERENCED_PARAMETER(addr);
    UNREFERENCED_PARAMETER(length);
    UNREFERENCED_PARAMETER(prot);
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(fd);
    UNREFERENCED_PARAMETER(offset);
    errno = ENOSYS;
    return MAP_FAILED;
}

extern "C" int munmap(void *addr, size_t length)
{
    UNREFERENCED_PARAMETER(addr);
    UNREFERENCED_PARAMETER(length);
    return 0;
}

extern "C" int msync(void *addr, size_t length, int flags)
{
    UNREFERENCED_PARAMETER(addr);
    UNREFERENCED_PARAMETER(length);
    UNREFERENCED_PARAMETER(flags);
    return 0;
}

extern "C" int gethostname(char *name, size_t len)
{
    if (!name || len == 0)
    {
        errno = EINVAL;
        return -1;
    }

    Bstrncpyz(name, "ps3", len);
    return 0;
}

// ---------------------------------------
//
// sysutil
//
// ---------------------------------------

static void SysutilCallback(u64 status, u64 param, void *userdata)
{
    UNREFERENCED_PARAMETER(userdata);

    switch (status)
    {
        case SYSUTIL_EXIT_GAME:
            PS3_Log("[sysutil] SYSUTIL_EXIT_GAME -> quit");
            exit_requested = 1;
            break;

        case SYSUTIL_DRAW_BEGIN:
            PS3_Log("[sysutil] XMB overlay: draw begin");
            break;

        case SYSUTIL_DRAW_END:
            PS3_Log("[sysutil] XMB overlay: draw end");
            break;

        case SYSUTIL_MENU_OPEN:
            PS3_Log("[sysutil] XMB menu open");
            break;

        case SYSUTIL_MENU_CLOSE:
            PS3_Log("[sysutil] XMB menu close");
            break;

        default:
            PS3_Log("[sysutil] event 0x%llx (param 0x%llx)", (unsigned long long)status, (unsigned long long)param);
            break;
    }
}

void PS3_Pump(void)
{
    if (sysutil_registered)
        sysUtilCheckCallback();
}

int PS3_ExitRequested(void)
{
    return exit_requested;
}

// The game's way out for the XMB's "Quit Game": EDuke32-PS3 defines it to
// save the settings first (ps3_game_duke3d.cpp). Without one, out at once.
extern "C" void PS3_GameQuit(void) __attribute__((weak));

static void HandleExitRequest(void)
{
    static int handling;

    if (!exit_requested || handling++)
        return;

    PS3_Log("[9] XMB quit: leaving");

    if (PS3_GameQuit)
        PS3_GameQuit();

    PS3_Exit(0);
}

// ---------------------------------------
//
// Leaving
//
// ---------------------------------------

// Every way out ends here: the engine's exit() and abort() are renamed to
// PS3_exit / PS3_abort (ps3/Makefile), and so are the XMB's "Quit Game"
// and fatal errors. Whatever the system runs on its own (the audio thread,
// flips) is stopped before the process goes, or the whole console hangs
// (handoff notes 3.8, 3.64).
void PS3_Exit(int code)
{
    static int exiting;

    if (exiting++)
        exit(code);

    PS3_Log("[9] exit(%d): shutting down", code);

    PS3Drv_PCM_Shutdown();
    PS3_Video_Shutdown();

    if (pad_ok)
    {
        PS3_Pad_Shutdown();
        pad_ok = 0;
    }

    if (sysutil_registered)
    {
        sysUtilUnregisterCallback(0);
        sysutil_registered = 0;
    }

    // Quit from the game's menu, or an error: back to the launcher (not
    // the XMB's "Quit Game"). Returns only if that can't be done.
    if (!exit_requested && PS3_Launcher_Used())
        PS3_Launcher_Relaunch(last_error[0] ? last_error : nullptr);

    PS3_Log("[9] bye");
    PS3_LogShutdown();

    exit(code);
}

extern "C" void PS3_exit(int code)
{
    PS3_Exit(code);
}

extern "C" void PS3_abort(void)
{
    PS3_Log("abort() called");
    PS3_Exit(3);
}

// ---------------------------------------
//
// Message boxes: a text screen on the TV and the pad
//
// ---------------------------------------

#define TEXT_COLUMNS 60
#define TEXT_LINES   16

// Fresh presses only: the button that got here (the menu's X) doesn't count.
// Returns the button, or 0 on timeout / XMB quit.
static unsigned WaitForButtons(unsigned mask, int seconds)
{
    ps3_padstate_t st;
    unsigned last = 0xffffu;

    if (!pad_ok)
        pad_ok = PS3_Pad_Init() == 0;

    for (int ms = 0; ms < seconds * 1000 && !exit_requested; ms += 20)
    {
        PS3_Pad_Poll(&st);

        unsigned const now = st.connected ? st.buttons : 0;
        unsigned const down = now & ~last & mask;

        if (down)
            return down;

        last = now;
        sysUsleep(20000);
        PS3_Pump();
    }

    return 0;
}

// ---------------------------------------
//
// Text screens (errors, questions, no data): drawn by the CPU into a
// 1280x720 source image and presented. The game's next frame sets its own
// source size again.
//
// ---------------------------------------

#define TXT_W 1280
#define TXT_H 720
#define TXT_SCALE 2

static void TxtRect(uint32_t *img, int x, int y, int w, int h, uint32_t c)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > TXT_W) w = TXT_W - x;
    if (y + h > TXT_H) h = TXT_H - y;

    for (int j = 0; j < h; j++)
    {
        uint32_t *row = img + (y + j) * TXT_W + x;
        for (int i = 0; i < w; i++)
            row[i] = c;
    }
}

static void TxtString(uint32_t *img, int x, int y, const char *str, uint32_t c)
{
    for (; *str; str++, x += 8 * TXT_SCALE)
    {
        unsigned ch = (unsigned char)*str;

        if (ch < 32 || ch > 126)
            ch = '?';

        for (int gy = 0; gy < 16; gy++)
        {
            unsigned const bits = ps3_font8x16[ch - 32][gy];

            if (!bits)
                continue;

            for (int gx = 0; gx < 8; gx++)
                if (bits & (0x80u >> gx))
                    TxtRect(img, x + gx * TXT_SCALE, y + gy * TXT_SCALE, TXT_SCALE, TXT_SCALE, c);
        }
    }
}

static void TextScreen(const char *title, const char *const *lines, int nlines)
{
    if (PS3_Video_Init() != 0 || PS3_Video_SetSource(TXT_W, TXT_H) != 0)
        return;

    uint32_t *const img = PS3_Video_Source();

    if (!img)
        return;

    TxtRect(img, 0, 0, TXT_W, TXT_H, 0xff000000u);

    int y = TXT_H / 5;

    if (title)
    {
        int const tw = (int)strlen(title) * 8 * TXT_SCALE;
        TxtString(img, (TXT_W - tw) / 2, y, title, 0xffd02020u);
        y += 16 * TXT_SCALE * 2;
    }

    for (int i = 0; i < nlines; i++, y += 16 * TXT_SCALE + 8)
        if (lines[i])
            TxtString(img, TXT_W / 10, y, lines[i], 0xffe0e0e0u);

    PS3_Video_Present();
    PS3_Video_Finish();

    PS3_Log("[video] text screen: %s", title ? title : "");

#ifdef USE_OPENGL
    // over an OpenGL mode: its render targets went for the text, back
    if (bpp > 8 && gl_started && PS3_Video_SetRenderTarget(xres, yres) == 0)
    {
        gl_frame_open = 0;
        PS3GL_Init(xres, yres);
        GLOpenFrame();
    }
#endif
}

static void ShowText(const char *title, const char *message, const char *footer1, const char *footer2)
{
    static char buf[TEXT_LINES][TEXT_COLUMNS + 1];
    const char *lines[TEXT_LINES + 4];
    const char *p = message ? message : "";
    int n = 0;

    // wrap the message at spaces
    while (*p && n < TEXT_LINES)
    {
        int len = 0, cut = -1;

        while (p[len] && p[len] != '\n' && len < TEXT_COLUMNS)
        {
            if (p[len] == ' ')
                cut = len;
            len++;
        }

        if (p[len] && p[len] != '\n' && cut > 0)
            len = cut;

        memcpy(buf[n], p, (size_t)len);
        buf[n][len] = '\0';
        lines[n] = buf[n];
        n++;

        p += len;

        if (*p == '\n' || *p == ' ')
            p++;
    }

    lines[n++] = "";
    if (footer1) lines[n++] = footer1;
    if (footer2) lines[n++] = footer2;

    TextScreen(title, lines, n);
}

void PS3_FatalScreen(const char *title, const char *message)
{
    PS3_Log("[error] %s: %s", title ? title : "", message ? message : "");
    snprintf(last_error, sizeof(last_error), "%s%s%s", title ? title : "", title ? ": " : "",
             message ? message : "");

    // Started from the launcher: it shows the error under its list once
    // the game has quit (every fatal error does), in a fresh process with
    // the video just set up. A text screen over the game's own video stayed
    // black (NBlood, an error during the engine's start).
    if (PS3_Launcher_Used())
        return;

    ShowText(title, message, "Details: USRDIR/" PS3_PROGRAM ".log", "Press X to go on.");
    WaitForButtons(PS3_PAD_CROSS, 120);
}

int32_t wm_msgbox(const char *name, const char *fmt, ...)
{
    char buf[MSGBOX_PRINTF_MAX];
    va_list va;

    va_start(va, fmt);
    Bvsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    PS3_FatalScreen(name, buf);
    return 0;
}

int32_t wm_ynbox(const char *name, const char *fmt, ...)
{
    char buf[MSGBOX_PRINTF_MAX];
    va_list va;

    va_start(va, fmt);
    Bvsnprintf(buf, sizeof(buf), fmt, va);
    va_end(va);

    PS3_Log("[question] %s: %s", name ? name : "", buf);
    ShowText(name, buf, "X = Yes      O = No", nullptr);

    // no answer in two minutes: yes, the safe default of every question
    // the game asks (import the old settings, use the internal scripts)
    unsigned const b = WaitForButtons(PS3_PAD_CROSS | PS3_PAD_CIRCLE, 120);
    int const yes = !(b & PS3_PAD_CIRCLE);

    PS3_Log("[question] answer: %s", yes ? "yes" : "no");
    return yes;
}

void wm_setapptitle(const char *name)
{
    UNREFERENCED_PARAMETER(name);
}

// ---------------------------------------
//
// System
//
// ---------------------------------------

int32_t initsystem(void)
{
    timerInit(CLOCKTICKSPERSECOND);

    frameplace = 0;
    lockcount = 0;

    if (PS3_Video_Init() != 0)
    {
        LOG_F(ERROR, "The RSX did not start.");
        novideo = 1;
    }

    g_numdisplays = 1;
    LOG_F(INFO, "Using the PS3 RSX video layer.");

#ifdef USE_OPENGL
    // glad's pointers right away: the engine touches GL state even in the
    // classic renderer (videoSetRenderMode). ps3gl only keeps it until an
    // OpenGL mode starts it on the RSX.
    gladLoadGLLoader(GLGetProc);

    // what glad didn't load for a 2.1 context (ps3_gladfill.cpp)
    LOG_F(INFO, "ps3gl: %d GL 3+ entry points filled in.", PS3_GladFill());

    if (novideo || GLVersion.major < 2)
    {
        LOG_F(ERROR, "No OpenGL 2 from ps3gl: only the classic renderer.");
        nogl = 1;
    }
    else
        gl_loaded = 1;
#endif

    return 0;
}

void uninitsystem(void)
{
    uninitinput();
    timerUninit();
}

void system_getcvars(void)
{
    vsync = videoSetVsync(vsync);
}

int debugprintf(const char *f, ...)
{
    UNREFERENCED_PARAMETER(f);
    return 0;
}

// ---------------------------------------
//
// Input: the DualShock 3 as a game controller (SDL GameController order,
// which mact and the game's default bindings expect)
//
// ---------------------------------------

enum
{
    PADBTN_A, PADBTN_B, PADBTN_X, PADBTN_Y,
    PADBTN_BACK, PADBTN_GUIDE, PADBTN_START,
    PADBTN_LEFTSTICK, PADBTN_RIGHTSTICK,
    PADBTN_LEFTSHOULDER, PADBTN_RIGHTSHOULDER,
    PADBTN_DPAD_UP, PADBTN_DPAD_DOWN, PADBTN_DPAD_LEFT, PADBTN_DPAD_RIGHT,
    PADBTN_COUNT
};

enum
{
    PADAXIS_LEFTX, PADAXIS_LEFTY, PADAXIS_RIGHTX, PADAXIS_RIGHTY,
    PADAXIS_TRIGGERLEFT, PADAXIS_TRIGGERRIGHT,
    PADAXIS_COUNT
};

static const struct { unsigned ps3; int button; } padmap[] =
{
    { PS3_PAD_CROSS,    PADBTN_A },
    { PS3_PAD_CIRCLE,   PADBTN_B },
    { PS3_PAD_SQUARE,   PADBTN_X },
    { PS3_PAD_TRIANGLE, PADBTN_Y },
    { PS3_PAD_SELECT,   PADBTN_BACK },
    { PS3_PAD_START,    PADBTN_START },
    { PS3_PAD_L3,       PADBTN_LEFTSTICK },
    { PS3_PAD_R3,       PADBTN_RIGHTSTICK },
    { PS3_PAD_L1,       PADBTN_LEFTSHOULDER },
    { PS3_PAD_R1,       PADBTN_RIGHTSHOULDER },
    { PS3_PAD_UP,       PADBTN_DPAD_UP },
    { PS3_PAD_DOWN,     PADBTN_DPAD_DOWN },
    { PS3_PAD_LEFT,     PADBTN_DPAD_LEFT },
    { PS3_PAD_RIGHT,    PADBTN_DPAD_RIGHT },
};

static int StickAxis(int v)
{
    // 0..255, 128 = center (y grows downwards, as SDL's)
    int const a = (v - 128) * 258;
    return clamp(a, -32768, 32767);
}

// The pad behaves as SDL's events do, not as a state copied every poll:
// a button's bit is set when it goes down and cleared when it comes up,
// an axis is written when it moves. The game "eats" a press by clearing
// joystick.bits (JOYSTICK_ClearAllButtons): with the button still held,
// it must stay eaten until released. Copying the whole state each poll
// brought it back on the next frame -- START opened the menu and the
// still-held START closed it again a few frames later.
static int32_t pad_lastbits;
static int32_t pad_lastaxis[PADAXIS_COUNT];

// the buttons down now, SDL's order (the game's menus: a direction held
// repeats, gamemenu.cpp); joystick.bits has only the presses not eaten
extern "C" int32_t PS3_PadHeldBits(void)
{
    return pad_lastbits;
}

static void PadReleaseAll(void)
{
    joystick.bits = 0;
    pad_lastbits = 0;

    for (int i = 0; i < PADAXIS_COUNT; i++)
    {
        if (i < joystick.numAxes)
            joystick.pAxis[i] = 0;
        pad_lastaxis[i] = 0;
    }
}

static void PollPad(void)
{
    ps3_padstate_t st;

    if (!pad_ok || !joystick.pAxis)
        return;

    PS3_Pad_Poll(&st);

    if (!st.connected)
    {
        PadReleaseAll();
        return;
    }

    // Rumble: the game adds up strength and time (duke3d/src/input.h);
    // like sdlayer: start what's asked, stop when the time is up. Low
    // frequency = the large motor (strength), high = the small one (on/off).
    {
        static uint32_t rumbleZeroTime;

        if (joystick.rumbleLow || joystick.rumbleHigh)
        {
            rumbleZeroTime = timerGetTicks() + joystick.rumbleTime;
            // (the large motor barely turns below ~48: weak hits start there)
            PS3_Pad_SetRumble(joystick.rumbleHigh >= 0x4000,
                              joystick.rumbleLow ? min(255, 48 + (joystick.rumbleLow >> 8)) : 0);
            joystick.rumbleTime = joystick.rumbleLow = joystick.rumbleHigh = 0;
        }
        else if (rumbleZeroTime && timerGetTicks() >= rumbleZeroTime)
        {
            rumbleZeroTime = 0;
            PS3_Pad_SetRumble(0, 0);
        }

        if (!appactive)
            PS3_Pad_SetRumble(0, 0);
    }

    if (!appactive)
        return;

    int32_t bits = 0;

    for (auto const &m : padmap)
        if (st.buttons & m.ps3)
            bits |= 1 << m.button;

    int32_t const pressed  = bits & ~pad_lastbits;
    int32_t const released = pad_lastbits & ~bits;

    // the testing combos below only with USRDIR/logs.txt: SELECT+L3/R3
    // are presses a player makes (run + crouch...). L3+R3 is the debug
    // builds' level skipper: the shader's debug views (ps3gl_prog.c) are
    // no longer on a button.
    if (PS3_LogsEnabled())
    {
#ifdef USE_OPENGL
    // SELECT+L3: Polymost's short fragment programs off / on (to compare)
    {
        int32_t const combo = (1 << PADBTN_BACK) | (1 << PADBTN_LEFTSTICK);

        if ((bits & combo) == combo && (pressed & combo))
            PS3GL_SetFastShaders(!PS3GL_FastShaders());
    }
#endif

    // SELECT+R3: the game clock on the vblanks, or plain (to compare)
    {
        int32_t const combo = (1 << PADBTN_BACK) | (1 << PADBTN_RIGHTSTICK);

        if ((bits & combo) == combo && (pressed & combo))
        {
            ps3_pacedclock ^= 1;
            PS3_Log("[video] game clock: %s", ps3_pacedclock ? "on the vblanks" : "plain");
        }
    }
    }

    joystick.bits = (joystick.bits | pressed) & ~released;
    pad_lastbits = bits;

    int32_t axis[PADAXIS_COUNT];

    axis[PADAXIS_LEFTX]  = StickAxis(st.lx);
    axis[PADAXIS_LEFTY]  = StickAxis(st.ly);
    axis[PADAXIS_RIGHTX] = StickAxis(st.rx);
    axis[PADAXIS_RIGHTY] = StickAxis(st.ry);
    axis[PADAXIS_TRIGGERLEFT]  = (st.buttons & PS3_PAD_L2) ? 32767 : 0;
    axis[PADAXIS_TRIGGERRIGHT] = (st.buttons & PS3_PAD_R2) ? 32767 : 0;

    for (int i = 0; i < PADAXIS_COUNT; i++)
        if (axis[i] != pad_lastaxis[i])
        {
            joystick.pAxis[i] = axis[i];
            pad_lastaxis[i] = axis[i];
        }
}

void joyScanDevices()
{
    inputdevices &= ~DEV_JOYSTICK;

    if (!pad_ok)
        pad_ok = PS3_Pad_Init() == 0;

    if (!pad_ok)
    {
        VLOG_F(LOG_INPUT, "No game controllers found.");
        return;
    }

    joystick.flags        = 0;
    joystick.numBalls     = 0;
    joystick.numHats      = 0;
    joystick.numAxes      = PADAXIS_COUNT;
    joystick.numButtons   = PADBTN_COUNT;
    joystick.validButtons = (1u << PADBTN_COUNT) - 1 - (1u << PADBTN_GUIDE);
    joystick.isGameController = 1;
    joystick.hasRumble = 1;   // DualShock 3 (PS3_Pad_SetRumble); Sixaxis: nothing happens

    Xfree(joystick.pAxis);
    joystick.pAxis = (int32_t *)Xcalloc(joystick.numAxes, sizeof(int32_t));
    DO_FREE_AND_NULL(joystick.pHat);

    inputdevices |= DEV_JOYSTICK;

    VLOG_F(LOG_INPUT, "Using controller: DualShock 3.");
}

int32_t initinput(void (*hotplugCallback)(void) /*= nullptr*/)
{
    if (hotplugCallback)
        g_controllerHotplugCallback = hotplugCallback;

    inputdevices = DEV_KEYBOARD;
    g_mouseGrabbed = false;
    memset(g_keyNameTable, 0, sizeof(g_keyNameTable));

    if ((g_controllerSupportFlags & CONTROLLER_DISABLED) == 0)
        joyScanDevices();

    return 0;
}

void uninitinput(void)
{
    mouseUninit();
}

const char *joyGetName(int32_t what, int32_t num)
{
    static char const *const axisStrings[PADAXIS_COUNT] =
    {
        "Left Stick X-Axis",
        "Left Stick Y-Axis",
        "Right Stick X-Axis",
        "Right Stick Y-Axis",
        "L2",
        "R2",
    };

    static char const *const buttonStrings[PADBTN_COUNT] =
    {
        "Cross",
        "Circle",
        "Square",
        "Triangle",
        "Select",
        "PS",
        "Start",
        "L3",
        "R3",
        "L1",
        "R1",
        "D-Pad Up",
        "D-Pad Down",
        "D-Pad Left",
        "D-Pad Right",
    };

    static char tmp[64];

    switch (what)
    {
        case 0:  // axis
            if ((unsigned)num >= (unsigned)joystick.numAxes)
                return NULL;
            if (num < PADAXIS_COUNT)
                return axisStrings[num];
            Bsprintf(tmp, "Axis %d", num);
            return tmp;

        case 1:  // button
            if ((unsigned)num >= (unsigned)joystick.numButtons)
                return NULL;
            if (num < PADBTN_COUNT)
                return buttonStrings[num];
            Bsprintf(tmp, "Button %d", num);
            return tmp;

        case 2:  // hat
            return NULL;

        default:
            return NULL;
    }
}

// No mouse.
void mouseInit(void)
{
    g_mouseEnabled = 0;
}

void mouseUninit(void)
{
    g_mouseEnabled = 0;
    g_mouseGrabbed = 0;
}

void mouseGrabInput(bool grab)
{
    g_mouseGrabbed = grab;
}

void mouseLockToWindow(char a)
{
    if (!(a & 2))
    {
        mouseGrabInput(a);
        g_mouseLockedToWindow = g_mouseGrabbed;
    }
}

// (NBlood's menus call it: no pointer to move on the PS3)
void mouseMoveToCenter(void)
{
}

// ---------------------------------------
//
// Video: the internal resolution is any of these; the RSX scales it to
// the 720p output at its own aspect (square pixels, as the engine draws).
// 8 bits: the classic renderer. 32 bits: Polymost (OpenGL, ps3gl).
//
// ---------------------------------------

static vec2_t const ps3modes[] =
{
    {  640, 360 },
    {  640, 400 },
    {  640, 480 },
    {  848, 480 },
    {  960, 540 },
    {  960, 720 },
    { 1024, 576 },
    { 1280, 720 },
};

static int sortmodes(const void *a_, const void *b_)
{
    auto a = (const struct validmode_t *)b_;
    auto b = (const struct validmode_t *)a_;
    int x;

    if ((x = a->fs   - b->fs)   != 0) return x;
    if ((x = a->bpp  - b->bpp)  != 0) return x;
    if ((x = a->xdim - b->xdim) != 0) return x;
    if ((x = a->ydim - b->ydim) != 0) return x;

    return 0;
}

void videoGetModes(int display)
{
    UNREFERENCED_PARAMETER(display);

    if (modeschecked || novideo)
        return;

    validmodecnt = 0;

#ifdef USE_OPENGL
    static int const depths[] = { 8, 32 };
#else
    static int const depths[] = { 8 };
#endif

    for (auto const &m : ps3modes)
        for (int fs = 1; fs >= 0; --fs)
            for (int const d : depths)
            {
#ifdef USE_OPENGL
                if (d > 8 && nogl)
                    continue;
#endif
                if (validmodecnt < MAXVALIDMODES)
                {
                    validmode[validmodecnt].xdim = m.x;
                    validmode[validmodecnt].ydim = m.y;
                    validmode[validmodecnt].bpp  = d;
                    validmode[validmodecnt].fs   = fs;
                    validmodecnt++;
                }
            }

    qsort((void *)validmode, validmodecnt, sizeof(struct validmode_t), &sortmodes);

    modeschecked = 1;
}

int32_t videoCheckMode(int32_t *x, int32_t *y, int32_t c, int32_t fs, int32_t forced)
{
    UNREFERENCED_PARAMETER(forced);

    int32_t nearest = -1, odx = 9999, ody = 9999;

    videoGetModes();

#ifdef USE_OPENGL
    c = (c > 8) ? 32 : 8;

    if (c > 8 && nogl)
        return -1;
#else
    if (c > 8)
        return -1;
#endif

    *x = clamp(*x & ~1, 320, MAXXDIM);
    *y = clamp(*y & ~1, 200, MAXYDIM);

    // windowed or not makes no difference here: both are in the list
    for (int i = 0; i < validmodecnt; i++)
    {
        if (validmode[i].bpp != c || validmode[i].fs != (fs & 1))
            continue;

        int32_t const dx = klabs(validmode[i].xdim - *x);
        int32_t const dy = klabs(validmode[i].ydim - *y);

        if (!(dx | dy))
        {
            nearest = i;
            break;
        }

        if ((dx <= odx) && (dy <= ody))
        {
            nearest = i;
            odx = dx;
            ody = dy;
        }
    }

    if (nearest < 0)
        return -1;

    *x = validmode[nearest].xdim;
    *y = validmode[nearest].ydim;

    return nearest;
}

char const *videoGetDisplayName(int display)
{
    UNREFERENCED_PARAMETER(display);
    return "TV";
}

int32_t videoSetVsync(int32_t newSync)
{
    // the RSX flips on the vertical blank: always in sync
    vsync_renderlayer = newSync;
    return newSync;
}

void engineBeginImGuiFrame(void) { }
void engineEndImGuiInput(void) { }
void engineBeginImGuiInput(void) { }
void engineSetupImGui(void) { }

#ifdef USE_OPENGL
// Polymost's mode: the render target, ps3gl, glad. 0 ok.
static int GLSetMode(int32_t x, int32_t y)
{
    if (!gl_loaded)
        return -1;

    // the frame being drawn (if any) goes with the old render target
    GLCloseFrame();

    if (PS3_Video_SetRenderTarget(x, y) != 0)
    {
        LOG_F(ERROR, "No %dx%d render target on the RSX.", x, y);
        return -1;
    }

    if (PS3GL_Init(x, y) != 0)
    {
        LOG_F(ERROR, "ps3gl failed to start (see " PS3_PROGRAM ".log).");
        nogl = 1;
        return -1;
    }

    gl_started = 1;
    return 0;
}
#endif

int32_t videoSetMode(int32_t x, int32_t y, int32_t c, int32_t fs)
{
#ifdef USE_OPENGL
    c = (c > 8) ? 32 : 8;

    if (c > 8 && nogl)
        return -1;
#else
    if (c > 8)
        return -1;
#endif

    if (videoCheckMode(&x, &y, c, fs, 0) < 0)
        return -1;

    while (lockcount)
        videoEndDrawing();

    if (PS3_Video_Init() != 0)
        return -1;

#ifdef USE_OPENGL
    // leaving or resizing an OpenGL mode: Polymost's textures go (as
    // sdlayer.cpp does)
    if (bpp > 8 && gl_started)
        polymost_glreset();

    if (c > 8)
    {
        if (GLSetMode(x, y) != 0)
        {
            videoResetMode();
            return -1;
        }
    }
    else
    {
        GLCloseFrame();
        softsurface_destroy();
    }
#else
    softsurface_destroy();
#endif

    VLOG_F(LOG_GFX, "Setting video mode %dx%d (%s, scaled to the TV by the RSX).", x, y,
           c > 8 ? "OpenGL on the RSX" : "8-bpp");

    xres = x;
    yres = y;
    bpp = c;
    fullscreen = fs;
    numpages = c > 8 ? 2 : 1;
    frameplace = 0;
    lockcount = 0;
    modechange = 1;
    videomodereset = 0;
    refreshfreq = 60.0;

#ifdef USE_OPENGL
    if (c > 8)
    {
        // sdlayer_setvideomode_opengl
        glShadeModel(GL_SMOOTH);
        glClearColor(0, 0, 0, 1.0);
        glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);
        glViewport(0, 0, xres, yres);
        glDisable(GL_DITHER);

        fill_glinfo();

        // brightness, contrast and saturation: Polymost's color
        // correction (videoSetGamma), not the palette
        gammabrightness = 1;

        GLOpenFrame();
    }
    else
#endif
    {
        // brightness goes into the palette, as on DOS (palette.cpp)
        gammabrightness = 0;
    }

    videoFadePalette(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);

    return 0;
}

void videoResetMode(void)
{
    videomodereset = 1;
    modeschecked = 0;
}

void videoBeginDrawing(void)
{
    if (bpp > 8)
    {
        if (offscreenrendering) return;
        frameplace = 0;
        bytesperline = 0;
        modechange = 0;
        return;
    }

    // lock the frame
    if (lockcount++ > 0)
        return;

    static intptr_t backupFrameplace = 0;

    if (inpreparemirror)
    {
        //POGO: if we are offscreenrendering and we need to render a mirror
        //      or we are rendering a mirror and we start offscreenrendering,
        //      backup our offscreen target so we can restore it later
        //      (but only allow one level deep,
        //       i.e. no viewscreen showing a camera showing a mirror that reflects the same viewscreen and recursing)
        if (offscreenrendering)
        {
            if (!backupFrameplace)
                backupFrameplace = frameplace;
            else if (frameplace != (intptr_t)mirrorBuffer &&
                     frameplace != backupFrameplace)
                return;
        }

        frameplace = (intptr_t)mirrorBuffer;

        if (offscreenrendering)
            return;
    }
    else if (offscreenrendering)
    {
        if (backupFrameplace)
        {
            frameplace = backupFrameplace;
            backupFrameplace = 0;
        }
        return;
    }
    else
    {
        frameplace = (intptr_t)softsurface_getBuffer();
    }

    if (modechange)
    {
        bytesperline = xdim;
        calc_ylookup(bytesperline, ydim);
        modechange = 0;
    }
}

void videoEndDrawing(void)
{
    if (bpp > 8)
    {
        if (!offscreenrendering) frameplace = 0;
        return;
    }

    if (!frameplace) return;
    if (lockcount > 1) { lockcount--; return; }
    if (!offscreenrendering) frameplace = 0;
    if (lockcount == 0) return;
    lockcount = 0;
}

void videoShowFrame(int32_t w)
{
    UNREFERENCED_PARAMETER(w);

    if (offscreenrendering || novideo)
        return;

    // the menus' TV screen fit (only marks the borders for clearing when it
    // changes)
    PS3_Video_SetFit(vid_ps3_screenfit);

#ifdef USE_OPENGL
    if (bpp > 8)
    {
        if (palfadedelta)
            fullscreen_tint_gl(palfadergb.r, palfadergb.g, palfadergb.b, palfadedelta);

        // the frame to the TV (scaled like the classic image), and the
        // next one starts in the next render target
        PS3_ProfBegin(PS3P_PRESENT);
        GLCloseFrame();
        PS3_Video_Present();
        GLOpenFrame();
        PS3_ProfEnd(PS3P_PRESENT);

        PS3_Pump();

        // where this frame's time went, for the log (ps3_prof.c)
        PS3_ProfFrame();

        // The RSX memory ran out with a texture filter on: Polymost makes
        // an RGBA texture for every tile and palette then (with mipmaps),
        // next to the indexed tilesheets, and Redneck Rampage's art
        // doesn't fit (Rides Again: textures in white). Back to no filter
        // (the indexed textures, a byte a texel), the RGBA ones freed.
        if (PS3GL_TakeOutOfMemory() && gltexfiltermode != 0)
        {
            PS3_Log("[gl] out of RSX memory with texture filter %d: back to none", (int)gltexfiltermode);
            gltexfiltermode = 0;
            polymost_ps3_dropRGBA();
            gltexapplyprops();
            PS3_GameFilterOff();
        }
        return;
    }
#endif

    if (lockcount)
    {
        LOG_F(WARNING, "Frame was still locked at a depth of %d when videoShowFrame() was called!", lockcount);
        while (lockcount) videoEndDrawing();
    }

    uint8_t const *const buf = softsurface_getBuffer();

    if (buf)
    {
        vec2_t const res = softsurface_getBufferResolution();

        // the image the RSX scales to the TV: the engine's 8 bit screen
        // through the faded palette (rows of res.x pixels, square pixels)
        if (PS3_Video_SetSource(res.x, res.y) == 0)
        {
            uint32_t *dst = PS3_Video_Source();

            if (dst)
            {
                uint64_t const t0 = PS3_Micros();
                int const n = res.x * res.y;

                for (int i = 0; i < n; i++)
                    dst[i] = pal32[buf[i]];

                PS3_Video_ProfileCopy((long long)(PS3_Micros() - t0));
                PS3_Video_Present();
            }
        }
    }

    PS3_Pump();
}

int32_t videoUpdatePalette(int32_t start, int32_t num)
{
    UNREFERENCED_PARAMETER(start);
    UNREFERENCED_PARAMETER(num);

    if (bpp > 8)
        return 0;

    for (int i = 0; i < 256; i++)
        pal32[i] = 0xff000000u | ((uint32_t)curpalettefaded[i].r << 16) |
                   ((uint32_t)curpalettefaded[i].g << 8) | (uint32_t)curpalettefaded[i].b;

    return 0;
}

int32_t videoSetGamma(void)
{
#ifdef USE_OPENGL
    if (bpp > 8 && !nogl)
    {
        g_glColorCorrection = { g_videoGamma, g_videoContrast, g_videoSaturation, 0.f };

        if (videoGetRenderMode() == REND_POLYMOST)
            polymost_setColorCorrection(g_glColorCorrection);

        return 0;
    }
#endif

    // classic: gammabrightness is 0, the engine brightens the palette itself
    return 0;
}

// ---------------------------------------
//
// Events
//
// ---------------------------------------

int32_t handleevents_peekkeys(void)
{
    return 0;
}

int32_t handleevents(void)
{
    PS3_Pump();
    HandleExitRequest();

    PollPad();

    inputchecked = 0;
    timerUpdateClock();

    communityapiRunCallbacks();

    return 0;
}

// ---------------------------------------
//
// Boot
//
// ---------------------------------------

static int ProbeDir(const char *path, int list)
{
    DIR *d = opendir(path);

    if (!d)
    {
        PS3_Log("[3] %s -> can't open", path);
        return -1;
    }

    int count = 0;
    struct dirent *e;

    while ((e = readdir(d)) != NULL)
    {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, ".."))
            continue;

        if (list && count < 40)
            PS3_Log("[3]     %s", e->d_name);

        count++;
    }

    closedir(d);
    PS3_Log("[3] %s -> OK, %d entries", path, count);

    return count;
}

static void ProbeFilesystem(void)
{
    PS3_Log("[3] file system");
    ProbeDir(PS3_USRDIR, 1);

    FILE *f = fopen(PS3_USRDIR "/.writetest", "w");

    if (f)
    {
        fputs("ok", f);
        fclose(f);
        remove(PS3_USRDIR "/.writetest");
        PS3_Log("[3] %s -> WRITABLE", PS3_USRDIR);
    }
    else
        PS3_Log("[3] %s -> NOT writable (settings and saves will fail)", PS3_USRDIR);
}

static void ProbeCPU(void)
{
    unsigned int one = 1;

    PS3_Log("[4] CPU is %s-endian, sizeof(long)=%d sizeof(void*)=%d, char is %s",
            (*(unsigned char *)&one == 1) ? "little" : "big",
            (int)sizeof(long), (int)sizeof(void *),
            ((char)0xff < 0) ? "signed" : "unsigned");
}

static void NoDataScreen(void)
{
    static const char *const lines[] =
    {
        "No game data found.",
        "",
#if defined PS3_GAME_BLOOD
        "Copy the files of YOUR copy of Blood (One Unit",
        "Whole Blood: GOG, Steam or CD) by FTP to:",
        "  " PS3_USRDIR "/",
        "",
        "or to its addons folder: BLOOD.RFF, BLOOD.INI,",
        "SOUNDS.RFF, GUI.RFF, SURFACE.DAT, TILES0xx.ART,",
        "VOXEL.DAT... (the whole game folder will do).",
#elif defined PS3_GAME_RR
#ifdef PS3_GAME_RRRA
        "Copy the files of YOUR copy of Redneck Rampage",
        "Rides Again (GOG's Collection, or the CD) to:",
#else
        "Copy the files of YOUR copy of Redneck Rampage",
        "(GOG's Collection, or the CD) by FTP to:",
#endif
        "  " PS3_USRDIR "/",
        "",
        "or to its addons folder. At least REDNECK.GRP; the",
        "music (track02.ogg ... track10.ogg) in the music folder.",
#elif defined PS3_GAME_SW
        "Copy the files of YOUR copy of Shadow Warrior",
        "(GOG, Steam, CD or the shareware) by FTP to:",
        "  " PS3_USRDIR "/",
        "",
        "or to its addons folder. At least SW.GRP; the add-ons",
        "(WT.GRP for Wanton Destruction, TD.GRP for Twin Dragon)",
        "and the music (Track02.ogg...) in the same folders.",
#else
        "Copy the files of YOUR copy of Duke Nukem 3D",
        "(GOG, Steam, CD or the shareware) by FTP to:",
        "  " PS3_USRDIR "/",
        "",
        "or to its addons folder. At least DUKE3D.GRP; the add-ons",
        "(DUKEDC.GRP, VACATION.GRP, NWINTER.GRP...) in the same",
        "folders, or each in a folder of its own inside addons.",
#endif
        "",
        "Press X to go back to the XMB.",
    };

    PS3_Log("[7] no game data: showing the instructions");

    TextScreen(PS3_LAUNCHER_NAME, lines, ARRAY_SIZE(lines));
    WaitForButtons(PS3_PAD_CROSS, 120);
}

static void GameMain(void)
{
    int argc = 0;
    char **argv = nullptr;
    // the launcher's command line, and the autotest's own after it
    static char *args[32];
#ifdef PS3_GAME_DUKE3D
    static char at1[] = "-v1", at2[] = "-l1", at3[] = "-s4";
#endif

    PS3_Log("[1] game thread running");

    {
        extern int ps3_autotest;   // ps3_game_*.cpp
        // the layer calls the real fopen (not ps3_stdio.c's): no working
        // directory on lv2, the full path
        // (a testing tool: with USRDIR/logs.txt too, whose log it writes to)
        FILE *f = PS3_LogsEnabled() ? fopen(PS3_USRDIR "/autotest.txt", "r") : NULL;

        if (f)
        {
            fclose(f);
            ps3_autotest = 1;
            PS3_Log("[autotest] on (autotest.txt): every level by itself, [autotest] lines in this log");
        }
    }

    if (sysUtilRegisterCallback(0, SysutilCallback, NULL) == 0)
    {
        sysutil_registered = 1;
        PS3_Log("[2] sysutil callback registered");
    }
    else
        PS3_Log("[2] WARNING: sysUtilRegisterCallback failed");

    ProbeFilesystem();
    ProbeCPU();

    pad_ok = PS3_Pad_Init() == 0;
    PS3_Log("[5] pad: %s", pad_ok ? "ok" : "FAILED");

    if (PS3_Video_Init() != 0)
    {
        PS3_Log("[6] FATAL: the RSX didn't start");
        PS3_Exit(1);
    }

    PS3_Video_SetPerfLog(PS3_LogsEnabled());  // fps in the log, with logs.txt

    // [7] the launcher: what is installed (USRDIR, USRDIR/addons), the
    // menu when there is more than one thing to play, the command line
    {
        extern int ps3_autotest;

        if (!PS3_Launcher_Run(&argc, &argv, ps3_autotest))
        {
            NoDataScreen();
            PS3_Exit(0);
        }

        int n = 0;

        for (; n < argc && n < (int)ARRAY_SIZE(args) - 4; n++)
            args[n] = argv[n];

        // autotest: straight into E1L1, the hardest skill (Duke's: SW has
        // no autotest yet, ps3_game_sw.cpp)
#ifdef PS3_GAME_DUKE3D
        if (ps3_autotest)
        {
            args[n++] = at1;
            args[n++] = at2;
            args[n++] = at3;
        }
#endif

        args[n] = nullptr;
        argc = n;
        argv = args;
    }

    PS3_Log("[8] the engine starts");

    engineSetupAllocator();
    engineSetupLogging(argc, argv);

    int const r = app_main(argc, argv);

    PS3_Log("[8] app_main returned %d", r);
    PS3_LogMemoryAt("exit");
    PS3_Exit(r);
}

static void GameThread(void *arg)
{
    UNREFERENCED_PARAMETER(arg);
    PS3_Debug_ThreadStart(GAME_STACK_SIZE);
    GameMain();
    sysThreadExit(0);
}

int main(int argc, char *argv[])
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    sys_ppu_thread_t tid;
    u64 code = 0;

    // newlib's free() gives the top of the heap back to lv2 once more than
    // 128 KB of it is free, and PSL1GHT's negative sbrk unmaps whole 1 MB
    // pages: memory still in use was going too (ps3gl's 600 KB stream
    // buffer, freed and allocated again by Polymost, read back as unmapped
    // -- the game thread dead). The heap never shrinks; freed memory is
    // reused as always (handoff notes 3.173: for every PSL1GHT port).
    mallopt(M_TRIM_THRESHOLD, 0x7fffffff);

    PS3_LogInit();
    PS3_Log("[0] main() reached (%s)", PS3_BUILD_DATE);

    s32 const r = sysThreadCreate(&tid, GameThread, NULL, 1000, GAME_STACK_SIZE,
                                  THREAD_JOINABLE, (char *)PS3_THREAD_NAME);

    if (r != 0)
    {
        PS3_Log("[0] FATAL: sysThreadCreate failed (%d)", (int)r);
        PS3_LogShutdown();
        return 1;
    }

    s32 const jr = sysThreadJoin(tid, &code);

    // Every normal way out ends the process from the game thread itself
    // (PS3_Exit): coming back here means it died. Its trail to the log.
    {
        char why[64];
        snprintf(why, sizeof(why), "the game thread died (exit code %d, join %d)", (int)code, (int)jr);
        PS3_Debug_Dump(why);
        PS3_Video_DebugState();
    }

    PS3_Exit((int)code);
}
