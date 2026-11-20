/*
 * Copyright (C) 2026 the EDuke32-PS3 contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * =======================================================================
 *
 * ps3gl: what EDuke32's Polymost needs on top of OpenGL 1.3.
 *
 *  - GLSL program and shader objects. There is no GLSL compiler on the
 *    PS3: a program whose fragment shader is Polymost's (polymost1Frag,
 *    told apart by its uniforms) runs as the Cg programs written for it,
 *    shaders/polymost_vp.vcg and polymost_fp.fcg, compiled into
 *    ps3gl_polymost_data.h. Uniforms are found by name and kept per
 *    program, like GL does; glUseProgram(0) is the fixed function again.
 *
 *  - Buffer objects, in main memory: every draw copies its vertices into
 *    the vertex ring anyway (ps3gl_draw.c), so a buffer is only a place
 *    the pointers can point into.
 *
 * The fragment program's uniforms go through the vertex program (see
 * polymost_vp.vcg): the RSX keeps fragment program constants inside the
 * program's code, vertex program constants are a few words in the command
 * buffer. Only the constants that changed since the last draw are sent.
 *
 * Perspective bit: PSL1GHT's cgcomp sets bit 31 of the fourth word of
 * every MUL / DP / DIV that reads a texture coordinate input ("perspective
 * correction off"); Polymost's tile lookup multiplies by
 * u_texturePosSize.zw like that, and every indexed tile came out as one
 * texel. The programs are kept in three versions (PM_MODES): as cgcomp
 * made them, the bit cleared everywhere, the bit set on every texture
 * coordinate read. PS3GL_SelfTest draws probes with each, reads the
 * result back from the render target and picks the one that is right.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>

#include "ps3gl.h"
#include "ps3gl_polymost_data.h"
#include "../ps3_platform.h"
#include "../ps3_video.h"

#ifndef GL_FRAGMENT_SHADER
#define GL_FRAGMENT_SHADER      0x8B30
#define GL_VERTEX_SHADER        0x8B31
#define GL_COMPILE_STATUS       0x8B81
#define GL_LINK_STATUS          0x8B82
#define GL_INFO_LOG_LENGTH      0x8B84
#define GL_SHADER_TYPE          0x8B4F
#endif

/* ================================================================ */
/* Shader and program objects                                         */
/* ================================================================ */

#define MAX_SHADERS     32
#define MAX_PROGRAMS    16

enum { KIND_UNKNOWN, KIND_POLYMOST_VERT, KIND_POLYMOST_FRAG };

/* The uniforms of Polymost's shaders (polymost1Vert/Frag.glsl): their
   index here is their location. */
enum {
	U_S_TEXTURE, U_S_PALSWAP, U_S_PALETTE, U_S_DETAIL, U_S_GLOW,
	U_TEXTUREPOSSIZE, U_HALFTEXELSIZE, U_PALSWAPPOS, U_PALSWAPSIZE, U_CLAMP,
	U_SHADE, U_NUMSHADES, U_VISFACTOR, U_FOGENABLED, U_USECOLORONLY,
	U_USEPALETTE, U_USEDETAILMAPPING, U_USEGLOWMAPPING, U_NPOTEMULATION,
	U_ROTMATRIX, U_SHADEINTERPOLATE, U_COLORCORRECTION,
	U_COUNT
};

static const char *const uniform_names[U_COUNT] = {
	"s_texture", "s_palswap", "s_palette", "s_detail", "s_glow",
	"u_texturePosSize", "u_halfTexelSize", "u_palswapPos", "u_palswapSize", "u_clamp",
	"u_shade", "u_numShades", "u_visFactor", "u_fogEnabled", "u_useColorOnly",
	"u_usePalette", "u_useDetailMapping", "u_useGlowMapping", "u_npotEmulation",
	"u_rotMatrix", "u_shadeInterpolate", "u_colorCorrection",
};

typedef struct {
	int     used;
	GLenum  type;
	int     kind;
} shader_t;

typedef struct {
	int     used;
	int     vert, frag;         /* kinds attached */
	int     polymost;           /* linked as Polymost's program */
	float   u[U_COUNT][16];
} program_t;

static shader_t shaders[MAX_SHADERS];
static program_t programs[MAX_PROGRAMS];

static rsxVertexProgram *pm_vp;
static void *pm_vp_ucode;

/* the perspective bit (see the top): as cgcomp left it, cleared on every
   instruction, set on every one that reads a texture coordinate */
#define PM_MODES 3
static const char *const mode_names[PM_MODES] = {
	"as cgcomp made it", "bit 31 cleared everywhere", "bit 31 set on every texcoord read",
};
static int pm_mode = 1;     /* the views' mode; PS3GL_SelfTest may change it */

/* the fragment programs (result and debug views), each in every mode */
static rsxFragmentProgram *pm_fp[PS3GL_POLYMOST_VIEWS];
static u32 pm_fp_offset[PS3GL_POLYMOST_VIEWS][PM_MODES];   /* copies in RSX memory */
static int pm_fp_ok[PS3GL_POLYMOST_VIEWS][PM_MODES];

/* the debug views (PS3GL_SetPolymostView; on no button since NBlood-PS3
   patch 31, L3+R3 skips levels in the debug builds): a fragment program
   and its mode (-1: pm_mode) */
typedef struct {
	int fp, mode;
	const char *name;
} pm_viewdef_t;

static const pm_viewdef_t pm_views[] = {
	{ 0, -1, "normal" },
	{ 0,  0, "normal, perspective bit as cgcomp made it (patch 14)" },
	{ 0,  1, "normal, perspective bit cleared everywhere" },
	{ 0,  2, "normal, perspective bit set on every texcoord read" },
	{ 1, -1, "1: texture coordinate (red/green)" },
	{ 2, -1, "2: tile sampling coordinate (red/green), usePalette (blue)" },
	{ 3, -1, "3: texture as sampled (indexed: the index as grey)" },
	{ 4, -1, "4: palette color" },
	{ 5, -1, "5: vertex color" },
	{ 6, -1, "6: usePalette (red), useColorOnly (green), fog factor (blue)" },
	{ 7, -1, "7: shade (red), shade table index (green), palswap size (blue)" },
	{ 8, -1, "8: the tile's center texel, through the palette" },
	{ 9, -1, "9: the whole texture over the polygon, through the palette" },
};
#define PM_VIEWS ((int)(sizeof(pm_views) / sizeof(pm_views[0])))
static int pm_view;

/* the result without what the draw doesn't use (polymost_fp.fcg FAST 1/2),
   in pm_mode; SELECT+L3 turns them off to compare */
static u32 pm_fast_offset[PS3GL_POLYMOST_FAST];
static int pm_fast_ok[PS3GL_POLYMOST_FAST];
static int pm_fast_on = 1;

void
PS3GL_SetFastShaders(int on)
{
	pm_fast_on = on ? 1 : 0;
	ps3gl.active_fp = -1;
	PS3_Log("[gl] Polymost fast shaders %s", pm_fast_on ? "on" : "off");
}

