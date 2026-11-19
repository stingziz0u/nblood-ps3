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
 * ps3gl: the OpenGL 1.3 subset TyrQuake's GL renderer uses, on top of the
 * RSX. Quake2PS3's ps3gl (made for Yamagi's ref_gl1), with what GLQuake
 * needs on top:
 *
 *  - Immediate mode (glBegin/glEnd), for the 2D drawing, sprites and the
 *    screen blends. Quads and polygons become triangles.
 *  - Three texture units and GL's texture environments as TyrQuake
 *    combines them (REPLACE, MODULATE, COMBINE x2/x4, DECAL): one
 *    generated fragment program per combination (shaders/gen_fp.py).
 *  - glCopyTexSubImage2D, for the water warp textures (rendered into the
 *    corner of the frame, then copied and swizzled by the RSX).
 *  - Texture names are handed out and taken back (TyrQuake frees and
 *    reallocates them on every map).
 *
 * Quake2PS3's ps3gl is itself based on ps3gl from IoQuake3-PS3 (GPLv2,
 * code/gl/), which runs Quake 3 on real hardware: same state tracking,
 * matrix stacks, vertex format, shaders and GCM calls. Changed for Quake 2
 * and for what Doom64-PS3 taught us on hardware:
 *
 *  - Real client arrays: glDrawArrays (all primitive types) and
 *    glDrawElements with any start index. ref_gl1 draws everything this
 *    way (no glBegin/glEnd).
 *  - Power of two textures are SWIZZLED, with their mip levels packed one
 *    after the other: linear textures only work with CLAMP, and Quake 2
 *    repeats its wall textures. Non power of two ones (the cinematics)
 *    stay linear and clamped.
 *  - Texture names index the table directly, no linear search per bind
 *    (glGenTextures hands them out here).
 *  - A frame fence (backend label): a texture the RSX may still be reading
 *    is never overwritten by glTexImage2D (new storage is allocated, the
 *    old one freed later), and freed memory waits for the RSX too.
 *  - GL_UNPACK_ROW_LENGTH and GL_RGB_SCALE (lightmap updates, overbright).
 *
 * The vertex ring has one segment per frame in flight; everything is
 * copied there (the game's arrays live in ordinary memory).
 *
 * For EDuke32 (ps3gl_prog.c): GLSL program objects (Polymost's program
 * runs as Cg programs written for it), buffer objects in main memory,
 * fog for that program, five texture units, and every entry point by
 * name for glad (ps3gl_procs.c). glGetString says 2.1.
 *
 * =======================================================================
 */

#ifndef PS3GL_H
#define PS3GL_H

#include <stdint.h>
#include <string.h>

#include <ppu-types.h>
#include <rsx/rsx.h>
#include <rsx/gcm_sys.h>

#include "GL/gl.h"

#ifndef GL_DEPTH_CLAMP
#define GL_DEPTH_CLAMP 0x864F
#endif

/* Units 3 and 4 only exist for Polymost (detail and glow maps, which
   aren't drawn); the fixed function programs use 0-2. */
#define PS3GL_MAX_TMUS          5
#define PS3GL_MAX_TEXTURES      8192    /* texture names 1..8191 */
#define PS3GL_MATRIX_DEPTH      32

/* One ring segment per frame that can be in flight (triple buffering). */
#define PS3GL_RING_SEGMENTS     3
#define PS3GL_RING_SEG_SIZE     (6 * 1024 * 1024)

/* GCM labels 0-63 are the system's. IoQuake3-PS3 uses 64-66. */
#define PS3GL_LABEL_FRAME       68
#define PS3GL_TS_BASE           200     /* RSX timestamp reports 200..207 */

/* Frames a texture memory block waits after its last use before being
   freed or reused, on top of the fence (belt and braces). */
#define PS3GL_FREE_DELAY        2

/* Dirty flags for the render state */
#define PS3GL_DIRTY_BLEND       0x0001u
#define PS3GL_DIRTY_ALPHA       0x0002u
#define PS3GL_DIRTY_DEPTH       0x0004u
#define PS3GL_DIRTY_STENCIL     0x0008u
#define PS3GL_DIRTY_VIEWPORT    0x0010u
#define PS3GL_DIRTY_CULL        0x0020u
#define PS3GL_DIRTY_SCISSOR     0x0040u
#define PS3GL_DIRTY_COLORMASK   0x0080u
#define PS3GL_DIRTY_POLYOFFSET  0x0100u
#define PS3GL_DIRTY_SHADE       0x0200u
#define PS3GL_DIRTY_ALL         0xFFFFu

/* Fragment programs (shaders/gen_fp.py, ps3gl_shader_data.h): one per
   combination of what the texture units do, index u0 + 4 * (u1 + 5 * u2). */
enum { PS3GL_U0_OFF, PS3GL_U0_REPLACE, PS3GL_U0_MODULATE, PS3GL_U0_MODULATE2X };
enum { PS3GL_U1_OFF, PS3GL_U1_MODULATE, PS3GL_U1_MODULATE2X, PS3GL_U1_MODULATE4X,
	PS3GL_U1_DECAL };
enum { PS3GL_U2_OFF, PS3GL_U2_DECAL };
#define PS3GL_FP_INDEX(u0, u1, u2) ((u0) + 4 * ((u1) + 5 * (u2)))
#define PS3GL_FP_COUNT 40

/* active_fp / vp_loaded values when Polymost's programs are loaded */
#define PS3GL_FP_POLYMOST       1000
#define PS3GL_VP_FIXED          1
#define PS3GL_VP_POLYMOST       2

/* 36 byte interleaved vertex, as in IoQuake3-PS3 */
#pragma pack(push, 1)
typedef struct {
	float x, y, z, w;
	float u0, v0;
	float u1, v1;
	uint32_t color;         /* bytes R G B A */
} ps3gl_vertex_t;
#pragma pack(pop)

#define PS3GL_VERTEX_SIZE       36
#define PS3GL_VATTR_POS_OFF     0
#define PS3GL_VATTR_TC0_OFF     16
#define PS3GL_VATTR_TC1_OFF     24
#define PS3GL_VATTR_COLOR_OFF   32

typedef struct {
	uint8_t    *data;           /* RSX memory, NULL = no storage */
	uint32_t    offset;
	uint32_t    size;
	uint32_t    pitch;          /* linear only */
	uint16_t    width, height;  /* level 0 */
	uint8_t     swizzled;
	uint8_t     max_levels;     /* levels the storage has room for */
	uint8_t     num_levels;     /* levels uploaded (contiguous from 0) */
	uint8_t     dirty;          /* descriptor or sampler state changed */
	uint8_t     allocated;      /* name handed out by glGenTextures */
	uint8_t     max_level;      /* GL_TEXTURE_MAX_LEVEL + 1, 0 = not set */
	GLenum      min_filter, mag_filter, wrap_s, wrap_t;
	uint32_t    last_frame;     /* last frame that drew with it */
	gcmTexture  gt;
} ps3gl_texture_t;

typedef struct {
	int         tex;            /* bound texture name */
	int         enabled;        /* GL_TEXTURE_2D */
	GLenum      env;            /* GL_TEXTURE_ENV_MODE */
	int         rgb_scale;      /* GL_RGB_SCALE (GL_COMBINE) */
	int         hw_tex;         /* what the sampler has: name, -1 none */
	int         hw_enabled;
} ps3gl_tmu_t;

typedef struct {
	float       stack[PS3GL_MATRIX_DEPTH][16];
	int         depth;
} ps3gl_matstack_t;

typedef struct {
	int         enabled;
	GLint       size;
	GLenum      type;
	GLsizei     stride;
	const void *ptr;            /* offset into vbo if vbo != 0 */
	GLuint      vbo;            /* GL_ARRAY_BUFFER when the pointer was set */
} ps3gl_array_t;

typedef struct {
	gcmContextData *ctx;
	int         screen_w, screen_h;     /* the render target */

	uint32_t    frame;                  /* frame being built, from 1 */
	volatile uint32_t *frame_label;     /* last frame the RSX finished */

	uint32_t    dirty;
	struct {
		int      blend;
		GLenum   blend_src, blend_dst;
		int      alpha_test;
		GLenum   alpha_func;
		float    alpha_ref;
		int      depth_test, depth_mask;
		int      depth_clamp;   /* GL_DEPTH_CLAMP (GL_ARB_depth_clamp) */
		GLenum   depth_func;
		int      cull;
		GLenum   cull_face, front_face;
		int      scissor;
		int      sc_x, sc_y, sc_w, sc_h;    /* GL convention */
		int      vp_x, vp_y, vp_w, vp_h;    /* GL convention */
		float    depth_near, depth_far;
		int      mask_r, mask_g, mask_b, mask_a;
		int      polyoffset;
		float    po_factor, po_units;
		GLenum   shade;
		int      stencil;
		GLenum   st_func;
		GLint    st_ref;
		GLuint   st_mask, st_writemask;
		GLenum   st_fail, st_zfail, st_zpass;
		uint32_t clear_color;               /* ARGB */
		float    clear_depth;
		int      clear_stencil;
	} rs;

	/* texture units */
	int         active_tmu, client_tmu;
	ps3gl_tmu_t tmu[PS3GL_MAX_TMUS];
	int         unpack_row_length;
	int         tex_written;            /* CPU wrote texture memory */

	/* matrices (the texture matrix is kept but not used) */
	GLenum      matrix_mode;
	ps3gl_matstack_t mv, proj, tex;
	int         mvp_dirty;
	float       mvp[16];                /* row major, for the VP */
	int         mvp_uploaded;

	/* client arrays */
	ps3gl_array_t va_vertex, va_color, va_texcoord[PS3GL_MAX_TMUS];
	uint32_t    color;                  /* current color, bytes R G B A */

	/* immediate mode: glBegin .. glEnd */
	int         in_begin;
	GLenum      im_mode;
	float       im_tc[2];               /* current texture coordinate */

	/* drawn since the RSX was last made to wait (glCopyTexSubImage2D) */
	int         drew;
	/* a copy into a texture is queued: wait for it before the next draw */
	int         copy_pending;

	/* vertex ring */
	uint8_t    *ring;
	uint32_t    ring_off;
	uint32_t    ring_seg, ring_head;
	uint32_t    ring_peak;

	/* shaders */
	int         active_fp;
	int         vp_loaded;              /* 0, PS3GL_VP_FIXED or _POLYMOST */

	/* GLSL program objects (ps3gl_prog.c): 0 = fixed function */
	GLuint      program;
	int         program_polymost;       /* the current one is Polymost's */

	/* buffer objects (ps3gl_prog.c), in main memory */
	GLuint      array_buffer, element_buffer;

	/* fog, as GLSL's gl_Fog sees it */
	float       fog_start, fog_end;
	float       fog_color[4];

	ps3gl_texture_t *textures;          /* [PS3GL_MAX_TEXTURES] */

	/* statistics, reset by PS3GL_LogStats */
	uint32_t    st_draws, st_verts, st_upload, st_dropped, st_copies;
	uint32_t    st_tex_bytes;
	uint64_t    st_gpu_ns, st_gpu_max_ns;   /* RSX time per frame (timestamps) */
	uint32_t    st_gpu_frames;
} ps3gl_state_t;

extern ps3gl_state_t ps3gl;

/* ---- used by the platform (vid_ps3_gl.c, ps3_video.c) ---- */

/* Once the render target exists (PS3_Video_SetRenderTarget). Sets the
   size again on a mode change. 0 ok, -1 error. */
int  PS3GL_Init(int width, int height);
int  PS3GL_Ready(void);

/* Frame bracket. Begin binds the render target (waiting for a free one)
   and resets the per frame state; End closes the frame (fence). The
   caller then presents with PS3_Video_Present(). */
void PS3GL_BeginFrame(void);
void PS3GL_EndFrame(void);

/* One log line with the averages since the last call. */
void PS3GL_LogStats(int frames);   /* 0: only resets the counters */

/* Polymost's fragment program: 0 the result, 1.. debug views of its
   steps (ps3gl_prog.c, shaders/polymost_fp.fcg). */
void PS3GL_SetPolymostView(int view);
int  PS3GL_PolymostView(void);

/* The short versions of Polymost's fragment program where a draw allows
   (on by default; SELECT+L3 to compare). */
void PS3GL_SetFastShaders(int on);
int  PS3GL_FastShaders(void);

/* Draws probes with Polymost's programs, reads them back from the render
   target and logs what the fragment programs got (ps3gl_prog.c). Once,
   from PS3GL_BeginFrame. */
void PS3GL_SelfTest(void);

/* Culling direction check (cvar gl_ps3_cullflip). */
void PS3GL_SetCullFlip(int flip);

/* For gladLoadGLLoader: every GL entry point by name (ps3gl_procs.c).
   Names ps3gl doesn't have get a function that does nothing. */
void *PS3GL_GetProcAddress(const char *name);

/* ---- internal ---- */

uint32_t ps3gl_completed_frame(void);
int      ps3gl_cull_flip(void);
void     ps3gl_defer_free(void *data, uint32_t last_frame);
void     ps3gl_process_frees(void);
void    *ps3gl_rsx_memalign(uint32_t align, uint32_t size);

void     ps3gl_states_reset(void);
void     ps3gl_apply_states(void);
void     ps3gl_matrices_reset(void);
void     ps3gl_apply_matrices(void);
void     ps3gl_textures_init(void);
void     ps3gl_apply_textures(unsigned units);   /* bit i: sampler i used */
void     ps3gl_shaders_init(void);
void     ps3gl_apply_shader(int fp);

/* ps3gl_draw.c: a triangle fan of ready vertices, with whatever programs
   and state the RSX has (the self test). 0 if the ring is full. */
int      ps3gl_draw_fan(const ps3gl_vertex_t *v, int count);

/* ps3gl_prog.c */
void     ps3gl_prog_init(void);
void     ps3gl_prog_apply(void);     /* Polymost's programs and constants */
const void *ps3gl_buffer_ptr(GLuint vbo, const void *offset);
const void *ps3gl_element_ptr(const void *indices);
long     ps3gl_buffer_size(GLuint vbo);
unsigned ps3gl_texel(const ps3gl_texture_t *t, int x, int y);   /* red, level 0 */
extern int ps3gl_sample;   /* sample draw logging, see ps3gl_draw.c */

#endif /* PS3GL_H */
