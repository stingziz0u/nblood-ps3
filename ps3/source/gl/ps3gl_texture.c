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
 * ps3gl: textures. Stored as A8R8G8B8 in RSX memory, but for the one
 * channel ones (GL_RED / GL_LUMINANCE: Polymost's tilesheets of palette
 * indices and its shade table), which are B8, a byte a texel, the RSX
 * putting it in R, G and B (alpha 1), as A8R8G8B8 had it. Each of
 * Polymost's 2048x2048 tilesheets took 16 MB as 32 bits: Redneck
 * Rampage needs 13 of them, and the RSX ran out (NBlood-PS3 patch 22).
 *
 *  - Power of two sizes (all of TyrQuake's: ps3gl doesn't offer NPOT
 *    textures, so TyrQuake pads or stretches them): SWIZZLED (Morton
 *    order), levels packed one after the other with no padding. Linear
 *    textures only filter correctly with CLAMP, and the walls repeat
 *    (Doom64-PS3, on hardware). Layout per RPCS3's
 *    get_subresources_layout_impl.
 *  - Anything else: linear, one level, rows padded to 64 bytes, clamped.
 *
 * The pixels are converted (and swizzled) in ordinary memory and copied
 * to RSX memory in one go. The CPU never reads RSX memory.
 *
 * A texture the RSX may still be reading (drawn with in a frame it hasn't
 * finished) gets new storage when its level 0 is uploaded again, the old
 * one is freed later (ps3gl_defer_free). glTexSubImage2D writes in place.
 * TyrQuake only updates a lightmap before drawing with it, and always
 * with the whole block's current data, so the worst a frame still in
 * flight can see is its lights one or two frames early.
 *
 * glCopyTexSubImage2D (water warp textures) is done by the RSX: the
 * rectangle of the render target is copied into the texture, swizzled on
 * the way (rsxSetConvertSwizzleFormat), in order with the drawing.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <stdlib.h>

#include "ps3gl.h"
#include "../ps3_platform.h"
#include "../ps3_video.h"
#include "ps3_prof.h"

/* identity remap (IoQuake3-PS3 / Doom64-PS3) */
#define TEX_REMAP_IDENTITY 0x00AAE4

/* B8: R, G and B from the texel's one channel (B), alpha one */
#define TEX_REMAP_B8 \
	((GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_B_SHIFT) | \
	 (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_G_SHIFT) | \
	 (GCM_TEXTURE_REMAP_TYPE_REMAP << GCM_TEXTURE_REMAP_TYPE_R_SHIFT) | \
	 (GCM_TEXTURE_REMAP_TYPE_ONE << GCM_TEXTURE_REMAP_TYPE_A_SHIFT) | \
	 (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_B_SHIFT) | \
	 (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_G_SHIFT) | \
	 (GCM_TEXTURE_REMAP_COLOR_B << GCM_TEXTURE_REMAP_COLOR_R_SHIFT) | \
	 (GCM_TEXTURE_REMAP_COLOR_A << GCM_TEXTURE_REMAP_COLOR_A_SHIFT))

/* (GL_R8 and GL_LUMINANCE8, as internal formats: glad's values) */
#define PS3GL_R8          0x8229
#define PS3GL_LUMINANCE8  0x8040

static int
TexelBytes(const ps3gl_texture_t *t)
{
	return t->bpp ? t->bpp : 4;
}

/* NBlood-PS3 (patch 44): RGBA textures the caller marks are kept as 16
   bits a texel, A1R5G5B5 (Polymost's art tiles with a texture filter on:
   256 colours and an alpha of 0 or 255, half the video memory). */
static int pack16;

void
PS3GL_SetPack16(int on)
{
	pack16 = on != 0;
}

static inline uint16_t
ToA1R5G5B5(uint32_t argb)
{
	return (uint16_t)(((argb >> 31) << 15) | ((argb >> 9) & 0x7c00) |
		((argb >> 6) & 0x03e0) | ((argb >> 3) & 0x001f));
}

/* When a texture doesn't fit: the owner frees textures not drawn for
   min_age frames (never 'bound', the one being made) and says how many;
   ps3gl tries again. Polymost's (polymost_ps3_evict). */
static int (*evict_hook)(int min_age, int bound);
static unsigned evicted_total;

void
PS3GL_SetEvictHook(int (*hook)(int min_age, int bound))
{
	evict_hook = hook;
}

/* Frames since a texture was made or drawn (huge for no texture) */
int
PS3GL_TextureAge(unsigned name)
{
	const ps3gl_texture_t *t;

	if (!ps3gl.textures || name == 0 || name >= PS3GL_MAX_TEXTURES)
		return 1 << 30;

	t = &ps3gl.textures[name];
	return t->data ? (int)(ps3gl.frame - t->used_frame) : 1 << 30;
}

/* conversion buffer, grown on demand */
static uint32_t *scratch;
static size_t scratch_size;

/* Swizzle tables of the last level size written into: EDuke32 puts
   thousands of small tiles into the same big tilesheet. */
static uint32_t *swz_tx, *swz_ty;
static int swz_w, swz_h;

/* What a unit without a texture samples in Polymost's program: white. */
static ps3gl_texture_t white;

static void BuildDescriptor(ps3gl_texture_t *t);