int
PS3GL_FastShaders(void)
{
	return pm_fast_on;
}

int ps3gl_sample;   /* > 0: this many more draws are logged (ps3gl_draw.c) */

void
PS3GL_SetPolymostView(int view)
{
	const pm_viewdef_t *v;

	pm_view = ((view % PM_VIEWS) + PM_VIEWS) % PM_VIEWS;
	v = &pm_views[pm_view];
	ps3gl.active_fp = -1;   /* load the new one at the next draw */
	PS3_Log("[gl] Polymost shader view %s (perspective bit: %s)", v->name,
			mode_names[v->mode >= 0 ? v->mode : pm_mode]);
}

int
PS3GL_PolymostView(void)
{
	return pm_view;
}

/* Logical instruction word i (0..3) of the fragment program code at p:
   cgcomp stores each word with its halves swapped. */
static u32
FPWord(const u8 *p, int i)
{
	u32 v = ((u32)p[i * 4] << 24) | ((u32)p[i * 4 + 1] << 16) |
		((u32)p[i * 4 + 2] << 8) | (u32)p[i * 4 + 3];

	return (v >> 16) | (v << 16);
}

/* Applies a perspective bit mode to a copy of fragment program code.
   Gives how many instructions read a texture coordinate. */
static int
PatchPerspective(u8 *code, u32 size, int mode)
{
	u32 pos = 0;
	int reads = 0;

	while (pos + 16 <= size)
	{
		u8 *p = code + pos;
		u32 w0 = FPWord(p, 0);
		u32 in = (w0 >> 13) & 15;
		int i, input = 0, constant = 0;

		for (i = 1; i <= 3; i++)
		{
			u32 type = FPWord(p, i) & 3;

			input |= (type == 1);
			constant |= (type == 2);
		}

		input = input && in >= 4 && in <= 11;   /* TEXCOORD0..7 */
		reads += input;

		/* logical bit 31 of word 3 = bit 7 of its byte 2 as stored */
		if (mode == 1 || (mode == 2 && !input))
		{
			p[14] &= 0x7f;
		}
		else if (mode == 2)
		{
			p[14] |= 0x80;
		}

		if (w0 & 1)
		{
			break;      /* the last one */
		}

		pos += constant ? 32 : 16;  /* a constant follows the instruction */
	}

	return reads;
}

/* A copy of a fragment program's code in RSX memory, in a mode */
static int
LoadFPCopy(const rsxFragmentProgram *fp, int mode, u32 *offset)
{
	void *ucode, *copy;
	u32 size;

	rsxFragmentProgramGetUCode((rsxFragmentProgram *)fp, &ucode, &size);
	copy = rsxMemalign(64, size);

	if (!copy)
	{
		return 0;
	}

	memcpy(copy, ucode, size);
	PatchPerspective((u8 *)copy, size, mode);
	rsxAddressToOffset(copy, offset);

	return 1;
}

/* the vertex program's constants m0 .. u7, as polymost_vp.vcg declares
   them: 16 float4 */
#define PM_CONSTS 16
static const char *const pm_const_names[PM_CONSTS] = {
	"m0", "m1", "m2", "m3", "e2", "e3", "t0", "t1", "fog",
	"u1", "u2", "u3", "u4", "u5", "u6", "u7",
};
static rsxProgramConst *pm_const[PM_CONSTS];
static int pm_const_base = -1;      /* contiguous from here: one block */
static float pm_sent[PM_CONSTS][4];
static int pm_sent_valid;

static void
ProgramDefaults(program_t *p)
{
	int i;

	memset(p->u, 0, sizeof(p->u));

	/* what Polymost sets anyway, in case a draw comes first */
	for (i = 0; i < 4; i++)
	{
		p->u[U_ROTMATRIX][i * 5] = 1.0f;
	}

	p->u[U_COLORCORRECTION][0] = p->u[U_COLORCORRECTION][1] = p->u[U_COLORCORRECTION][2] = 1.0f;
	p->u[U_NPOTEMULATION][1] = p->u[U_NPOTEMULATION][3] = 1.0f;
	p->u[U_NUMSHADES][0] = 1.0f;
	p->u[U_NUMSHADES][1] = 1.0f;
	p->u[U_TEXTUREPOSSIZE][2] = p->u[U_TEXTUREPOSSIZE][3] = 1.0f;
}

void
ps3gl_prog_init(void)
{
	void *ucode;
	u32 size;
	int i, contiguous = 1;

	pm_vp = (rsxVertexProgram *)polymost_vp_vpo;
	rsxVertexProgramGetUCode(pm_vp, &pm_vp_ucode, &size);

	for (i = 0; i < PM_CONSTS; i++)
	{
		pm_const[i] = rsxVertexProgramGetConst(pm_vp, pm_const_names[i]);

		if (!pm_const[i] || (int)pm_const[i]->index != (int)pm_const[0]->index + i)
		{
			contiguous = 0;
		}
	}

	/* (the registers are the program's const_start on, as
	   rsxSetVertexProgramParameter counts them) */
	pm_const_base = (contiguous && pm_const[0]) ?
		(int)pm_vp->const_start + (int)pm_const[0]->index : -1;

	for (i = 0; i < PS3GL_POLYMOST_VIEWS; i++)
	{
		int m;

		pm_fp[i] = (rsxFragmentProgram *)polymost_fp_views[i];

		for (m = 0; m < PM_MODES; m++)
		{
			pm_fp_ok[i][m] = LoadFPCopy(pm_fp[i], m, &pm_fp_offset[i][m]);
		}

		if (i == 0)
		{
			int f;

			for (f = 0; f < PS3GL_POLYMOST_FAST; f++)
			{
				pm_fast_ok[f] = LoadFPCopy((const rsxFragmentProgram *)polymost_fast[f], pm_mode,
						&pm_fast_offset[f]);
			}

			rsxFragmentProgramGetUCode(pm_fp[0], &ucode, &size);
			PS3_Log("[gl] Polymost programs: vertex constants %s (base %d), fragment program "
					"%u bytes (%d texcoord reads)%s, %d debug views",
					contiguous ? "in one block" : "one by one", pm_const_base, (unsigned)size,
					PatchPerspective((u8 *)ucode, size, 0),
					pm_fp_ok[0][0] ? "" : " -- NO RSX MEMORY", PM_VIEWS);
		}
	}
}

GLuint APIENTRY
glCreateShader(GLenum type)
{
	int i;

	for (i = 1; i < MAX_SHADERS; i++)
	{
		if (!shaders[i].used)
		{
			shaders[i].used = 1;
			shaders[i].type = type;
			shaders[i].kind = KIND_UNKNOWN;
			return (GLuint)i;
		}
	}

	PS3_Log("[gl] WARNING: out of shader objects");
	return 0;
}

void APIENTRY
glDeleteShader(GLuint shader)
{
	if (shader > 0 && shader < MAX_SHADERS)
	{
		memset(&shaders[shader], 0, sizeof(shaders[shader]));
	}
}

