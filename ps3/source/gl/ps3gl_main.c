/*
 * Copyright (C) 2026 the Quake2PS3 / TyrQuakeCell contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * ps3gl: init, frame bracket, frame fence, deferred frees, queries and
 * statistics. See ps3gl.h.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ps3gl.h"
#include "../ps3_platform.h"
#include "../ps3_video.h"

ps3gl_state_t ps3gl;

static int ready;
static int cull_flip;

/* ================================================================ */
/* Frame fence and deferred frees                                     */
/* ================================================================ */

#define MAX_PENDING_FREES 4096

typedef struct {
	void     *data;
	uint32_t  last_frame;
} pending_free_t;

static pending_free_t pending[MAX_PENDING_FREES];
static int npending;

uint32_t
ps3gl_completed_frame(void)
{
	if (!ps3gl.frame_label)
	{
		return 0;
	}

	return *ps3gl.frame_label;
}

/* Memory the RSX may still read (drawn with in 'last_frame') is freed
   once that frame is done. */
void
ps3gl_defer_free(void *data, uint32_t last_frame)
{
	if (!data)
	{
		return;
	}

	if (last_frame == 0 || npending >= MAX_PENDING_FREES)
	{
		if (last_frame != 0)
		{
			/* queue full: wait for the RSX rather than risk it */
			PS3_Video_Finish();
		}

		rsxFree(data);
		return;
	}

	pending[npending].data = data;
	pending[npending].last_frame = last_frame + PS3GL_FREE_DELAY;
	npending++;
}

void
ps3gl_process_frees(void)
{
	uint32_t done = ps3gl_completed_frame();
	int i = 0;

	while (i < npending)
	{
		if ((int)(done - pending[i].last_frame) >= 0 &&
			(int)(ps3gl.frame - pending[i].last_frame) >= 0)
		{
			rsxFree(pending[i].data);
			pending[i] = pending[--npending];
		}
		else
		{
			i++;
		}
	}
}

/* RSX memory, with a second try: what's waiting to be freed (textures
   Polymost dropped, a render target of the resolution before) only goes
   once frames are drawn after it, and a mode change or a level load
   allocates before any is. Out of memory: wait for the RSX to finish
   everything, free all of it, and try again. */
void *
ps3gl_rsx_memalign(uint32_t align, uint32_t size)
{
	void *p = rsxMemalign(align, size);
	int i;

	if (p || !npending)
	{
		return p;
	}

	PS3_Video_Finish();

	for (i = 0; i < npending; i++)
	{
		rsxFree(pending[i].data);
	}

	PS3_Log("[gl] out of RSX memory for %u KB: freed %d buffers the RSX was done with",
			(unsigned)(size >> 10), npending);
	npending = 0;

	return rsxMemalign(align, size);
}

static void
WaitForFrame(uint32_t frame)
{
	int waited = 0;

	while ((int)(ps3gl_completed_frame() - frame) < 0)
	{
		usleep(20);

		if (++waited > 100000)
		{
			PS3_Log("[gl] WARNING: frame fence timed out (want %u, RSX at %u)",
					(unsigned)frame, (unsigned)ps3gl_completed_frame());
			break;
		}
	}
}

/* ================================================================ */

int
PS3GL_Ready(void)
{
	return ready;
}

