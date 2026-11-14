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
 * ps3gl: every GL entry point by name, for gladLoadGLLoader (EDuke32
 * calls GL only through glad's function pointers).
 *
 * The table lists every gl* function ps3gl defines; add new ones here.
 * (This file doesn't include GL/gl.h: the declarations below only give
 * the addresses, the real prototypes are in the files that define them.)
 *
 * A name ps3gl doesn't have gets a function that does nothing and returns
 * 0: glad asks for every GL 2.1 entry point, most of which EDuke32 never
 * calls, and a NULL one it did call would be a crash. What EDuke32 does
 * call of those (sync and sampler objects, glVertexAttribPointer for
 * Polymer...) is fine doing nothing: glinfo says they aren't there. Only
 * the names EDuke32 tests for being there (robustness) are NULL.
 *
 * =======================================================================
 */

#include <stdio.h>
#include <string.h>


extern void glActiveTexture(void);
extern void glAlphaFunc(void);
extern void glAttachShader(void);
extern void glBegin(void);
extern void glBindBuffer(void);
extern void glBindTexture(void);
extern void glBlendFunc(void);
extern void glBufferData(void);
extern void glBufferSubData(void);
extern void glClear(void);
extern void glClearColor(void);
extern void glClearDepth(void);
extern void glClearStencil(void);
extern void glClientActiveTexture(void);
extern void glColor3f(void);
extern void glColor3fv(void);
extern void glColor3ub(void);
extern void glColor3ubv(void);
extern void glColor4f(void);
extern void glColor4fv(void);
extern void glColor4ub(void);
extern void glColor4ubv(void);
extern void glColorMask(void);
extern void glColorPointer(void);
extern void glCompileShader(void);
extern void glCopyTexSubImage2D(void);
extern void glCreateProgram(void);
extern void glCreateShader(void);
extern void glCullFace(void);
extern void glDeleteBuffers(void);
extern void glDeleteProgram(void);
extern void glDeleteShader(void);
extern void glDeleteTextures(void);
extern void glDepthFunc(void);
extern void glDepthMask(void);
extern void glDepthRange(void);
extern void glDetachShader(void);
extern void glDisable(void);
extern void glDisableClientState(void);
extern void glDrawArrays(void);
extern void glDrawBuffer(void);
extern void glDrawElements(void);
extern void glDrawRangeElements(void);
extern void glEnable(void);
extern void glEnableClientState(void);
extern void glEnd(void);
extern void glFinish(void);
extern void glFlush(void);
extern void glFogf(void);
extern void glFogfv(void);
extern void glFogi(void);
extern void glFrontFace(void);
extern void glFrustum(void);
extern void glGenBuffers(void);
extern void glGenSamplers(void);
extern void glGenTextures(void);
extern void glGetBooleanv(void);
extern void glGetError(void);
extern void glGetFloatv(void);
extern void glGetIntegerv(void);
extern void glGetProgramiv(void);
extern void glGetShaderInfoLog(void);
extern void glGetShaderiv(void);
extern void glGetString(void);
extern void glGetTexLevelParameteriv(void);
extern void glGetTexParameterfv(void);
extern void glGetUniformLocation(void);
extern void glHint(void);
extern void glIsBuffer(void);
extern void glIsProgram(void);
extern void glIsShader(void);
extern void glIsTexture(void);
extern void glLineWidth(void);
extern void glLinkProgram(void);
extern void glLoadIdentity(void);
extern void glLoadMatrixf(void);
extern void glMapBuffer(void);
extern void glMapBufferRange(void);
extern void glMatrixMode(void);
extern void glMultMatrixf(void);
extern void glMultiTexCoord2f(void);
extern void glNormalPointer(void);
extern void glOrtho(void);
extern void glPixelStorei(void);
extern void glPointSize(void);
extern void glPolygonMode(void);
extern void glPolygonOffset(void);
extern void glPopMatrix(void);
extern void glPushMatrix(void);
extern void glReadBuffer(void);
extern void glReadPixels(void);
extern void glRecti(void);
extern void glRotatef(void);
extern void glScalef(void);
extern void glScissor(void);
extern void glShadeModel(void);
extern void glShaderSource(void);
extern void glStencilFunc(void);
extern void glStencilMask(void);
extern void glStencilOp(void);
extern void glTexCoord2f(void);
extern void glTexCoord2fv(void);
extern void glTexCoordPointer(void);
extern void glTexEnvf(void);
extern void glTexEnvi(void);
extern void glTexImage2D(void);
extern void glTexParameterf(void);
extern void glTexParameteri(void);
extern void glTexSubImage2D(void);
extern void glTranslatef(void);
extern void glUniform1f(void);
extern void glUniform1i(void);
extern void glUniform2f(void);
extern void glUniform2fv(void);
extern void glUniform4f(void);
extern void glUniform4fv(void);
extern void glUniformMatrix4fv(void);
extern void glUnmapBuffer(void);
extern void glUseProgram(void);
extern void glVertex2f(void);
extern void glVertex2i(void);
extern void glVertex3f(void);
extern void glVertex3fv(void);
extern void glVertexPointer(void);
extern void glViewport(void);

static const struct {
	const char *name;
	void *func;
} procs[] = {
	{ "glActiveTexture", (void *)glActiveTexture },
	{ "glAlphaFunc", (void *)glAlphaFunc },
	{ "glAttachShader", (void *)glAttachShader },
	{ "glBegin", (void *)glBegin },
	{ "glBindBuffer", (void *)glBindBuffer },
	{ "glBindTexture", (void *)glBindTexture },
	{ "glBlendFunc", (void *)glBlendFunc },
	{ "glBufferData", (void *)glBufferData },
	{ "glBufferSubData", (void *)glBufferSubData },
	{ "glClear", (void *)glClear },
	{ "glClearColor", (void *)glClearColor },
	{ "glClearDepth", (void *)glClearDepth },
	{ "glClearStencil", (void *)glClearStencil },
	{ "glClientActiveTexture", (void *)glClientActiveTexture },
	{ "glColor3f", (void *)glColor3f },
	{ "glColor3fv", (void *)glColor3fv },
	{ "glColor3ub", (void *)glColor3ub },
	{ "glColor3ubv", (void *)glColor3ubv },
	{ "glColor4f", (void *)glColor4f },
	{ "glColor4fv", (void *)glColor4fv },
	{ "glColor4ub", (void *)glColor4ub },
	{ "glColor4ubv", (void *)glColor4ubv },
	{ "glColorMask", (void *)glColorMask },
	{ "glColorPointer", (void *)glColorPointer },
	{ "glCompileShader", (void *)glCompileShader },
	{ "glCopyTexSubImage2D", (void *)glCopyTexSubImage2D },
	{ "glCreateProgram", (void *)glCreateProgram },
	{ "glCreateShader", (void *)glCreateShader },
	{ "glCullFace", (void *)glCullFace },
	{ "glDeleteBuffers", (void *)glDeleteBuffers },
	{ "glDeleteProgram", (void *)glDeleteProgram },
	{ "glDeleteShader", (void *)glDeleteShader },
	{ "glDeleteTextures", (void *)glDeleteTextures },
	{ "glDepthFunc", (void *)glDepthFunc },
	{ "glDepthMask", (void *)glDepthMask },
	{ "glDepthRange", (void *)glDepthRange },
	{ "glDetachShader", (void *)glDetachShader },
	{ "glDisable", (void *)glDisable },
	{ "glDisableClientState", (void *)glDisableClientState },
	{ "glDrawArrays", (void *)glDrawArrays },
	{ "glDrawBuffer", (void *)glDrawBuffer },
	{ "glDrawElements", (void *)glDrawElements },
	{ "glDrawRangeElements", (void *)glDrawRangeElements },
	{ "glEnable", (void *)glEnable },
	{ "glEnableClientState", (void *)glEnableClientState },
	{ "glEnd", (void *)glEnd },
	{ "glFinish", (void *)glFinish },
	{ "glFlush", (void *)glFlush },
	{ "glFogf", (void *)glFogf },
	{ "glFogfv", (void *)glFogfv },
	{ "glFogi", (void *)glFogi },
	{ "glFrontFace", (void *)glFrontFace },
	{ "glFrustum", (void *)glFrustum },
	{ "glGenBuffers", (void *)glGenBuffers },
	{ "glGenSamplers", (void *)glGenSamplers },
	{ "glGenTextures", (void *)glGenTextures },
	{ "glGetBooleanv", (void *)glGetBooleanv },
	{ "glGetError", (void *)glGetError },
	{ "glGetFloatv", (void *)glGetFloatv },
	{ "glGetIntegerv", (void *)glGetIntegerv },
	{ "glGetProgramiv", (void *)glGetProgramiv },
	{ "glGetShaderInfoLog", (void *)glGetShaderInfoLog },
	{ "glGetShaderiv", (void *)glGetShaderiv },
	{ "glGetString", (void *)glGetString },
	{ "glGetTexLevelParameteriv", (void *)glGetTexLevelParameteriv },
	{ "glGetTexParameterfv", (void *)glGetTexParameterfv },
	{ "glGetUniformLocation", (void *)glGetUniformLocation },
	{ "glHint", (void *)glHint },
	{ "glIsBuffer", (void *)glIsBuffer },
	{ "glIsProgram", (void *)glIsProgram },
	{ "glIsShader", (void *)glIsShader },
	{ "glIsTexture", (void *)glIsTexture },
	{ "glLineWidth", (void *)glLineWidth },
	{ "glLinkProgram", (void *)glLinkProgram },
	{ "glLoadIdentity", (void *)glLoadIdentity },
	{ "glLoadMatrixf", (void *)glLoadMatrixf },
	{ "glMapBuffer", (void *)glMapBuffer },
	{ "glMapBufferRange", (void *)glMapBufferRange },
	{ "glMatrixMode", (void *)glMatrixMode },
	{ "glMultMatrixf", (void *)glMultMatrixf },
	{ "glMultiTexCoord2f", (void *)glMultiTexCoord2f },
	{ "glNormalPointer", (void *)glNormalPointer },
	{ "glOrtho", (void *)glOrtho },
	{ "glPixelStorei", (void *)glPixelStorei },
	{ "glPointSize", (void *)glPointSize },
	{ "glPolygonMode", (void *)glPolygonMode },
	{ "glPolygonOffset", (void *)glPolygonOffset },
	{ "glPopMatrix", (void *)glPopMatrix },
	{ "glPushMatrix", (void *)glPushMatrix },
	{ "glReadBuffer", (void *)glReadBuffer },
	{ "glReadPixels", (void *)glReadPixels },
	{ "glRecti", (void *)glRecti },
	{ "glRotatef", (void *)glRotatef },
	{ "glScalef", (void *)glScalef },
	{ "glScissor", (void *)glScissor },
	{ "glShadeModel", (void *)glShadeModel },
	{ "glShaderSource", (void *)glShaderSource },
	{ "glStencilFunc", (void *)glStencilFunc },
	{ "glStencilMask", (void *)glStencilMask },
	{ "glStencilOp", (void *)glStencilOp },
	{ "glTexCoord2f", (void *)glTexCoord2f },
	{ "glTexCoord2fv", (void *)glTexCoord2fv },
	{ "glTexCoordPointer", (void *)glTexCoordPointer },
	{ "glTexEnvf", (void *)glTexEnvf },
	{ "glTexEnvi", (void *)glTexEnvi },
	{ "glTexImage2D", (void *)glTexImage2D },
	{ "glTexParameterf", (void *)glTexParameterf },
	{ "glTexParameteri", (void *)glTexParameteri },
	{ "glTexSubImage2D", (void *)glTexSubImage2D },
	{ "glTranslatef", (void *)glTranslatef },
	{ "glUniform1f", (void *)glUniform1f },
	{ "glUniform1i", (void *)glUniform1i },
	{ "glUniform2f", (void *)glUniform2f },
	{ "glUniform2fv", (void *)glUniform2fv },
	{ "glUniform4f", (void *)glUniform4f },
	{ "glUniform4fv", (void *)glUniform4fv },
	{ "glUniformMatrix4fv", (void *)glUniformMatrix4fv },
	{ "glUnmapBuffer", (void *)glUnmapBuffer },
	{ "glUseProgram", (void *)glUseProgram },
	{ "glVertex2f", (void *)glVertex2f },
	{ "glVertex2i", (void *)glVertex2i },
	{ "glVertex3f", (void *)glVertex3f },
	{ "glVertex3fv", (void *)glVertex3fv },
	{ "glVertexPointer", (void *)glVertexPointer },
	{ "glViewport", (void *)glViewport },
};

/* Names that must look absent (baselayer.cpp's fill_glinfo tests them) */
static const char *const absent[] = {
	"glGetGraphicsResetStatus",
	"glGetGraphicsResetStatusARB",
	"glGetGraphicsResetStatusKHR",
};

static long
Nothing(void)
{
	return 0;
}

static void *
Find(const char *name)
{
	size_t i;

	for (i = 0; i < sizeof(procs) / sizeof(procs[0]); i++)
	{
		if (!strcmp(name, procs[i].name))
		{
			return procs[i].func;
		}
	}

	return NULL;
}

void *
PS3GL_GetProcAddress(const char *name)
{
	char base[64];
	size_t i, len;
	void *f;

	if (!name)
	{
		return NULL;
	}

	for (i = 0; i < sizeof(absent) / sizeof(absent[0]); i++)
	{
		if (!strcmp(name, absent[i]))
		{
			return NULL;
		}
	}

	if ((f = Find(name)) != NULL)
	{
		return f;
	}

	/* glActiveTextureARB, glDrawRangeElementsEXT...: the core function */
	len = strlen(name);

	if (len > 3 && len < sizeof(base) &&
		(!strcmp(name + len - 3, "ARB") || !strcmp(name + len - 3, "EXT")))
	{
		memcpy(base, name, len - 3);
		base[len - 3] = 0;

		if ((f = Find(base)) != NULL)
		{
			return f;
		}
	}

	return (void *)Nothing;
}