GLboolean APIENTRY
glIsShader(GLuint shader)
{
	return (shader > 0 && shader < MAX_SHADERS && shaders[shader].used) ? GL_TRUE : GL_FALSE;
}

/* Which shader it is, from its text (the whole of it, as the strings
   given) */
void APIENTRY
glShaderSource(GLuint shader, GLsizei count, const char *const *string, const GLint *length)
{
	shader_t *s;
	int i, has_palswap = 0, has_rot = 0;

	if (shader == 0 || shader >= MAX_SHADERS || !shaders[shader].used || !string)
	{
		return;
	}

	s = &shaders[shader];

	for (i = 0; i < count; i++)
	{
		const char *text = string[i];
		size_t len;
		char *copy;

		if (!text)
		{
			continue;
		}

		len = (length && length[i] >= 0) ? (size_t)length[i] : strlen(text);
		copy = (char *)malloc(len + 1);

		if (!copy)
		{
			continue;
		}

		memcpy(copy, text, len);
		copy[len] = 0;

		if (strstr(copy, "u_palswapPos"))
		{
			has_palswap = 1;
		}

		if (strstr(copy, "u_rotMatrix"))
		{
			has_rot = 1;
		}

		free(copy);
	}

	if (s->type == GL_FRAGMENT_SHADER && has_palswap)
	{
		s->kind = KIND_POLYMOST_FRAG;
	}
	else if (s->type == GL_VERTEX_SHADER && has_rot)
	{
		s->kind = KIND_POLYMOST_VERT;
	}
	else
	{
		s->kind = KIND_UNKNOWN;
		PS3_Log("[gl] a %s shader ps3gl doesn't know: not run",
				s->type == GL_FRAGMENT_SHADER ? "fragment" : "vertex");
	}
}

void APIENTRY
glCompileShader(GLuint shader)
{
	(void)shader;
}

void APIENTRY
glGetShaderiv(GLuint shader, GLenum pname, GLint *params)
{
	if (!params)
	{
		return;
	}

	switch (pname)
	{
		case GL_COMPILE_STATUS:
			*params = (shader > 0 && shader < MAX_SHADERS &&
				shaders[shader].kind != KIND_UNKNOWN) ? GL_TRUE : GL_FALSE;
			break;
		case GL_SHADER_TYPE:
			*params = (shader > 0 && shader < MAX_SHADERS) ? (GLint)shaders[shader].type : 0;
			break;
		default:
			*params = 0;   /* GL_INFO_LOG_LENGTH: no log */
			break;
	}
}

void APIENTRY
glGetShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, char *infoLog)
{
	(void)shader;

	if (length)
	{
		*length = 0;
	}

	if (infoLog && bufSize > 0)
	{
		infoLog[0] = 0;
	}
}

GLuint APIENTRY
glCreateProgram(void)
{
	int i;

	for (i = 1; i < MAX_PROGRAMS; i++)
	{
		if (!programs[i].used)
		{
			memset(&programs[i], 0, sizeof(programs[i]));
			programs[i].used = 1;
			ProgramDefaults(&programs[i]);
			return (GLuint)i;
		}
	}

	PS3_Log("[gl] WARNING: out of program objects");
	return 0;
}

void APIENTRY
glDeleteProgram(GLuint program)
{
	if (program > 0 && program < MAX_PROGRAMS)
	{
		memset(&programs[program], 0, sizeof(programs[program]));

		if (ps3gl.program == program)
		{
			ps3gl.program = 0;
			ps3gl.program_polymost = 0;
		}
	}
}

GLboolean APIENTRY
glIsProgram(GLuint program)
{
	return (program > 0 && program < MAX_PROGRAMS && programs[program].used) ? GL_TRUE : GL_FALSE;
}

void APIENTRY
glAttachShader(GLuint program, GLuint shader)
{
	if (program == 0 || program >= MAX_PROGRAMS || shader == 0 || shader >= MAX_SHADERS)
	{
		return;
	}

	if (shaders[shader].type == GL_FRAGMENT_SHADER)
	{
		programs[program].frag = shaders[shader].kind;
	}
	else
	{
		programs[program].vert = shaders[shader].kind;
	}
}

void APIENTRY
glDetachShader(GLuint program, GLuint shader)
{
	(void)program;
	(void)shader;   /* the program keeps what it was linked with */
}

void APIENTRY
glLinkProgram(GLuint program)
{
	program_t *p;

	if (program == 0 || program >= MAX_PROGRAMS)
	{
		return;
	}

	p = &programs[program];
	p->polymost = (p->frag == KIND_POLYMOST_FRAG);

	PS3_Log("[gl] program %u linked: %s", (unsigned)program,
			p->polymost ? "Polymost's (Cg on the RSX)" : "unknown, draws with the fixed function");
}

void APIENTRY
glGetProgramiv(GLuint program, GLenum pname, GLint *params)
{
	if (!params)
	{
		return;
	}

	*params = (pname == GL_LINK_STATUS && program > 0 && program < MAX_PROGRAMS &&
		programs[program].polymost) ? GL_TRUE : 0;
}

void APIENTRY
glUseProgram(GLuint program)
{
	if (program >= MAX_PROGRAMS || !programs[program].used)
	{
		program = 0;
	}

	ps3gl.program = program;
	ps3gl.program_polymost = program && programs[program].polymost;
}

GLint APIENTRY
glGetUniformLocation(GLuint program, const char *name)
{
	int i;

	(void)program;

	if (!name)
	{
		return -1;
	}

	for (i = 0; i < U_COUNT; i++)
	{
		if (!strcmp(name, uniform_names[i]))
		{
			return i;
		}
	}

	return -1;
}

static float *
Uniform(GLint location)
{
	if (location < 0 || location >= U_COUNT || !ps3gl.program)
	{
		return NULL;
	}

	return programs[ps3gl.program].u[location];
}

void APIENTRY
glUniform1f(GLint location, GLfloat v0)
{
	float *u = Uniform(location);

	if (u)
	{
		u[0] = v0;
	}
}

void APIENTRY
glUniform2f(GLint location, GLfloat v0, GLfloat v1)
{
	float *u = Uniform(location);

	if (u)
	{
		u[0] = v0;
		u[1] = v1;
	}
}

void APIENTRY
glUniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)
{
	float *u = Uniform(location);

	if (u)
	{
		u[0] = v0;
		u[1] = v1;
		u[2] = v2;
		u[3] = v3;
	}
}

void APIENTRY
glUniform2fv(GLint location, GLsizei count, const GLfloat *v)
{
	if (count > 0 && v)
	{
		glUniform2f(location, v[0], v[1]);
	}
}

void APIENTRY
glUniform4fv(GLint location, GLsizei count, const GLfloat *v)
{
	if (count > 0 && v)
	{
		glUniform4f(location, v[0], v[1], v[2], v[3]);
	}
}

/* Samplers: the Cg program has them on fixed units (0, 1, 2), the ones
   Polymost gives them. */
void APIENTRY
glUniform1i(GLint location, GLint v0)
{
	glUniform1f(location, (GLfloat)v0);
}