void
ps3gl_textures_init(void)
{
	/* ps3gl.textures is zeroed by calloc: no storage, name free */

	white.data = (uint8_t *)rsxMemalign(128, 128);

	if (white.data)
	{
		memset(white.data, 0xff, 128);
		rsxAddressToOffset(white.data, &white.offset);
		white.size = 128;
		white.width = white.height = 1;
		white.swizzled = 1;
		white.max_levels = white.num_levels = 1;
		white.min_filter = white.mag_filter = GL_NEAREST;
		white.wrap_s = white.wrap_t = GL_REPEAT;
		BuildDescriptor(&white);
	}
}

static uint32_t *
Scratch(size_t bytes)
{
	if (bytes > scratch_size)
	{
		free(scratch);
		scratch = (uint32_t *)malloc(bytes);
		scratch_size = scratch ? bytes : 0;
	}

	return scratch;
}

static int
IsPow2(int v)
{
	return v > 0 && (v & (v - 1)) == 0;
}

static int
Log2(int v)
{
	int r = 0;

	while (v > 1)
	{
		v >>= 1;
		r++;
	}

	return r;
}

static int
LevelDim(int base, int level)
{
	int d = base >> level;

	return d ? d : 1;
}

/* Byte offset of 'level' in a swizzled chain of 'bpp' byte texels */
static uint32_t
LevelOffset(int w, int h, int level, int bpp)
{
	uint32_t off = 0;
	int i;

	for (i = 0; i < level; i++)
	{
		off += (uint32_t)LevelDim(w, i) * LevelDim(h, i) * bpp;
	}

	return off;
}

/* Morton order of a 2^lw x 2^lh image: x and y bits interleaved, x
   first; once one runs out the other's bits follow. The bits of x and y
   never overlap, so offset(x, y) = offset(x, 0) | offset(0, y): two small
   tables per image. */
static void
SwizzleTables(uint32_t *tx, uint32_t *ty, int w, int h)
{
	int lw = Log2(w), lh = Log2(h);
	int x, y;

	for (x = 0; x < w; x++)
	{
		uint32_t off = 0, bit = 0;
		int v = x, bw = lw, bh = lh;

		while (bw || bh)
		{
			if (bw) { off |= (uint32_t)(v & 1) << bit; v >>= 1; bit++; bw--; }
			if (bh) { bit++; bh--; }
		}

		tx[x] = off;
	}

	for (y = 0; y < h; y++)
	{
		uint32_t off = 0, bit = 0;
		int v = y, bw = lw, bh = lh;

		while (bw || bh)
		{
			if (bw) { bit++; bw--; }
			if (bh) { off |= (uint32_t)(v & 1) << bit; v >>= 1; bit++; bh--; }
		}

		ty[y] = off;
	}
}

static int
FormatBytes(GLenum format)
{
	switch (format)
	{
		case GL_ALPHA:
		case GL_LUMINANCE:
		case GL_RED:
		case GL_COLOR_INDEX:
			return 1;
		case GL_LUMINANCE_ALPHA:
			return 2;
		case GL_RGB:
			return 3;
		default:
			return 4;
	}
}

/* One source pixel -> A8R8G8B8 (as a big endian word: bytes A R G B). */
static inline uint32_t
ToARGB(const uint8_t *p, int bytes, GLenum format)
{
	switch (bytes)
	{
		case 4:
			if (format == GL_BGRA)
			{
				return ((uint32_t)p[3] << 24) | ((uint32_t)p[2] << 16) |
					((uint32_t)p[1] << 8) | p[0];
			}

			return ((uint32_t)p[3] << 24) | ((uint32_t)p[0] << 16) |
				((uint32_t)p[1] << 8) | p[2];
		case 3:
			return 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2];
		case 2:
			return ((uint32_t)p[1] << 24) | ((uint32_t)p[0] << 16) |
				((uint32_t)p[0] << 8) | p[0];
		default:
			if (format == GL_ALPHA)
			{
				return ((uint32_t)p[0] << 24) | 0x00ffffffu;
			}

			return 0xff000000u | ((uint32_t)p[0] << 16) | ((uint32_t)p[0] << 8) | p[0];
	}
}

static ps3gl_texture_t *
Bound(void)
{
	int name = ps3gl.tmu[ps3gl.active_tmu].tex;

	if (!ps3gl.textures || name < 0 || name >= PS3GL_MAX_TEXTURES)
	{
		return NULL;
	}

	return &ps3gl.textures[name];
}

/* a texture that didn't get RSX memory since the last call (ps3layer.cpp
   turns the texture filter off: NBlood-PS3 patch 32) */
static int ps3gl_oom;

/* the textures that didn't fit, since the last PS3GL_TexStats(..., 1)
   (the video memory test, NBlood-PS3 patch 41) */
static uint32_t tex_fails, tex_fail_bytes;

int
PS3GL_TakeOutOfMemory(void)
{
	int const r = ps3gl_oom;

	ps3gl_oom = 0;
	return r;
}

/* the RSX memory the textures have, and the ones that didn't get any */
void
PS3GL_TexStats(uint32_t *bytes, uint32_t *fails, uint32_t *fail_bytes, int reset)
{
	if (bytes)
		*bytes = ps3gl.st_tex_bytes;
	if (fails)
		*fails = tex_fails;
	if (fail_bytes)
		*fail_bytes = tex_fail_bytes;

	if (reset)
		tex_fails = tex_fail_bytes = 0;
}

/* What the RSX's heap has free, in bytes: the largest block, and all of
   it (taken in blocks as big as they come, then given back). Slow: for
   the test. */