int
PS3GL_Init(int width, int height)
{
	static int started;

	if (!ready)
	{
		ps3gl.ctx = PS3_Video_Context();

		if (!ps3gl.ctx)
		{
			PS3_Log("[gl] FATAL: no RSX context");
			return -1;
		}

		ps3gl.textures = (ps3gl_texture_t *)calloc(PS3GL_MAX_TEXTURES, sizeof(ps3gl_texture_t));

		if (!ps3gl.textures)
		{
			PS3_Log("[gl] FATAL: no memory for the texture table");
			return -1;
		}

		ps3gl.ring = (uint8_t *)rsxMemalign(128, PS3GL_RING_SEGMENTS * PS3GL_RING_SEG_SIZE);

		if (!ps3gl.ring)
		{
			PS3_Log("[gl] FATAL: rsxMemalign failed for the vertex ring (%d MB)",
					(PS3GL_RING_SEGMENTS * PS3GL_RING_SEG_SIZE) >> 20);
			return -1;
		}

		rsxAddressToOffset(ps3gl.ring, &ps3gl.ring_off);

		/* every segment written once now, from the start (a ring the CPU
		   can't write would show here, in the log, not mid-game) */
		{
			int seg;

			for (seg = 0; seg < PS3GL_RING_SEGMENTS; seg++)
			{
				uint8_t *base = ps3gl.ring + seg * PS3GL_RING_SEG_SIZE;

				PS3_Crumb("ring seg", seg);
				memset(base, 0, PS3GL_RING_SEG_SIZE);
				PS3_Log("[gl] ring segment %d at %p written", seg, (void *)base);
			}
		}

		ps3gl.frame_label = (volatile uint32_t *)gcmGetLabelAddress(PS3GL_LABEL_FRAME);
		*ps3gl.frame_label = 0;
		ps3gl.frame = 0;

		ps3gl_textures_init();
		ps3gl_shaders_init();
		ps3gl_prog_init();

		ready = 1;
		PS3_Log("[gl] ps3gl ready: ring %d x %d KB at offset 0x%08x",
				PS3GL_RING_SEGMENTS, PS3GL_RING_SEG_SIZE / 1024, (unsigned)ps3gl.ring_off);
	}

	ps3gl.screen_w = width;
	ps3gl.screen_h = height;

	if (!started)
	{
		/* GL's initial state. A new size later (Video Settings) keeps the
		   state the game has set up: TyrQuake sets some of it only once,
		   in GL_Init. */
		ps3gl_states_reset();
		ps3gl_matrices_reset();

		ps3gl.color = 0xffffffffu;
		ps3gl.unpack_row_length = 0;
		memset(&ps3gl.va_vertex, 0, sizeof(ps3gl.va_vertex));
		memset(&ps3gl.va_color, 0, sizeof(ps3gl.va_color));
		memset(ps3gl.va_texcoord, 0, sizeof(ps3gl.va_texcoord));
		/* GL's fog defaults */
		ps3gl.fog_start = 0.0f;
		ps3gl.fog_end = 1.0f;
		memset(ps3gl.fog_color, 0, sizeof(ps3gl.fog_color));
		started = 1;
	}

	/* viewport and scissor are flipped against the new height */
	ps3gl.dirty = PS3GL_DIRTY_ALL;

	PS3_Log("[gl] render target %dx%d", width, height);

	return 0;
}

void
PS3GL_SetCullFlip(int flip)
{
	flip = flip ? 1 : 0;

	if (flip != cull_flip)
	{
		cull_flip = flip;
		ps3gl.dirty |= PS3GL_DIRTY_CULL;
		PS3_Log("[gl] cull flip %s", flip ? "ON" : "off");
	}
}

int
ps3gl_cull_flip(void)
{
	return cull_flip;
}