void APIENTRY
glUniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)
{
	float *u = Uniform(location);
	int r, c;

	if (!u || count <= 0 || !value)
	{
		return;
	}

	if (!transpose)
	{
		memcpy(u, value, 16 * sizeof(float));
		return;
	}

	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			u[c * 4 + r] = value[r * 4 + c];
		}
	}
}

/* ---------------------------------------------------------------- */

/* out = a * b, column major */
static void
Mul(float *out, const float *a, const float *b)
{
	int c, r;

	for (c = 0; c < 4; c++)
	{
		for (r = 0; r < 4; r++)
		{
			out[c * 4 + r] = a[r] * b[c * 4] + a[4 + r] * b[c * 4 + 1] +
				a[8 + r] * b[c * 4 + 2] + a[12 + r] * b[c * 4 + 3];
		}
	}
}

static void
Row(float *dst, const float *m, int r)
{
	dst[0] = m[r];
	dst[1] = m[4 + r];
	dst[2] = m[8 + r];
	dst[3] = m[12 + r];
}

static void
Set4(float *dst, float a, float b, float c, float d)
{
	dst[0] = a;
	dst[1] = b;
	dst[2] = c;
	dst[3] = d;
}

/* Polymost's programs on the RSX and their constants for this draw */
void
ps3gl_prog_apply(void)
{
	gcmContextData *ctx = ps3gl.ctx;
	const program_t *p = &programs[ps3gl.program];
	float k[PM_CONSTS][4];
	float mvr[16], pmvr[16];
	float range;
	int first, last, i;

	if (ps3gl.vp_loaded != PS3GL_VP_POLYMOST)
	{
		rsxLoadVertexProgram(ctx, pm_vp, pm_vp_ucode);
		ps3gl.vp_loaded = PS3GL_VP_POLYMOST;
		ps3gl.mvp_uploaded = 0;   /* the fixed one's mvp is overwritten */
		pm_sent_valid = 0;
	}

	{
		const pm_viewdef_t *v = &pm_views[pm_view];
		int mode = v->mode >= 0 ? v->mode : pm_mode;
		int id = PS3GL_FP_POLYMOST + v->fp * PM_MODES + mode;
		int fast = 0;

		/* the plain result, and the draw needs no NPOT emulation and no
		   color correction: the short program (and without the second
		   shade lookup if shade interpolation is off) */
		if (pm_fast_on && v->fp == 0 && v->mode < 0 && p->u[U_USECOLORONLY][0] != 0.0f)
		{
			/* color only (fades, rectangles): no texture lookups at all */
			fast = pm_fast_ok[2] ? 3 : 0;
		}
		else if (pm_fast_on && v->fp == 0 && v->mode < 0 && p->u[U_NPOTEMULATION][2] == 0.0f &&
			p->u[U_COLORCORRECTION][0] == 1.0f && p->u[U_COLORCORRECTION][1] == 1.0f &&
			p->u[U_COLORCORRECTION][2] == 1.0f)
		{
			fast = p->u[U_SHADEINTERPOLATE][0] == 0.0f ? 2 : 1;

			if (!pm_fast_ok[fast - 1])
			{
				fast = 0;
			}
		}

		if (fast)
		{
			id = PS3GL_FP_POLYMOST + 900 + fast;

			if (ps3gl.active_fp != id)
			{
				rsxLoadFragmentProgramLocation(ctx, (const rsxFragmentProgram *)polymost_fast[fast - 1],
						pm_fast_offset[fast - 1], GCM_LOCATION_RSX);
				ps3gl.active_fp = id;
			}
		}
		else if (ps3gl.active_fp != id && pm_fp_ok[v->fp][mode])
		{
			rsxLoadFragmentProgramLocation(ctx, pm_fp[v->fp], pm_fp_offset[v->fp][mode],
					GCM_LOCATION_RSX);
			ps3gl.active_fp = id;
		}
	}

	/* matrices: (projection * modelview * u_rotMatrix), rows */
	Mul(mvr, ps3gl.mv.stack[ps3gl.mv.depth], p->u[U_ROTMATRIX]);
	Mul(pmvr, ps3gl.proj.stack[ps3gl.proj.depth], mvr);

	for (i = 0; i < 4; i++)
	{
		Row(k[i], pmvr, i);
	}

	Row(k[4], mvr, 2);
	Row(k[5], mvr, 3);
	Row(k[6], ps3gl.tex.stack[ps3gl.tex.depth], 0);
	Row(k[7], ps3gl.tex.stack[ps3gl.tex.depth], 1);

	range = ps3gl.fog_end - ps3gl.fog_start;
	Set4(k[8], ps3gl.fog_end, (fabsf(range) > 1e-6f) ? 1.0f / range : 1e6f,
			p->u[U_FOGENABLED][0] != 0.0f ? 1.0f : 0.0f, 0.0f);

	/* the fragment shader's uniforms, as polymost_fp.fcg unpacks them */
	memcpy(k[9], p->u[U_TEXTUREPOSSIZE], 4 * sizeof(float));
	Set4(k[10], p->u[U_HALFTEXELSIZE][0], p->u[U_HALFTEXELSIZE][1],
			p->u[U_CLAMP][0], p->u[U_CLAMP][1]);
	/* The shade table lookup lands exactly on texel edges when
	   u_palswapPos has no half texel in it (the menus), and these values
	   come interpolated, a hair off from pixel to pixel: some pixels took
	   the next column or row (speckles). A quarter texel of the shade
	   table texture in, and the integer shade a bit above itself, keep
	   every lookup inside its texel. */
	{
		const ps3gl_texture_t *pt = &ps3gl.textures[ps3gl.tmu[1].tex];
		float qx = pt->width ? 0.25f / (float)pt->width : 0.0f;
		float qy = pt->height ? 0.25f / (float)pt->height : 0.0f;

		Set4(k[11], p->u[U_PALSWAPPOS][0] + qx, p->u[U_PALSWAPPOS][1] + qy,
				p->u[U_PALSWAPSIZE][0], p->u[U_PALSWAPSIZE][1]);
	}
	Set4(k[12], p->u[U_SHADE][0] + (1.0f / 256.0f), p->u[U_NUMSHADES][0],
			p->u[U_NUMSHADES][1], p->u[U_VISFACTOR][0]);
	Set4(k[13], p->u[U_COLORCORRECTION][0] - 1.0f, p->u[U_USECOLORONLY][0],
			p->u[U_USEPALETTE][0], p->u[U_SHADEINTERPOLATE][0]);
	Set4(k[14], p->u[U_NPOTEMULATION][0],
			p->u[U_NPOTEMULATION][1] != 0.0f ? p->u[U_NPOTEMULATION][1] : 1.0f,
			p->u[U_NPOTEMULATION][2], p->u[U_COLORCORRECTION][1]);
	Set4(k[15], ps3gl.fog_color[0], ps3gl.fog_color[1], ps3gl.fog_color[2],
			p->u[U_COLORCORRECTION][2]);

	/* sample draws (ps3gl_sample, set by PS3GL_BeginFrame every 10 s):
	   the numbers the shaders get, as Polymost set them */
	if (ps3gl_sample > 0)
	{
		PS3_Log("[sample] frame %u prog %u: tex0 %d (%dx%d %s) | uniforms texPosSize %g %g %g %g "
				"halfTexel %g %g clamp %g %g palswapPos %g %g palswapSize %g %g shade %g "
				"numShades %g %g visFactor %g fog %g colorOnly %g usePalette %g shadeInterp %g "
				"npot %g %g %g %g cc %g %g %g",
				(unsigned)ps3gl.frame, (unsigned)ps3gl.program, ps3gl.tmu[0].tex,
				ps3gl.textures[ps3gl.tmu[0].tex].width, ps3gl.textures[ps3gl.tmu[0].tex].height,
				ps3gl.textures[ps3gl.tmu[0].tex].swizzled ? "swz" : "lin",
				p->u[U_TEXTUREPOSSIZE][0], p->u[U_TEXTUREPOSSIZE][1], p->u[U_TEXTUREPOSSIZE][2],
				p->u[U_TEXTUREPOSSIZE][3], p->u[U_HALFTEXELSIZE][0], p->u[U_HALFTEXELSIZE][1],
				p->u[U_CLAMP][0], p->u[U_CLAMP][1], p->u[U_PALSWAPPOS][0], p->u[U_PALSWAPPOS][1],
				p->u[U_PALSWAPSIZE][0], p->u[U_PALSWAPSIZE][1], p->u[U_SHADE][0],
				p->u[U_NUMSHADES][0], p->u[U_NUMSHADES][1], p->u[U_VISFACTOR][0],
				p->u[U_FOGENABLED][0], p->u[U_USECOLORONLY][0], p->u[U_USEPALETTE][0],
				p->u[U_SHADEINTERPOLATE][0], p->u[U_NPOTEMULATION][0], p->u[U_NPOTEMULATION][1],
				p->u[U_NPOTEMULATION][2], p->u[U_NPOTEMULATION][3], p->u[U_COLORCORRECTION][0],
				p->u[U_COLORCORRECTION][1], p->u[U_COLORCORRECTION][2]);
		{
			const ps3gl_texture_t *t0 = &ps3gl.textures[ps3gl.tmu[0].tex];
			int tx = (int)(p->u[U_TEXTUREPOSSIZE][0] * t0->width + 0.5f);
			int ty = (int)(p->u[U_TEXTUREPOSSIZE][1] * t0->height + 0.5f);
			int tw = (int)(p->u[U_TEXTUREPOSSIZE][2] * t0->width + 0.5f);
			int th = (int)(p->u[U_TEXTUREPOSSIZE][3] * t0->height + 0.5f);

			PS3_Log("[sample]   tile at %d,%d size %dx%d in tex %d; VRAM (R): %u %u %u %u | %u %u %u %u | "
					"center %u",
					tx, ty, tw, th, ps3gl.tmu[0].tex,
					ps3gl_texel(t0, tx, ty), ps3gl_texel(t0, tx + 1, ty),
					ps3gl_texel(t0, tx + 2, ty), ps3gl_texel(t0, tx + 3, ty),
					ps3gl_texel(t0, tx, ty + 1), ps3gl_texel(t0, tx + 1, ty + 1),
					ps3gl_texel(t0, tx + 2, ty + 1), ps3gl_texel(t0, tx + 3, ty + 1),
					ps3gl_texel(t0, tx + tw / 2, ty + th / 2));
		}
		PS3_Log("[sample]   consts k9 %g %g %g %g k10 %g %g %g %g k11 %g %g %g %g k12 %g %g %g %g "
				"k13 %g %g %g %g k8(fog) %g %g %g; tex rows %g %g %g %g / %g %g %g %g",
				k[9][0], k[9][1], k[9][2], k[9][3], k[10][0], k[10][1], k[10][2], k[10][3],
				k[11][0], k[11][1], k[11][2], k[11][3], k[12][0], k[12][1], k[12][2], k[12][3],
				k[13][0], k[13][1], k[13][2], k[13][3], k[8][0], k[8][1], k[8][2],
				k[6][0], k[6][1], k[6][2], k[6][3], k[7][0], k[7][1], k[7][2], k[7][3]);
	}

	/* only what changed since the last draw */
	first = 0;
	last = PM_CONSTS - 1;

	if (pm_sent_valid)
	{
		while (first < PM_CONSTS && !memcmp(k[first], pm_sent[first], sizeof(k[0])))
		{
			first++;
		}

		if (first == PM_CONSTS)
		{
			return;
		}

		while (last > first && !memcmp(k[last], pm_sent[last], sizeof(k[0])))
		{
			last--;
		}
	}

	if (pm_const_base >= 0)
	{
		rsxLoadVertexProgramParameterBlock(ctx, (u32)(pm_const_base + first),
				(u32)(last - first + 1), &k[first][0]);
	}
	else
	{
		for (i = first; i <= last; i++)
		{
			if (pm_const[i])
			{
				rsxSetVertexProgramParameter(ctx, pm_vp, pm_const[i], k[i]);
			}
		}
	}

	memcpy(&pm_sent[first][0], &k[first][0], (size_t)(last - first + 1) * sizeof(k[0]));
	pm_sent_valid = 1;
}