uint32_t
PS3GL_ProbeFreeRSX(uint32_t *largest)
{
	enum { MAXBLOCKS = 4096, MINBLOCK = 64 << 10 };
	static void *blocks[MAXBLOCKS];
	uint32_t lo = 0, hi = 256u << 20, total = 0, size;
	int n = 0, i;

	/* the largest block: halving */
	while (hi - lo > MINBLOCK)
	{
		uint32_t const mid = lo + ((hi - lo) / 2 & ~(MINBLOCK - 1u));
		void *const p = rsxMemalign(128, mid);

		if (p)
		{
			rsxFree(p);
			lo = mid;
		}
		else
			hi = mid;
	}

	if (largest)
		*largest = lo;

	/* all of it */
	for (size = lo ? lo : MINBLOCK; size >= MINBLOCK && n < MAXBLOCKS;)
	{
		void *const p = rsxMemalign(128, size);

		if (p)
		{
			blocks[n++] = p;
			total += size;
		}
		else
			size /= 2;
	}

	for (i = 0; i < n; i++)
		rsxFree(blocks[i]);

	return total;
}

static int
Busy(const ps3gl_texture_t *t)
{
	return t->last_frame != 0 && (int)(ps3gl_completed_frame() - t->last_frame) < 0;
}

static void
ReleaseStorage(ps3gl_texture_t *t)
{
	if (t->data)
	{
		ps3gl_defer_free(t->data, t->last_frame);
		ps3gl.st_tex_bytes -= t->size;
		t->data = NULL;
		t->size = 0;
	}

	t->last_frame = 0;
	t->num_levels = 0;
}

static void
BuildDescriptor(ps3gl_texture_t *t)
{
	memset(&t->gt, 0, sizeof(t->gt));
	t->gt.format = (TexelBytes(t) == 1 ? GCM_TEXTURE_FORMAT_B8 :
			TexelBytes(t) == 2 ? GCM_TEXTURE_FORMAT_A1R5G5B5 : GCM_TEXTURE_FORMAT_A8R8G8B8) |
		(t->swizzled ? GCM_TEXTURE_FORMAT_SWZ : GCM_TEXTURE_FORMAT_LIN);
	t->gt.mipmap = t->num_levels ? t->num_levels : 1;
	t->gt.dimension = GCM_TEXTURE_DIMS_2D;
	t->gt.cubemap = GCM_FALSE;
	t->gt.remap = TexelBytes(t) == 1 ? TEX_REMAP_B8 : TEX_REMAP_IDENTITY;
	t->gt.width = t->width;
	t->gt.height = t->height;
	t->gt.depth = 1;
	t->gt.location = GCM_LOCATION_RSX;
	t->gt.pitch = t->swizzled ? 0 : t->pitch;
	t->gt.offset = t->offset;
	t->dirty = 1;
}

/* ================================================================ */

void APIENTRY
glGenTextures(GLsizei n, GLuint *textures)
{
	/* Free names, 1..PS3GL_MAX_TEXTURES-1, going round: TyrQuake frees
	   and asks for names on every map. */
	static int next = 1;
	int i, tries;

	if (!ps3gl.textures)
	{
		return;
	}

	for (i = 0; i < n; i++)
	{
		textures[i] = 0;

		for (tries = 0; tries < PS3GL_MAX_TEXTURES; tries++)
		{
			int name = next;

			next = (next + 1 < PS3GL_MAX_TEXTURES) ? next + 1 : 1;

			if (!ps3gl.textures[name].allocated)
			{
				ps3gl.textures[name].allocated = 1;
				textures[i] = (GLuint)name;
				break;
			}
		}

		if (!textures[i])
		{
			static int warned;

			if (!warned)
			{
				PS3_Log("[gl] WARNING: out of texture names (%d)", PS3GL_MAX_TEXTURES);
				warned = 1;
			}
		}
	}
}

void APIENTRY
glDeleteTextures(GLsizei n, const GLuint *textures)
{
	int i, j;

	if (!ps3gl.textures)
	{
		return;
	}

	for (i = 0; i < n; i++)
	{
		ps3gl_texture_t *t;

		if (textures[i] == 0 || textures[i] >= PS3GL_MAX_TEXTURES)
		{
			continue;
		}

		t = &ps3gl.textures[textures[i]];
		ReleaseStorage(t);
		memset(t, 0, sizeof(*t));

		for (j = 0; j < PS3GL_MAX_TMUS; j++)
		{
			if (ps3gl.tmu[j].tex == (int)textures[i])
			{
				ps3gl.tmu[j].hw_tex = -2;
			}
		}
	}
}

void APIENTRY
glBindTexture(GLenum target, GLuint texture)
{
	(void)target;

	if (texture >= PS3GL_MAX_TEXTURES)
	{
		static int warned;

		if (!warned)
		{
			PS3_Log("[gl] WARNING: texture name %u out of range", (unsigned)texture);
			warned = 1;
		}

		texture = 0;
	}

	ps3gl.tmu[ps3gl.active_tmu].tex = (int)texture;
}

/* The red byte of texel x,y of level 0, read back from RSX memory (slow:
   debug only). Where ps3gl's own layout puts it. */