void
PS3GL_BeginFrame(void)
{
	int i;

	if (!ready)
	{
		return;
	}

	PS3_Video_BindRenderTarget();

	ps3gl.in_begin = 0;
	ps3gl.drew = 0;
	ps3gl.frame++;
	ps3gl.ring_seg = ps3gl.frame % PS3GL_RING_SEGMENTS;

	/* the segment was last used PS3GL_RING_SEGMENTS frames ago */
	if (ps3gl.frame > PS3GL_RING_SEGMENTS)
	{
		WaitForFrame(ps3gl.frame - PS3GL_RING_SEGMENTS);
	}

	if (ps3gl.ring_head > ps3gl.ring_peak)
	{
		ps3gl.ring_peak = ps3gl.ring_head;
	}

	ps3gl.ring_head = 0;

	/* Everything goes to the RSX again: the surface changed, and system
	   overlays may have touched the programs. */
	ps3gl.dirty = PS3GL_DIRTY_ALL;
	ps3gl.vp_loaded = 0;
	ps3gl.active_fp = -1;
	ps3gl.mvp_uploaded = 0;

	for (i = 0; i < PS3GL_MAX_TMUS; i++)
	{
		ps3gl.tmu[i].hw_tex = -2;
		ps3gl.tmu[i].hw_enabled = -1;
	}

	ps3gl_process_frees();

	/* (patch 19 timed the RSX here with rsxSetTimeStamp: PSL1GHT's writes
	   its method and argument one word apart, garbage in between, and the
	   RSX stopped on the first frame. No RSX timing.) */

	/* every 10 s (a level running at 60 fps), a few draws of the frame
	   are logged with what the shaders get */
	/* (ps3gl_sample > 0 logs that many draws with what the shaders get;
	   patch 13..16 did 12 every 10 s. Off: the log writes made hitches
	   and filled the log.) */
	ps3gl_sample = 0;

	/* (PS3GL_SelfTest: off since patch 20. Its readback never matched the
	   probes it drew, and the perspective bit question is settled: mode 1,
	   bit cleared, on hardware since patch 15.) */
}

void
PS3GL_EndFrame(void)
{
	if (!ready)
	{
		return;
	}

	/* Written by the RSX once everything before it is drawn. */
	rsxSetWriteBackendLabel(ps3gl.ctx, PS3GL_LABEL_FRAME, ps3gl.frame);
}

void
PS3GL_LogStats(int frames)
{
	/* frames 0: just reset (the periodic log is off) */
	if (frames > 0)
	{
		PS3_Log("[gl]   %u draws, %u verts, %u KB uploaded, "
				"%u copies per frame; ring peak %u KB; textures %u MB%s",
				(unsigned)(ps3gl.st_draws / frames), (unsigned)(ps3gl.st_verts / frames),
				(unsigned)(ps3gl.st_upload / frames / 1024),
				(unsigned)(ps3gl.st_copies / frames), (unsigned)(ps3gl.ring_peak / 1024),
				(unsigned)(ps3gl.st_tex_bytes >> 20),
				ps3gl.st_dropped ? " -- DRAWS DROPPED (ring full)" : "");
	}

	ps3gl.st_draws = ps3gl.st_verts = ps3gl.st_upload = ps3gl.st_dropped = 0;
	ps3gl.st_copies = 0;
	ps3gl.ring_peak = 0;
	ps3gl.st_gpu_ns = ps3gl.st_gpu_max_ns = 0;
	ps3gl.st_gpu_frames = 0;
}

/* ================================================================ */
/* Queries and things that do nothing here                            */
/* ================================================================ */

const GLubyte * APIENTRY
glGetString(GLenum name)
{
	switch (name)
	{
		case GL_VENDOR:     return (const GLubyte *)"TyrQuakeCell";
		case GL_RENDERER:   return (const GLubyte *)"RSX (ps3gl)";
		/* 2.1 for EDuke32: its GLSL program (Polymost's, run as Cg
		   programs, ps3gl_prog.c) and buffer objects (in main memory).
		   No texture compression, no NPOT textures, no sync objects. */
		case GL_VERSION:    return (const GLubyte *)"2.1 ps3gl";
		case GL_EXTENSIONS: return (const GLubyte *)"GL_ARB_multitexture GL_ARB_texture_env_combine "
				"GL_EXT_draw_range_elements GL_ARB_shader_objects GL_ARB_vertex_buffer_object "
				"GL_EXT_bgra GL_ARB_texture_non_power_of_two GL_ARB_depth_clamp";
		default:            return (const GLubyte *)"";
	}
}