/* ================================================================ */
/* Buffer objects                                                     */
/* ================================================================ */

typedef struct {
	uint8_t *data;
	size_t   size, capacity;
	int      used;
} buffer_t;

static buffer_t *buffers;
static GLuint num_buffers;          /* entries in buffers[] */

static buffer_t *
Buffer(GLuint name)
{
	return (name > 0 && name < num_buffers && buffers[name].used) ? &buffers[name] : NULL;
}

static GLuint *
Binding(GLenum target)
{
	switch (target)
	{
		case GL_ARRAY_BUFFER:         return &ps3gl.array_buffer;
		case GL_ELEMENT_ARRAY_BUFFER: return &ps3gl.element_buffer;
		default:                      return NULL;
	}
}

static buffer_t *
Bound(GLenum target)
{
	GLuint *b = Binding(target);

	return b ? Buffer(*b) : NULL;
}

const void *
ps3gl_buffer_ptr(GLuint vbo, const void *offset)
{
	buffer_t *b = Buffer(vbo);

	if (!b || !b->data || (size_t)(uintptr_t)offset >= b->size)
	{
		return NULL;
	}

	return b->data + (uintptr_t)offset;
}

/* bytes in buffer object vbo, -1 if there is no such buffer */
long
ps3gl_buffer_size(GLuint vbo)
{
	buffer_t *b = Buffer(vbo);

	return (b && b->data) ? (long)b->size : -1;
}

const void *
ps3gl_element_ptr(const void *indices)
{
	if (!ps3gl.element_buffer)
	{
		return indices;
	}

	return ps3gl_buffer_ptr(ps3gl.element_buffer, indices);
}