unsigned
ps3gl_texel(const ps3gl_texture_t *t, int x, int y)
{
	uint32_t off = 0, bit = 0;
	int lw, lh;

	if (!t || !t->data || x < 0 || y < 0 || x >= t->width || y >= t->height)
	{
		return 999;
	}

	if (!t->swizzled)
	{
		return TexelBytes(t) == 1 ? t->data[(size_t)y * t->pitch + x] :
			TexelBytes(t) == 2 ? (unsigned)(t->data[(size_t)y * t->pitch + (size_t)x * 2] & 0x7c) << 1 :
			t->data[(size_t)y * t->pitch + (size_t)x * 4 + 1];
	}

	lw = Log2(t->width);
	lh = Log2(t->height);

	while (lw || lh)
	{
		if (lw) { off |= (uint32_t)(x & 1) << bit; x >>= 1; bit++; lw--; }
		if (lh) { off |= (uint32_t)(y & 1) << bit; y >>= 1; bit++; lh--; }
	}

	return TexelBytes(t) == 1 ? t->data[off] :
		TexelBytes(t) == 2 ? (unsigned)(t->data[(size_t)off * 2] & 0x7c) << 1 :
		t->data[(size_t)off * 4 + 1];
}

GLboolean APIENTRY
glIsTexture(GLuint texture)
{
	return (ps3gl.textures && texture > 0 && texture < PS3GL_MAX_TEXTURES &&
		ps3gl.textures[texture].allocated) ? GL_TRUE : GL_FALSE;
}

void APIENTRY
glActiveTexture(GLenum texture)
{
	int tmu = (int)(texture - GL_TEXTURE0);

	if (tmu >= 0 && tmu < PS3GL_MAX_TMUS)
	{
		ps3gl.active_tmu = tmu;
	}
}

void APIENTRY
glClientActiveTexture(GLenum texture)
{
	int tmu = (int)(texture - GL_TEXTURE0);

	if (tmu >= 0 && tmu < PS3GL_MAX_TMUS)
	{
		ps3gl.client_tmu = tmu;
	}
}

void APIENTRY
glPixelStorei(GLenum pname, GLint param)
{
	if (pname == GL_UNPACK_ROW_LENGTH)
	{
		ps3gl.unpack_row_length = param > 0 ? param : 0;
	}
}

/* Writes a w x h block of source pixels at (x, y) of 'level', converted,
   into RSX memory. The block is the whole level for glTexImage2D. */
/* The swizzle tables for a level of lw x lh (kept for the next one) */
static int
SwizzleFor(int lw, int lh)
{
	if (lw != swz_w || lh != swz_h)
	{
		free(swz_tx);
		swz_tx = (uint32_t *)malloc((size_t)(lw + lh) * sizeof(uint32_t));
		swz_w = swz_h = 0;

		if (!swz_tx)
		{
			return 0;
		}

		swz_ty = swz_tx + lw;
		SwizzleTables(swz_tx, swz_ty, lw, lh);
		swz_w = lw;
		swz_h = lh;
	}

	return 1;
}

/* One source pixel -> the one channel of a B8 texture: the byte itself,
   or the red of anything wider (a texture given another format later) */
static inline uint8_t
ToB8(const uint8_t *p, int bytes, GLenum format)
{
	return bytes == 1 ? p[0] : (uint8_t)(ToARGB(p, bytes, format) >> 16);
}

/* Upload for B8 textures (a byte a texel), as Upload below */
static void
Upload8(ps3gl_texture_t *t, int level, int x, int y, int w, int h,
		GLenum format, const uint8_t *src, int row_pixels, int lw, int lh)
{
	int bytes = FormatBytes(format);
	int i, j;

	if (t->swizzled)
	{
		uint8_t *dst = t->data + LevelOffset(t->width, t->height, level, 1);

		if (!SwizzleFor(lw, lh))
		{
			return;
		}

		if (x == 0 && y == 0 && w == lw && h == lh)
		{
			/* whole level: swizzle into memory, one copy */
			uint8_t *pix = (uint8_t *)Scratch((size_t)w * h);

			if (!pix)
			{
				return;
			}

			for (j = 0; j < h; j++)
			{
				const uint8_t *s = src + (size_t)j * row_pixels * bytes;

				for (i = 0; i < w; i++, s += bytes)
				{
					pix[swz_tx[i] | swz_ty[j]] = ToB8(s, bytes, format);
				}
			}

			memcpy(dst, pix, (size_t)w * h);
		}
		else
		{
			/* part of it (a tile into its tilesheet): straight to its places */
			for (j = 0; j < h; j++)
			{
				const uint8_t *s = src + (size_t)j * row_pixels * bytes;
				uint32_t oy = swz_ty[y + j];

				for (i = 0; i < w; i++, s += bytes)
				{
					dst[swz_tx[x + i] | oy] = ToB8(s, bytes, format);
				}
			}
		}
	}
	else
	{
		uint8_t *row = (uint8_t *)Scratch((size_t)w);

		if (!row)
		{
			return;
		}

		for (j = 0; j < h; j++)
		{
			const uint8_t *s = src + (size_t)j * row_pixels * bytes;

			for (i = 0; i < w; i++, s += bytes)
			{
				row[i] = ToB8(s, bytes, format);
			}

			memcpy(t->data + (size_t)(y + j) * t->pitch + x, row, (size_t)w);
		}
	}

	ps3gl.st_upload += (uint32_t)w * h;
	ps3gl.tex_written = 1;
}

