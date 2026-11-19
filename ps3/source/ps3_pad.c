/*
 * Copyright (C) 2026 the ROTT-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * DualShock 3 through ioPad. Taken from Doom64-PS3 (ps3_pad.c), which
 * survived plugging, unplugging and powering pads off and on:
 *
 *  - ioPadGetInfo2 (per port status, bit 1 = "assignment changed");
 *  - the active port is resolved again every frame: a reconnection can
 *    land on another port;
 *  - do NOT skip the data when len == 0: the buffer keeps the previous
 *    poll, skipping freezes the sticks;
 *  - if the library keeps failing, end and init it again.
 *
 *  - only pad number 1 (port 0) plays, as in re3-PS3: another pad is
 *    logged and ignored, and pad 1 takes over again when it comes back.
 *
 * Values are raw here (sticks 0..255, 128 = center). Vibration at the end
 * (re3-PS3's PS3_PadRumble).
 *
 * =======================================================================
 */

#include <stdio.h>
#include <string.h>

#include <io/pad.h>

#include "ps3_platform.h"
#include "ps3_pad.h"

#define STICK_CENTER 128
#define PLAY_PORT    0      /* pad number 1 */

static padInfo2 info;
static padData data;

static int initialized;
static int connected;
static int port = -1;
static int failures;
static int nopad;

static void
ReleaseAll(void)
{
	/* "at rest", not zero: zero is a stick pushed all the way */
	memset(&data, 0, sizeof(data));
	data.button[4] = data.button[5] = STICK_CENTER;
	data.button[6] = data.button[7] = STICK_CENTER;
}

int
PS3_Pad_Init(void)
{
	s32 ret;

	if (initialized)
	{
		return 0;
	}

	ret = ioPadInit(7);
	PS3_Log("[pad] ioPadInit ret=%d", (int)ret);

	if (ret < 0)
	{
		return -1;
	}

	initialized = 1;

	memset(&info, 0, sizeof(info));
	ReleaseAll();
	port = -1;
	connected = 0;

	return 0;
}

void
PS3_Pad_Shutdown(void)
{
	if (!initialized)
	{
		return;
	}

	ioPadEnd();
	initialized = 0;
	connected = 0;
	port = -1;
}

static void
PadReinit(const char *why)
{
	s32 a = ioPadEnd();
	s32 b = ioPadInit(7);

	PS3_Log("[pad] ioPad restarted (%s): end=%d init=%d", why, (int)a, (int)b);
	port = -1;
	failures = 0;
	nopad = 0;
	connected = 0;
	ReleaseAll();
}

void
PS3_Pad_Poll(ps3_padstate_t *out)
{
	static u32 last_status[MAX_PORT_NUM];
	int i, p = -1;
	s32 r;

	if (!initialized)
	{
		ReleaseAll();
		goto done;
	}

	r = ioPadGetInfo2(&info);

	if (r < 0)
	{
		if (connected)
		{
			PS3_Log("[pad] ioPadGetInfo2 failed (%d): pad lost", (int)r);
		}

		connected = 0;
		ReleaseAll();

		if (++nopad == 300)
		{
			PadReinit("GetInfo2 failing");
		}

		goto done;
	}

	for (i = 0; i < MAX_PORT_NUM; i++)
	{
		if (info.port_status[i] != last_status[i])
		{
			PS3_Log("[pad] port %d: status 0x%x -> 0x%x", i,
					(unsigned)last_status[i], (unsigned)info.port_status[i]);
			last_status[i] = info.port_status[i];
		}

		/* assignment changed: drop that port's stale buffer */
		if (info.port_status[i] & 2)
		{
			ioPadClearBuf(i);
		}
	}

	/* Ren: player 1 plays, not the first pad found (a second pad took over
	   when pad 1 went to sleep, and kept it when pad 1 came back) */
	if (info.port_status[PLAY_PORT] & 1)
	{
		p = PLAY_PORT;
	}

	if (p < 0)
	{
		if (connected)
		{
			PS3_Log("[pad] pad 1 gone: no control until a pad is number 1 again");
		}

		connected = 0;
		port = -1;
		ReleaseAll();
		nopad++;

		goto done;
	}

	if (!connected || p != port)
	{
		PS3_Log("[pad] active pad on port %d (was %d)", p, port);
		ioPadClearBuf(p);
		ReleaseAll();
			failures = 0;
	}

	port = p;
	connected = 1;
	nopad = 0;

	r = ioPadGetData(port, &data);

	if (r != 0)
	{
		if (failures < 5)
		{
			PS3_Log("[pad] ioPadGetData(%d) = %d", port, (int)r);
		}

		if (++failures == 180)
		{
			PadReinit("GetData failing");
		}

		/* keep the previous state: don't release buttons */
		goto done;
	}

	failures = 0;

	/* NOT bailing out on len == 0 on purpose, see the header. */

done:
	out->connected = connected;
	out->buttons = (unsigned)((data.button[2] << 8) | data.button[3]);
	out->rx = data.button[4];
	out->ry = data.button[5];
	out->lx = data.button[6];
	out->ly = data.button[7];
}

/* Vibration: the DualShock 3's two motors (small: on / off, large:
   0..255) on pad 1. As re3-PS3's PS3_PadRumble, which works on the
   hardware: sent when it changes, and again every half second while it
   runs. Also no more often than every 50 ms: Blood asks every frame with
   a strength that fades (the pain), and a pad sent a new command 60 times
   a second didn't shake at all. The first changes and any error go in the
   log, with the pad's capability (0x10 = it has motors). */
void
PS3_Pad_SetRumble(int small_on, int large)
{
	static int last_small = -1, last_large = -1, last_port = -1, logged;
	static unsigned long long last_send, on_since;
	unsigned long long const now = PS3_Micros() / 1000;
	padActParam act;
	s32 r;

	if (!initialized || !connected || port < 0)
	{
		last_small = last_large = -1;
		return;
	}

	small_on = small_on ? 1 : 0;
	large = large < 0 ? 0 : (large > 255 ? 255 : large);

	if (small_on == last_small && large == last_large && port == last_port)
	{
		/* the same: again only while it runs, every half second */
		if ((!small_on && !large) || now - last_send < 500)
		{
			return;
		}
	}
	else if ((small_on || large) && (last_small > 0 || last_large > 0) &&
			 port == last_port && now - last_send < 50)
	{
		/* a new strength while it runs: at most every 50 ms (stopping
		   always goes through) */
		return;
	}

	memset(&act, 0, sizeof(act));
	act.small_motor = (u8)small_on;
	act.large_motor = (u8)large;
	r = ioPadSetActDirect((u32)port, &act);

	if ((small_on || large) && !(last_small > 0 || last_large > 0))
	{
		on_since = now;
	}

	if (logged < 40 && (r != 0 || (small_on || large) != (last_small > 0 || last_large > 0)))
	{
		logged++;
		char ran[32] = "";

		if (!small_on && !large)
		{
			snprintf(ran, sizeof(ran), ", after %u ms", (unsigned)(now - on_since));
		}

		PS3_Log("[pad] vibration %s (small %d, large %d%s) on port %d: ioPadSetActDirect = 0x%x, pad capability 0x%x (vibration %s)",
				(small_on || large) ? "on" : "off", small_on, large, ran,
				port, (unsigned)r,
				(unsigned)info.device_capability[port],
				(info.device_capability[port] & 0x10) ? "yes" : "no");
	}

	last_small = small_on;
	last_large = large;
	last_port = port;
	last_send = now;
}