void APIENTRY
glGenBuffers(GLsizei n, GLuint *names)
{
	GLuint name = 1;
	int i;

	for (i = 0; i < n; i++)
	{
		while (name < num_buffers && buffers[name].used)
		{
			name++;
		}

		if (name >= num_buffers)
		{
			GLuint grow = num_buffers ? num_buffers * 2 : 64;
			buffer_t *nb = (buffer_t *)realloc(buffers, grow * sizeof(buffer_t));

			if (!nb)
			{
				names[i] = 0;
				continue;
			}

			memset(nb + num_buffers, 0, (grow - num_buffers) * sizeof(buffer_t));
			buffers = nb;
			num_buffers = grow;
		}

		buffers[name].used = 1;
		names[i] = name++;
	}
}

void APIENTRY
glDeleteBuffers(GLsizei n, const GLuint *names)
{
	int i;

	for (i = 0; i < n; i++)
	{
		buffer_t *b = Buffer(names[i]);

		if (!b)
		{
			continue;
		}

		free(b->data);
		memset(b, 0, sizeof(*b));

		if (ps3gl.array_buffer == names[i])
		{
			ps3gl.array_buffer = 0;
		}

		if (ps3gl.element_buffer == names[i])
		{
			ps3gl.element_buffer = 0;
		}
	}
}

GLboolean APIENTRY
glIsBuffer(GLuint name)
{
	return Buffer(name) ? GL_TRUE : GL_FALSE;
}

void APIENTRY
glBindBuffer(GLenum target, GLuint name)
{
	GLuint *b = Binding(target);

	if (b)
	{
		*b = Buffer(name) ? name : 0;
	}
}

/* The storage only grows: a new glBufferData with the same size (EDuke32
   "orphans" its stream buffer that way) keeps the memory. Nothing the RSX
   reads is in here, so overwriting is always safe. */
void APIENTRY
glBufferData(GLenum target, GLsizeiptr size, const GLvoid *data, GLenum usage)
{
	buffer_t *b = Bound(target);

	(void)usage;

	if (!b || size < 0)
	{
		return;
	}

	if ((size_t)size > b->capacity)
	{
		uint8_t *nd = (uint8_t *)realloc(b->data, (size_t)size);

		if (!nd)
		{
			PS3_Log("[gl] WARNING: no memory for a %ld byte buffer object", (long)size);
			return;
		}

		b->data = nd;
		b->capacity = (size_t)size;
	}

	b->size = (size_t)size;

	/* GL leaves new storage undefined; zeroed here, which also shows at
	   once (crumb) if this memory can't be written */
	if (!data && size)
	{
		PS3_Crumb("bufdata zero", (int)size);
		memset(b->data, 0, (size_t)size);
	}

	{
		static int logged;

		if (logged < 4)
		{
			PS3_Log("[gl] buffer %u (target 0x%x): %ld bytes at %p%s", (unsigned)*Binding(target),
					(unsigned)target, (long)size, (void *)b->data, data ? "" : " (no data)");
			logged++;
		}
	}

	if (data && size)
	{
		memcpy(b->data, data, (size_t)size);
	}
}

void APIENTRY
glBufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const GLvoid *data)
{
	buffer_t *b = Bound(target);

	if (!b || !data || offset < 0 || size <= 0 || (size_t)(offset + size) > b->size)
	{
		return;
	}

	memcpy(b->data + offset, data, (size_t)size);
}

void * APIENTRY
glMapBuffer(GLenum target, GLenum access)
{
	buffer_t *b = Bound(target);

	(void)access;

	return b ? b->data : NULL;
}

void * APIENTRY
glMapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access)
{
	buffer_t *b = Bound(target);

	(void)length;
	(void)access;

	return (b && b->data && (size_t)offset < b->size) ? b->data + offset : NULL;
}

GLboolean APIENTRY
glUnmapBuffer(GLenum target)
{
	(void)target;
	return GL_TRUE;
}

/* ================================================================ */
/* Queries ps3gl answers with "nothing"                               */
/* ================================================================ */

/* texcache.cpp asks (the texture cache is off: no compression) */
void APIENTRY
glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint *params)
{
	(void)target;
	(void)level;
	(void)pname;

	if (params)
	{
		*params = 0;
	}
}

void APIENTRY
glGenSamplers(GLsizei n, GLuint *samplers)
{
	int i;

	for (i = 0; samplers && i < n; i++)
	{
		samplers[i] = 0;
	}
}

/* ================================================================ */
/* Self test: what the fragment programs really get                   */
/* ================================================================ */

/*
 * Run once, early in the first GL frames (PS3GL_BeginFrame). Draws a
 * screen-sized quad with each probe (shaders/polymost_probe.fcg, and
 * Polymost's debug view 2) in each perspective bit mode, three times: all
 * vertices at w 1, all at w 4, and w 1 on the left going to w 4 on the
 * right (a real perspective). After each draw the RSX is waited for and
 * three pixels of the middle row (1/4, 1/2, 3/4 across) are read back.
 * The log gets the bytes read, the bytes expected, and a verdict per
 * mode; the first mode that is right everywhere becomes the views' mode.
 */

#define ST_PROBES   7       /* polymost_probe.fcg 1..6, Polymost's view 2 */
#define ST_DRAWS    3
#define ST_POINTS   3
#define ST_TOL      3       /* bytes */

static const char *const st_probe_names[ST_PROBES] = {
	"P1 TEXCOORD1 (MOV)",
	"P2 TEXCOORD1 * 0.5 (MUL)",
	"P3 TEXCOORD0.xy (MOV)",
	"P4 TEXCOORD0.xy * 0.5 (MUL)",
	"P5 w of TEXCOORD2,3,4,7",
	"P6 w of TEXCOORD5,6, z of TEXCOORD2,3",
	"P7 Polymost view 2 (tile coordinate)",
};

static const float st_w[ST_DRAWS][2] = { { 1.0f, 1.0f }, { 4.0f, 4.0f }, { 1.0f, 4.0f } };
static const char *const st_draw_names[ST_DRAWS] = { "w1", "w4", "w1..4" };

static int st_res[ST_PROBES][PM_MODES][ST_DRAWS][ST_POINTS][4];