/* Upload for A1R5G5B5 textures (two bytes a texel), as Upload below */
static void
Upload16(ps3gl_texture_t *t, int level, int x, int y, int w, int h,
		GLenum format, const uint8_t *src, int row_pixels, int lw, int lh)
{
	int bytes = FormatBytes(format);
	int i, j;

	if (t->swizzled)
	{
		uint16_t *dst = (uint16_t *)(t->data + LevelOffset(t->width, t->height, level, 2));

		if (!SwizzleFor(lw, lh))
		{
			return;
		}

		if (x == 0 && y == 0 && w == lw && h == lh)
		{
			uint16_t *pix = (uint16_t *)Scratch((size_t)w * h * 2);

			if (!pix)
			{
				return;
			}

			for (j = 0; j < h; j++)
			{
				const uint8_t *s = src + (size_t)j * row_pixels * bytes;

				for (i = 0; i < w; i++, s += bytes)
				{
					pix[swz_tx[i] | swz_ty[j]] = ToA1R5G5B5(ToARGB(s, bytes, format));
				}
			}

			memcpy(dst, pix, (size_t)w * h * 2);
		}
		else
		{
			for (j = 0; j < h; j++)
			{
				const uint8_t *s = src + (size_t)j * row_pixels * bytes;
				uint32_t oy = swz_ty[y + j];

				for (i = 0; i < w; i++, s += bytes)
				{
					dst[swz_tx[x + i] | oy] = ToA1R5G5B5(ToARGB(s, bytes, format));
				}
			}
		}
	}
	else
	{
		uint16_t *row = (uint16_t *)Scratch((size_t)w * 2);

		if (!row)
		{
			return;
		}

		for (j = 0; j < h; j++)
		{
			const uint8_t *s = src + (size_t)j * row_pixels * bytes;

			for (i = 0; i < w; i++, s += bytes)
			{
				row[i] = ToA1R5G5B5(ToARGB(s, bytes, format));
			}

			memcpy(t->data + (size_t)(y + j) * t->pitch + (size_t)x * 2, row, (size_t)w * 2);
		}
	}

	ps3gl.st_upload += (uint32_t)w * h * 2;
	ps3gl.tex_written = 1;
}

static void
Upload(ps3gl_texture_t *t, int level, int x, int y, int w, int h,
		GLenum format, const uint8_t *src)
{
	int bytes = FormatBytes(format);
	int row_pixels = ps3gl.unpack_row_length ? ps3gl.unpack_row_length : w;
	int lw = LevelDim(t->width, level), lh = LevelDim(t->height, level);
	int i, j;

	if (x < 0 || y < 0 || x + w > lw || y + h > lh || w <= 0 || h <= 0)
	{
		return;
	}

	if (TexelBytes(t) == 1)
	{
		Upload8(t, level, x, y, w, h, format, src, row_pixels, lw, lh);
		return;
	}

	if (TexelBytes(t) == 2)
	{
		Upload16(t, level, x, y, w, h, format, src, row_pixels, lw, lh);
		return;
	}

	if (t->swizzled)
	{
		uint32_t *dst = (uint32_t *)(t->data + LevelOffset(t->width, t->height, level, 4));
		uint32_t *tx, *ty, *pix;

		if (!SwizzleFor(lw, lh))
		{
			return;
		}

		tx = swz_tx;
		ty = swz_ty;
		pix = Scratch((size_t)w * h * sizeof(uint32_t));

		if (!pix)
		{
			return;
		}

		if (x == 0 && y == 0 && w == lw && h == lh)
		{
			/* whole level: swizzle into memory, one copy */
			for (j = 0; j < h; j++)
			{
				const uint8_t *s = src + (size_t)j * row_pixels * bytes;

				for (i = 0; i < w; i++, s += bytes)
				{
					pix[tx[i] | ty[j]] = ToARGB(s, bytes, format);
				}
			}

			memcpy(dst, pix, (size_t)w * h * 4);
		}
		else
		{
			/* part of it (lightmaps): straight to its places */
			for (j = 0; j < h; j++)
			{
				const uint8_t *s = src + (size_t)j * row_pixels * bytes;
				uint32_t oy = ty[y + j];

				for (i = 0; i < w; i++, s += bytes)
				{
					dst[tx[x + i] | oy] = ToARGB(s, bytes, format);
				}
			}
		}
	}
	else
	{
		uint32_t *row = Scratch((size_t)w * sizeof(uint32_t));

		if (!row)
		{
			return;
		}

		for (j = 0; j < h; j++)
		{
			const uint8_t *s = src + (size_t)j * row_pixels * bytes;

			for (i = 0; i < w; i++, s += bytes)
			{
				row[i] = ToARGB(s, bytes, format);
			}

			memcpy(t->data + (size_t)(y + j) * t->pitch + (size_t)x * 4, row, (size_t)w * 4);
		}
	}

	ps3gl.st_upload += (uint32_t)w * h * 4;
	ps3gl.tex_written = 1;
}

