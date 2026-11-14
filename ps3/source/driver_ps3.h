/*
 * Copyright (C) 2026 the EDuke32-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * The audiolib's PCM driver for the PS3 (see driver_ps3.cpp).
 */

#ifndef driver_ps3_h__
#define driver_ps3_h__

int         PS3Drv_GetError(void);
const char *PS3Drv_ErrorString(int ErrorNumber);

int  PS3Drv_PCM_Init(int *mixrate, int *numchannels, void *initdata);
void PS3Drv_PCM_Shutdown(void);
int  PS3Drv_PCM_BeginPlayback(char *BufferStart, int BufferSize, int NumDivisions, void (*CallBackFunc)(void));
void PS3Drv_PCM_StopPlayback(void);
void PS3Drv_PCM_Lock(void);
void PS3Drv_PCM_Unlock(void);

#endif