static void
StConsts(float k[PM_CONSTS][4], int tile)
{
	int i;

	memset(k, 0, sizeof(float) * PM_CONSTS * 4);

	for (i = 0; i < 4; i++)
	{
		k[i][i] = 1.0f;     /* m0..m3: identity, the positions are clip space */
	}

	Set4(k[4], 0.0f, 0.0f, 1.0f, 0.0f);     /* e2, e3 */
	Set4(k[5], 0.0f, 0.0f, 0.0f, 1.0f);
	Set4(k[6], 1.0f, 0.0f, 0.0f, 0.0f);     /* texture matrix rows */
	Set4(k[7], 0.0f, 1.0f, 0.0f, 0.0f);
	Set4(k[8], 0.0f, 0.0f, 0.0f, 0.0f);     /* fog off */

	if (!tile)
	{
		/* u5.z (usePalette) 0: the vertex program doesn't swap s and t */
		Set4(k[9],  0.80f, 0.60f, 0.40f, 0.20f);
		Set4(k[10], 0.30f, 0.70f, 0.10f, 0.90f);
		Set4(k[11], 0.15f, 0.35f, 0.55f, 0.75f);
		Set4(k[12], 0.05f, 0.45f, 0.65f, 0.85f);
		Set4(k[13], 0.25f, 0.50f, 0.00f, 0.95f);
		Set4(k[14], 0.12f, 0.34f, 0.56f, 0.78f);
		Set4(k[15], 0.90f, 0.80f, 0.70f, 0.60f);
	}
	else
	{
		/* a 1/8 x 1/4 tile at 1/4, 1/2, no clamping, no NPOT, no palette */
		Set4(k[9],  0.25f, 0.50f, 0.125f, 0.25f);
		Set4(k[10], 0.001f, 0.001f, 0.0f, 0.0f);
		Set4(k[13], 0.0f, 0.0f, 0.0f, 0.0f);
		Set4(k[14], 0.0f, 1.0f, 0.0f, 1.0f);
	}
}