static void
TexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width,
		GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	ps3gl_texture_t *t = Bound();

	(void)target;
	(void)internalFormat;
	(void)border;

	/* GL_UNSIGNED_INT_8_8_8_8 on a big endian CPU: the first component
	   in the first byte, the same as GL_UNSIGNED_BYTE (EDuke32's 32 bit
	   textures, GL_BGRA) */
	if (type == 0x8035)
	{
		type = GL_UNSIGNED_BYTE;
	}

	if (!t || level < 0 || width <= 0 || height <= 0 || width > 4096 || height > 4096)
	{
		return;
	}

	if (type != GL_UNSIGNED_BYTE)
	{
		static int warned;

		if (!warned)
		{
			PS3_Log("[gl] WARNING: glTexImage2D type 0x%x not supported", (unsigned)type);
			warned = 1;
		}

		return;
	}

	if (level == 0)
	{
		int swz = IsPow2(width) && IsPow2(height);
		int levels = swz ? Log2(width > height ? width : height) + 1 : 1;
		/* one channel in, one channel kept: a byte a texel (B8) */
		int bpp = ((format == GL_RED || format == GL_LUMINANCE) &&
				   (internalFormat == GL_RED || internalFormat == GL_LUMINANCE ||
					internalFormat == PS3GL_R8 || internalFormat == PS3GL_LUMINANCE8 ||
					internalFormat == 1)) ? 1 : pack16 ? 2 : 4;
		uint32_t pitch;
		uint32_t size;

		/* GL_TEXTURE_MAX_LEVEL set first: no room for levels never used
		   (EDuke32's tilesheets are 2048x2048, one level) */
		if (t->max_level && levels > t->max_level)
		{
			levels = t->max_level;
		}

		pitch = swz ? 0 : (((uint32_t)width * bpp + 63) & ~63u);
		size = swz ? LevelOffset(width, height, levels, bpp) : pitch * height;

		/* Same shape and the RSX is done with it: reuse. Otherwise new
		   storage; the old one is freed when the RSX is done. */
		if (!t->data || t->width != width || t->height != height ||
			t->swizzled != swz || t->max_levels != levels || TexelBytes(t) != bpp || Busy(t))
		{
			ReleaseStorage(t);

			t->data = (uint8_t *)ps3gl_rsx_memalign(128, size);

			/* full: the owner frees textures not drawn for a while (a
			   long while first), and again */
			if (!t->data && evict_hook)
			{
				static const int ages[] = { 600, 60, 3 };
				int k, n;

				for (k = 0; k < 3 && !t->data; k++)
				{
					n = evict_hook(ages[k], ps3gl.tmu[ps3gl.active_tmu].tex);

					if (n > 0)
					{
						if (evicted_total == 0 || ((evicted_total + n) >> 10) != (evicted_total >> 10))
							PS3_Log("[gl] RSX memory full: freed %d textures not drawn for %d frames (%u in all)",
									n, ages[k], evicted_total + n);
						evicted_total += n;
						t->data = (uint8_t *)ps3gl_rsx_memalign(128, size);
					}
				}
			}

			if (!t->data)
			{
				static int warned;

				ps3gl_oom = 1;
				tex_fails++;
				tex_fail_bytes += size;

				if (!warned)
				{
					PS3_Log("[gl] WARNING: out of RSX memory for a %dx%d texture "
							"(%u MB of textures)", width, height,
							(unsigned)(ps3gl.st_tex_bytes >> 20));
					warned = 1;
				}

				return;
			}

			rsxAddressToOffset(t->data, &t->offset);
			t->size = size;
			ps3gl.st_tex_bytes += size;
			t->used_frame = ps3gl.frame;

			if (!t->min_filter)
			{
				/* GL's defaults */
				t->min_filter = GL_NEAREST_MIPMAP_LINEAR;
				t->mag_filter = GL_LINEAR;
				t->wrap_s = t->wrap_t = GL_REPEAT;
			}
		}

		t->width = (uint16_t)width;
		t->height = (uint16_t)height;
		t->swizzled = (uint8_t)swz;
		t->bpp = (uint8_t)bpp;
		t->pitch = pitch;
		t->max_levels = (uint8_t)levels;
		t->num_levels = 0;
	}
	else
	{
		/* a mip level: only into the chain level 0 set up */
		if (!t->data || !t->swizzled || level >= t->max_levels ||
			width != LevelDim(t->width, level) || height != LevelDim(t->height, level))
		{
			return;
		}
	}

	if (pixels)
	{
		Upload(t, level, 0, 0, width, height, format, (const uint8_t *)pixels);
	}
	else if (level == 0)
	{
		memset(t->data, 0, t->size);
	}

	/* levels are uploaded in order; a gap would leave garbage */
	if (level == t->num_levels)
	{
		t->num_levels = (uint8_t)(level + 1);
	}

	BuildDescriptor(t);
}

static void
TexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	ps3gl_texture_t *t = Bound();

	(void)target;

	if (type == 0x8035)   /* GL_UNSIGNED_INT_8_8_8_8, see glTexImage2D */
	{
		type = GL_UNSIGNED_BYTE;
	}

	if (!t || !t->data || !pixels || type != GL_UNSIGNED_BYTE ||
		level < 0 || level >= t->num_levels)
	{
		return;
	}

	{
		static int logged;

		if (logged < 0 && t->width >= 1024 && width != 256)   /* (off: patch 14's) */
		{
			const uint8_t *px = (const uint8_t *)pixels;

			PS3_Log("[gl] tilesheet %d (unit %d): %dx%d at %d,%d (format 0x%x), first texels %u %u %u %u",
					ps3gl.tmu[ps3gl.active_tmu].tex, ps3gl.active_tmu, width, height, xoffset,
					yoffset, (unsigned)format, px[0], px[1], px[2], px[3]);
			logged++;
		}
	}

	Upload(t, level, xoffset, yoffset, width, height, format, (const uint8_t *)pixels);
}