GLenum APIENTRY
glGetError(void)
{
	return GL_NO_ERROR;
}

void APIENTRY
glGetIntegerv(GLenum pname, GLint *params)
{
	if (!params)
	{
		return;
	}

	switch (pname)
	{
		case GL_MAX_TEXTURE_SIZE:  *params = 4096; break;
		case GL_MAX_TEXTURE_UNITS: *params = PS3GL_MAX_TMUS; break;
		case GL_MAX_ELEMENTS_VERTICES:
		case GL_MAX_ELEMENTS_INDICES:
			*params = 65536;
			break;
		case GL_DEPTH_BITS:        *params = 24; break;
		case GL_STENCIL_BITS:      *params = 8; break;
		case 0x8894: /* GL_ARRAY_BUFFER_BINDING */
			*params = (GLint)ps3gl.array_buffer;
			break;
		case 0x8895: /* GL_ELEMENT_ARRAY_BUFFER_BINDING */
			*params = (GLint)ps3gl.element_buffer;
			break;
		default:                   *params = 0; break;
	}
}

void APIENTRY
glGetBooleanv(GLenum pname, GLboolean *params)
{
	if (!params)
	{
		return;
	}

	switch (pname)
	{
		case GL_DEPTH_WRITEMASK:
			params[0] = ps3gl.rs.depth_mask ? GL_TRUE : GL_FALSE;
			break;
		case GL_COLOR_WRITEMASK:
			params[0] = ps3gl.rs.mask_r;
			params[1] = ps3gl.rs.mask_g;
			params[2] = ps3gl.rs.mask_b;
			params[3] = ps3gl.rs.mask_a;
			break;
		default:
			params[0] = GL_FALSE;
			break;
	}
}

/* Waiting for the RSX here would only stall the frame (gl_finish). */
void APIENTRY glFinish(void) {}
void APIENTRY glFlush(void) {}
void APIENTRY glDrawBuffer(GLenum mode) { (void)mode; }
void APIENTRY glReadBuffer(GLenum mode) { (void)mode; }
void APIENTRY glHint(GLenum target, GLenum mode) { (void)target; (void)mode; }
void APIENTRY glPointSize(GLfloat size) { (void)size; }
void APIENTRY glLineWidth(GLfloat width) { (void)width; }

/* Fog: only kept for Polymost's program (gl_Fog in GLSL), the fixed
   function programs have no fog stage. */
void APIENTRY
glFogf(GLenum pname, GLfloat param)
{
	switch (pname)
	{
		case GL_FOG_START: ps3gl.fog_start = param; break;
		case GL_FOG_END:   ps3gl.fog_end = param; break;
		default:           break;   /* mode, density: GLSL uses start/end */
	}
}

void APIENTRY
glFogi(GLenum pname, GLint param)
{
	glFogf(pname, (GLfloat)param);
}

void APIENTRY
glFogfv(GLenum pname, const GLfloat *params)
{
	if (!params)
	{
		return;
	}

	if (pname == GL_FOG_COLOR)
	{
		memcpy(ps3gl.fog_color, params, sizeof(ps3gl.fog_color));
	}
	else
	{
		glFogf(pname, params[0]);
	}
}

void APIENTRY
glGetTexParameterfv(GLenum target, GLenum pname, GLfloat *params)
{
	(void)target;
	(void)pname;

	if (params)
	{
		params[0] = 1.0f;
	}
}

/* Black. Patch 22 had the RSX copy the render target into main memory
   for the savegame's picture: saving froze on hardware. The savegame
   picture stays black (not a priority). */
void APIENTRY
glReadPixels(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format,
		GLenum type, GLvoid *pixels)
{
	int bpp = (format == GL_RGBA || format == GL_BGRA) ? 4 : 3;

	(void)x;
	(void)y;
	(void)type;

	if (pixels && width > 0 && height > 0)
	{
		memset(pixels, 0, (size_t)width * height * bpp);
	}
}