static float
StClamp(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

/* What probe p should give at screen fraction s of draw d (e), and what
   it would give if the inputs came uncorrected, divided by w (raw). */
static void
StExpect(int p, int d, float s, const float ka[PM_CONSTS][4], float e[4], float raw[4])
{
	float a = (1.0f - s) / st_w[d][0], b = s / st_w[d][1];
	float invw = a + b, u = b / invw;
	int i;

	for (i = 0; i < 4; i++)
	{
		e[i] = raw[i] = -1.0f;
	}

	switch (p)
	{
		case 0:
		case 1:
			for (i = 0; i < 4; i++)
			{
				e[i] = ka[9][i] * (p == 1 ? 0.5f : 1.0f);
				raw[i] = e[i] * invw;
			}
			break;
		case 2:
		case 3:
		{
			float f = (p == 3) ? 0.5f : 1.0f;

			Set4(e, u * f, 0.5f * f, 0.0f, 1.0f);
			Set4(raw, b * f, 0.5f * f * invw, 0.0f, 1.0f);
			break;
		}
		case 4:
			Set4(e, ka[10][3], ka[11][3], ka[12][3], ka[15][3]);
			break;
		case 5:
			Set4(e, ka[13][3], ka[14][3], ka[10][2], ka[11][2]);
			break;
		default:
			Set4(e, 0.25f + StClamp(0.125f * (u - floorf(u)), 0.001f, 0.124f),
					0.5f + StClamp(0.25f * 0.5f, 0.001f, 0.249f), 0.0f, 1.0f);
			break;
	}
}

static int
StByte(float v)
{
	return (int)(StClamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

static void
StUpload(gcmContextData *ctx, const float k[PM_CONSTS][4])
{
	int i;

	if (pm_const_base >= 0)
	{
		rsxLoadVertexProgramParameterBlock(ctx, (u32)pm_const_base, PM_CONSTS, &k[0][0]);
	}
	else
	{
		for (i = 0; i < PM_CONSTS; i++)
		{
			if (pm_const[i])
			{
				rsxSetVertexProgramParameter(ctx, pm_vp, pm_const[i], k[i]);
			}
		}
	}

	pm_sent_valid = 0;
}

static void
StQuad(int d)
{
	static const float qx[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
	static const float qy[4] = { -1.0f, -1.0f, 1.0f, 1.0f };
	ps3gl_vertex_t v[4];
	int i;

	for (i = 0; i < 4; i++)
	{
		float w = st_w[d][qx[i] < 0.0f ? 0 : 1];

		v[i].x = qx[i] * w;
		v[i].y = qy[i] * w;
		v[i].z = 0.5f * w;
		v[i].w = w;
		v[i].u0 = v[i].u1 = qx[i] < 0.0f ? 0.0f : 1.0f;
		v[i].v0 = v[i].v1 = qy[i] < 0.0f ? 0.0f : 1.0f;
		v[i].color = 0xffffffffu;
	}

	ps3gl_draw_fan(v, 4);
}

static void
StLine(char *buf, size_t size, const char *head, int vals[ST_DRAWS][ST_POINTS][4])
{
	size_t n = (size_t)snprintf(buf, size, "%s", head);
	int d, i;

	for (d = 0; d < ST_DRAWS && n < size; d++)
	{
		n += (size_t)snprintf(buf + n, size - n, " | %s", st_draw_names[d]);

		for (i = 0; i < ST_POINTS && n < size; i++)
		{
			if (vals[d][i][0] < 0)
			{
				n += (size_t)snprintf(buf + n, size - n, " -");
			}
			else
			{
				n += (size_t)snprintf(buf + n, size - n, " %d,%d,%d,%d", vals[d][i][0],
						vals[d][i][1], vals[d][i][2], vals[d][i][3]);
			}
		}
	}
}

/* Waits until the pixels of everything drawn so far are in memory: a
   back end label is written after the raster operations are done (the
   reference value only says the commands were read: patch 15 read the
   draw before the last one). */
#define ST_LABEL 69

static void
StWaitPixels(gcmContextData *ctx)
{
	static u32 seq = 0x5e1f0000u;
	volatile u32 *label = (volatile u32 *)gcmGetLabelAddress(ST_LABEL);
	int waited = 0;

	seq++;
	rsxSetWriteBackendLabel(ctx, ST_LABEL, seq);
	rsxFlushBuffer(ctx);

	while (*label != seq)
	{
		usleep(20);

		if (++waited > 100000)
		{
			PS3_Log("[selftest] WARNING: the label never came (%08x)", (unsigned)*label);
			break;
		}
	}
}

void
PS3GL_SelfTest(void)
{
	gcmContextData *ctx = ps3gl.ctx;
	float ka[PM_CONSTS][4], kb[PM_CONSTS][4];
	int exp_[ST_PROBES][ST_DRAWS][ST_POINTS][4], raw_[ST_PROBES][ST_DRAWS][ST_POINTS][4];
	int px[ST_POINTS], ok_mode[PM_MODES], chosen = -1;
	uint32_t *rt, rt_off, rt_pitch;
	char line[1024];
	__typeof__(ps3gl.rs) saved;
	int p, m, d, i, c;

	if (!ctx || !ps3gl.ring || !pm_vp || !pm_fp_ok[2][0] ||
		PS3_Video_RenderTargetInfo(&rt_off, &rt_pitch) != 0 || !(rt = PS3_Video_Source()))
	{
		PS3_Log("[selftest] skipped: nothing to draw with yet");
		return;
	}

	for (i = 0; i < ST_POINTS; i++)
	{
		px[i] = ps3gl.screen_w * (i + 1) / 4;
	}

	StConsts(ka, 0);
	StConsts(kb, 1);

	/* plain state: no blending, tests, culling; the whole target */
	saved = ps3gl.rs;
	ps3gl.rs.blend = ps3gl.rs.alpha_test = ps3gl.rs.depth_test = 0;
	ps3gl.rs.depth_mask = ps3gl.rs.cull = ps3gl.rs.scissor = 0;
	ps3gl.rs.stencil = ps3gl.rs.polyoffset = 0;
	ps3gl.rs.mask_r = ps3gl.rs.mask_g = ps3gl.rs.mask_b = ps3gl.rs.mask_a = 1;
	ps3gl.rs.vp_x = ps3gl.rs.vp_y = 0;
	ps3gl.rs.vp_w = ps3gl.screen_w;
	ps3gl.rs.vp_h = ps3gl.screen_h;
	ps3gl.rs.depth_near = 0.0f;
	ps3gl.rs.depth_far = 1.0f;
	ps3gl.dirty = PS3GL_DIRTY_ALL;
	ps3gl_apply_states();

	rsxLoadVertexProgram(ctx, pm_vp, pm_vp_ucode);

	for (p = 0; p < ST_PROBES; p++)
	{
		const rsxFragmentProgram *fp;
		u32 off[PM_MODES];
		int have = 1;

		if (p < PS3GL_POLYMOST_PROBES)
		{
			fp = (const rsxFragmentProgram *)polymost_probes[p];

			for (m = 0; m < PM_MODES; m++)
			{
				have &= LoadFPCopy(fp, m, &off[m]);
			}
		}
		else
		{
			fp = pm_fp[2];

			for (m = 0; m < PM_MODES; m++)
			{
				have &= pm_fp_ok[2][m];
				off[m] = pm_fp_offset[2][m];
			}
		}

		StUpload(ctx, p == ST_PROBES - 1 ? kb : ka);

		for (m = 0; m < PM_MODES; m++)
		{
			if (have)
			{
				rsxLoadFragmentProgramLocation(ctx, fp, off[m], GCM_LOCATION_RSX);
			}

			for (d = 0; d < ST_DRAWS; d++)
			{
				if (have)
				{
					StQuad(d);
					StWaitPixels(ctx);
				}

				for (i = 0; i < ST_POINTS; i++)
				{
					const uint32_t *pp = (const uint32_t *)((const uint8_t *)rt +
							(size_t)(ps3gl.screen_h / 2) * rt_pitch + (size_t)px[i] * 4);
					uint32_t v;

					/* RSX memory is write-through for the CPU but its
					   reads are cached: patches 15 and 16 read stale
					   lines (an earlier probe). Out of the cache first. */
					__asm__ volatile("dcbf 0,%0\n\tsync" :: "r"(pp) : "memory");
					v = *(volatile const uint32_t *)pp;

					st_res[p][m][d][i][0] = have ? (int)((v >> 16) & 255) : -1;
					st_res[p][m][d][i][1] = (int)((v >> 8) & 255);
					st_res[p][m][d][i][2] = (int)(v & 255);
					st_res[p][m][d][i][3] = (int)(v >> 24);
				}
			}
		}

		/* (the probe copies stay: a few hundred bytes, once) */
	}

	/* what was read, against what should be there */
	PS3_Log("[selftest] Polymost fragment program inputs, read back from the render target "
			"(%dx%d; bytes R,G,B,A at x %d %d %d of the middle row)",
			ps3gl.screen_w, ps3gl.screen_h, px[0], px[1], px[2]);

	for (m = 0; m < PM_MODES; m++)
	{
		ok_mode[m] = 1;
	}

	for (p = 0; p < ST_PROBES; p++)
	{
		int any_raw = 0;

		for (d = 0; d < ST_DRAWS; d++)
		{
			for (i = 0; i < ST_POINTS; i++)
			{
				float s = ((float)px[i] + 0.5f) / (float)ps3gl.screen_w;
				float e[4], r[4];

				StExpect(p, d, s, p == ST_PROBES - 1 ? kb : ka, e, r);

				for (c = 0; c < 4; c++)
				{
					exp_[p][d][i][c] = StByte(e[c]);
					raw_[p][d][i][c] = r[c] < 0.0f ? -1 : StByte(r[c]);
				}

				any_raw |= (r[0] >= 0.0f);
			}
		}

		snprintf(line, sizeof(line), "%s, expected:", st_probe_names[p]);
		StLine(line + strlen(line), sizeof(line) - strlen(line), "", exp_[p]);
		PS3_Log("[selftest] %s", line);

		if (any_raw)
		{
			StLine(line, sizeof(line), "    if divided by w:", raw_[p]);
			PS3_Log("[selftest] %s", line);
		}

		for (m = 0; m < PM_MODES; m++)
		{
			int bad = 0;

			for (d = 0; d < ST_DRAWS; d++)
			{
				for (i = 0; i < ST_POINTS; i++)
				{
					for (c = 0; c < 4; c++)
					{
						int diff = st_res[p][m][d][i][c] - exp_[p][d][i][c];

						if (st_res[p][m][d][i][0] < 0 || diff > ST_TOL || diff < -ST_TOL)
						{
							bad++;
						}
					}
				}
			}

			if (bad)
			{
				ok_mode[m] = 0;
			}

			snprintf(line, sizeof(line), "    mode %d %s:", m, bad ? "WRONG" : "ok   ");
			StLine(line + strlen(line), sizeof(line) - strlen(line), "", st_res[p][m]);
			PS3_Log("[selftest] %s", line);
		}
	}

	for (m = 0; m < PM_MODES; m++)
	{
		PS3_Log("[selftest] mode %d (perspective bit %s): %s", m, mode_names[m],
				ok_mode[m] ? "RIGHT everywhere" : "wrong somewhere");
	}

	/* bit cleared first: the plain way; then set; cgcomp's last */
	{
		static const int order[PM_MODES] = { 1, 2, 0 };

		for (i = 0; i < PM_MODES && chosen < 0; i++)
		{
			if (ok_mode[order[i]])
			{
				chosen = order[i];
			}
		}
	}

	if (chosen >= 0)
	{
		int f;

		pm_mode = chosen;

		for (f = 0; f < PS3GL_POLYMOST_FAST; f++)
		{
			pm_fast_ok[f] = LoadFPCopy((const rsxFragmentProgram *)polymost_fast[f], pm_mode,
					&pm_fast_offset[f]);
		}

		PS3_Log("[selftest] Polymost runs with mode %d (perspective bit %s)", pm_mode,
				mode_names[pm_mode]);
	}
	else
	{
		PS3_Log("[selftest] NO mode is right: Polymost stays with mode %d (perspective bit %s)",
				pm_mode, mode_names[pm_mode]);
	}

	/* clean up: black target, everything to the RSX again */
	rsxSetClearColor(ctx, 0);
	rsxClearSurface(ctx, GCM_CLEAR_R | GCM_CLEAR_G | GCM_CLEAR_B | GCM_CLEAR_A);

	ps3gl.rs = saved;
	ps3gl.dirty = PS3GL_DIRTY_ALL;
	ps3gl.vp_loaded = 0;
	ps3gl.active_fp = -1;
	ps3gl.mvp_uploaded = 0;
	pm_sent_valid = 0;
}