/* NBlood-PS3: the uploads' time for the slow frames' log (ps3_prof.c) */
void APIENTRY
glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width,
		GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
	PS3_ProfBegin(PS3P_UPLOAD);
	TexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
	if (pixels && width > 0 && height > 0)
	{
		PS3_ProfBytes(PS3P_UPLOAD, (unsigned)width * (unsigned)height * (unsigned)FormatBytes(format));
	}
	PS3_ProfEnd(PS3P_UPLOAD);
}

void APIENTRY
glTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
	PS3_ProfBegin(PS3P_UPLOAD);
	TexSubImage2D(target, level, xoffset, yoffset, width, height, format, type, pixels);
	if (pixels && width > 0 && height > 0)
	{
		PS3_ProfBytes(PS3P_UPLOAD, (unsigned)width * (unsigned)height * (unsigned)FormatBytes(format));
	}
	PS3_ProfEnd(PS3P_UPLOAD);
}

static void
TexParam(GLenum pname, GLint param)
{
	ps3gl_texture_t *t = Bound();

	if (!t)
	{
		return;
	}

	switch (pname)
	{
		case GL_TEXTURE_MIN_FILTER: t->min_filter = (GLenum)param; break;
		case GL_TEXTURE_MAG_FILTER: t->mag_filter = (GLenum)param; break;
		case GL_TEXTURE_WRAP_S:     t->wrap_s = (GLenum)param; break;
		case GL_TEXTURE_WRAP_T:     t->wrap_t = (GLenum)param; break;
		case GL_TEXTURE_MAX_LEVEL:
			t->max_level = (uint8_t)(param < 0 ? 1 : param > 15 ? 16 : param + 1);
			return;
		default:                    return;   /* GL_GENERATE_MIPMAP... */
	}

	t->dirty = 1;
}

/* The rectangle x, y, w, h of the frame being drawn (GL coordinates,
   origin bottom left) into level 0 of the bound texture at xoffset,
   yoffset. The frame's rows are top down in memory, so the copy comes out
   upside down compared with GL: TyrQuake's warp rendering draws flipped
   on the PS3 to match (gl_warp.c). */
void APIENTRY
glCopyTexSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset,
		GLint x, GLint y, GLsizei width, GLsizei height)
{
	ps3gl_texture_t *t = Bound();
	gcmContextData *ctx = ps3gl.ctx;
	uint32_t rt_offset, rt_pitch;
	int src_y;

	(void)target;

	if (!ctx || !t || !t->data || level != 0 || width <= 0 || height <= 0)
	{
		return;
	}

	/* the render target is 32 bits: not into a B8 texture (nothing does) */
	if (TexelBytes(t) != 4)
	{
		return;
	}

	/* inside the render target and the texture */
	if (x < 0 || y < 0 || x + width > ps3gl.screen_w || y + height > ps3gl.screen_h ||
		xoffset < 0 || yoffset < 0 || xoffset + width > t->width ||
		yoffset + height > t->height)
	{
		static int warned;

		if (!warned)
		{
			PS3_Log("[gl] WARNING: glCopyTexSubImage2D out of bounds (%d,%d %dx%d -> %dx%d texture)",
					x, y, width, height, t->width, t->height);
			warned = 1;
		}

		return;
	}

	if (PS3_Video_RenderTargetInfo(&rt_offset, &rt_pitch) != 0)
	{
		return;
	}

	src_y = ps3gl.screen_h - y - height;

	/* The drawing into the render target must be finished first (another
	   unit of the RSX does the copy); this also means no earlier frame is
	   still sampling the texture. */
	rsxSetWaitForIdle(ctx);
	ps3gl.drew = 0;

	if (t->swizzled)
	{
		rsxSetConvertSwizzleFormat(ctx, t->offset, t->width, t->height,
				(u32)xoffset, (u32)yoffset, rt_offset, rt_pitch, (u32)x, (u32)src_y,
				(u32)width, (u32)height, 4, GCM_TRANSFER_LOCAL_TO_LOCAL);

		/* It left the scaled image unit writing to swizzled surfaces: back
		   to plain ones, or the next frame's blit to the TV goes wrong. */
		rsxSetTransferScaleMode(ctx, GCM_TRANSFER_LOCAL_TO_LOCAL, GCM_TRANSFER_SURFACE);
	}
	else
	{
		rsxSetTransferData(ctx, GCM_TRANSFER_LOCAL_TO_LOCAL,
				t->offset + (uint32_t)yoffset * t->pitch + (uint32_t)xoffset * 4, t->pitch,
				rt_offset + (uint32_t)src_y * rt_pitch + (uint32_t)x * 4, rt_pitch,
				(u32)width * 4, (u32)height);
	}

	if (t->num_levels == 0)
	{
		t->num_levels = 1;
		BuildDescriptor(t);
	}

	t->last_frame = ps3gl.frame;
	ps3gl.copy_pending = 1;
	ps3gl.st_copies++;
}

void APIENTRY
glTexParameteri(GLenum target, GLenum pname, GLint param)
{
	(void)target;
	TexParam(pname, param);
}

void APIENTRY
glTexParameterf(GLenum target, GLenum pname, GLfloat param)
{
	(void)target;
	TexParam(pname, (GLint)param);
}

void APIENTRY
glTexEnvi(GLenum target, GLenum pname, GLint param)
{
	ps3gl_tmu_t *tmu = &ps3gl.tmu[ps3gl.active_tmu];

	(void)target;

	if (pname == GL_TEXTURE_ENV_MODE)
	{
		tmu->env = (GLenum)param;
	}
	else if (pname == GL_RGB_SCALE)
	{
		tmu->rgb_scale = param;
	}
}

void APIENTRY
glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
	glTexEnvi(target, pname, (GLint)param);
}

/* ================================================================ */
/* To the RSX                                                         */
/* ================================================================ */

static uint8_t
Filter(GLenum f, int mipmaps)
{
	if (!mipmaps)
	{
		/* one level: the mipmap modes would sample nothing */
		switch (f)
		{
			case GL_NEAREST:
			case GL_NEAREST_MIPMAP_NEAREST:
			case GL_NEAREST_MIPMAP_LINEAR:
				return GCM_TEXTURE_NEAREST;
			default:
				return GCM_TEXTURE_LINEAR;
		}
	}

	switch (f)
	{
		case GL_NEAREST:                return GCM_TEXTURE_NEAREST;
		case GL_NEAREST_MIPMAP_NEAREST: return GCM_TEXTURE_NEAREST_MIPMAP_NEAREST;
		case GL_LINEAR_MIPMAP_NEAREST:  return GCM_TEXTURE_LINEAR_MIPMAP_NEAREST;
		case GL_NEAREST_MIPMAP_LINEAR:  return GCM_TEXTURE_NEAREST_MIPMAP_LINEAR;
		case GL_LINEAR_MIPMAP_LINEAR:   return GCM_TEXTURE_LINEAR_MIPMAP_LINEAR;
		default:                        return GCM_TEXTURE_LINEAR;
	}
}

static uint8_t
Wrap(GLenum w, int swizzled)
{
	if (!swizzled || w != GL_REPEAT)
	{
		return GCM_TEXTURE_CLAMP_TO_EDGE;
	}

	return GCM_TEXTURE_REPEAT;
}

/* The samplers in 'units' (bit i = sampler i) get their unit's texture,
   the others are turned off. Also marks the textures as used by this
   frame. */
void
ps3gl_apply_textures(unsigned units)
{
	gcmContextData *ctx = ps3gl.ctx;
	int i;

	if (ps3gl.tex_written)
	{
		/* the CPU's writes must land before the RSX samples them */
		__asm__ volatile("sync" ::: "memory");
		rsxInvalidateTextureCache(ctx, GCM_INVALIDATE_TEXTURE);
		ps3gl.tex_written = 0;
	}

	for (i = 0; i < PS3GL_MAX_TMUS; i++)
	{
		ps3gl_tmu_t *tmu = &ps3gl.tmu[i];
		ps3gl_texture_t *t;

		if (!(units & (1u << i)))
		{
			if (tmu->hw_enabled != 0)
			{
				rsxTextureControl(ctx, i, GCM_FALSE, 0, 0, 0);
				tmu->hw_enabled = 0;
				tmu->hw_tex = -2;
			}

			continue;
		}

		t = &ps3gl.textures[tmu->tex];

		/* only Polymost's program asks for units without a texture */
		if (!t->data || !t->num_levels)
		{
			if (!white.data)
			{
				continue;
			}

			if (tmu->hw_tex == -3 && tmu->hw_enabled == 1)
			{
				continue;
			}

			rsxLoadTexture(ctx, i, &white.gt);
			rsxTextureControl(ctx, i, GCM_TRUE, 0, 0, GCM_TEXTURE_MAX_ANISO_1);
			rsxTextureFilter(ctx, i, 0, GCM_TEXTURE_NEAREST, GCM_TEXTURE_NEAREST,
					GCM_TEXTURE_CONVOLUTION_QUINCUNX);
			rsxTextureWrapMode(ctx, i, GCM_TEXTURE_REPEAT, GCM_TEXTURE_REPEAT,
					GCM_TEXTURE_CLAMP_TO_EDGE, GCM_TEXTURE_UNSIGNED_REMAP_NORMAL,
					GCM_TEXTURE_ZFUNC_NEVER, 0);
			tmu->hw_tex = -3;
			tmu->hw_enabled = 1;
			continue;
		}

		t->last_frame = ps3gl.frame;
		t->used_frame = ps3gl.frame;

		if (tmu->hw_tex == tmu->tex && tmu->hw_enabled == 1 && !t->dirty)
		{
			continue;
		}

		{
			int mips = t->swizzled && t->num_levels > 1;
			uint16_t maxlod = (uint16_t)(((t->num_levels ? t->num_levels : 1) - 1) << 8);

			rsxLoadTexture(ctx, i, &t->gt);
			/* maxlod is 4.8 fixed point (IoQuake3-PS3's note) */
			rsxTextureControl(ctx, i, GCM_TRUE, 0, maxlod, GCM_TEXTURE_MAX_ANISO_1);
			rsxTextureFilter(ctx, i, 0, Filter(t->min_filter, mips),
					Filter(t->mag_filter, 0), GCM_TEXTURE_CONVOLUTION_QUINCUNX);
			rsxTextureWrapMode(ctx, i, Wrap(t->wrap_s, t->swizzled),
					Wrap(t->wrap_t, t->swizzled), GCM_TEXTURE_CLAMP_TO_EDGE,
					GCM_TEXTURE_UNSIGNED_REMAP_NORMAL, GCM_TEXTURE_ZFUNC_NEVER, 0);
		}

		tmu->hw_tex = tmu->tex;
		tmu->hw_enabled = 1;
		t->dirty = 0;
	}
}
