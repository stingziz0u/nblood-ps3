// EDuke32-PS3: glad's pointers that stayed NULL, from ps3gl.
//
// ps3gl says OpenGL 2.1, so gladLoadGLLoader leaves every GL 3.x and 4.x
// entry point NULL (sync and sampler objects, glMapBufferRange,
// glBufferStorage...). EDuke32 calls some of them anyway, believing they
// are there on any real driver: polymost_initdrawpoly calls glIsSync
// before anything else. Every GL function the engine and the game call
// (the glad_gl* symbols their objects use) gets ps3gl's, or ps3gl's
// function that does nothing.
//
// The list is every glad_gl* symbol the engine's and the game's objects
// use; regenerate it if new GL calls appear (a NULL one is a crash):
//   for f in obj/build/*.o obj/duke3d/*.o; do ppu-nm -u $f; done |
//     grep -o 'glad_gl[A-Za-z0-9_]*' | sort -u
// glGetGraphicsResetStatus(KHR) stay NULL: fill_glinfo tests them.
//
// Copyright (C) 2026 the EDuke32-PS3 contributors. GPL v2 or later.

#include "glad/glad.h"

extern "C" void *PS3GL_GetProcAddress(const char *name);

static const struct { void **ptr; const char *name; } used[] = {
    { (void **)&glad_glActiveTexture, "glActiveTexture" },
    { (void **)&glad_glAlphaFunc, "glAlphaFunc" },
    { (void **)&glad_glAttachShader, "glAttachShader" },
    { (void **)&glad_glBegin, "glBegin" },
    { (void **)&glad_glBindAttribLocation, "glBindAttribLocation" },
    { (void **)&glad_glBindBuffer, "glBindBuffer" },
    { (void **)&glad_glBindSampler, "glBindSampler" },
    { (void **)&glad_glBindTexture, "glBindTexture" },
    { (void **)&glad_glBlendFunc, "glBlendFunc" },
    { (void **)&glad_glBufferData, "glBufferData" },
    { (void **)&glad_glBufferStorage, "glBufferStorage" },
    { (void **)&glad_glBufferSubData, "glBufferSubData" },
    { (void **)&glad_glClear, "glClear" },
    { (void **)&glad_glClearColor, "glClearColor" },
    { (void **)&glad_glClientActiveTexture, "glClientActiveTexture" },
    { (void **)&glad_glClientWaitSync, "glClientWaitSync" },
    { (void **)&glad_glColor3f, "glColor3f" },
    { (void **)&glad_glColor4f, "glColor4f" },
    { (void **)&glad_glColor4ub, "glColor4ub" },
    { (void **)&glad_glColorMask, "glColorMask" },
    { (void **)&glad_glCompileShader, "glCompileShader" },
    { (void **)&glad_glCompressedTexImage2D, "glCompressedTexImage2D" },
    { (void **)&glad_glCreateProgram, "glCreateProgram" },
    { (void **)&glad_glCreateShader, "glCreateShader" },
    { (void **)&glad_glCullFace, "glCullFace" },
    { (void **)&glad_glDebugMessageInsertARB, "glDebugMessageInsertARB" },
    { (void **)&glad_glDeleteBuffers, "glDeleteBuffers" },
    { (void **)&glad_glDeleteProgram, "glDeleteProgram" },
    { (void **)&glad_glDeleteShader, "glDeleteShader" },
    { (void **)&glad_glDeleteSync, "glDeleteSync" },
    { (void **)&glad_glDeleteTextures, "glDeleteTextures" },
    { (void **)&glad_glDepthFunc, "glDepthFunc" },
    { (void **)&glad_glDepthMask, "glDepthMask" },
    { (void **)&glad_glDetachShader, "glDetachShader" },
    { (void **)&glad_glDisable, "glDisable" },
    { (void **)&glad_glDisableClientState, "glDisableClientState" },
    { (void **)&glad_glDisableVertexAttribArray, "glDisableVertexAttribArray" },
    { (void **)&glad_glDrawArrays, "glDrawArrays" },
    { (void **)&glad_glDrawElements, "glDrawElements" },
    { (void **)&glad_glEnable, "glEnable" },
    { (void **)&glad_glEnableClientState, "glEnableClientState" },
    { (void **)&glad_glEnableVertexAttribArray, "glEnableVertexAttribArray" },
    { (void **)&glad_glEnd, "glEnd" },
    { (void **)&glad_glFenceSync, "glFenceSync" },
    { (void **)&glad_glFogf, "glFogf" },
    { (void **)&glad_glFogfv, "glFogfv" },
    { (void **)&glad_glFogi, "glFogi" },
    { (void **)&glad_glFrontFace, "glFrontFace" },
    { (void **)&glad_glGenBuffers, "glGenBuffers" },
    { (void **)&glad_glGenSamplers, "glGenSamplers" },
    { (void **)&glad_glGenTextures, "glGenTextures" },
    { (void **)&glad_glGetCompressedTexImage, "glGetCompressedTexImage" },
    { (void **)&glad_glGetError, "glGetError" },
    { (void **)&glad_glGetFloatv, "glGetFloatv" },
    { (void **)&glad_glGetIntegerv, "glGetIntegerv" },
    { (void **)&glad_glGetShaderInfoLog, "glGetShaderInfoLog" },
    { (void **)&glad_glGetShaderiv, "glGetShaderiv" },
    { (void **)&glad_glGetString, "glGetString" },
    { (void **)&glad_glGetTexLevelParameteriv, "glGetTexLevelParameteriv" },
    { (void **)&glad_glGetUniformLocation, "glGetUniformLocation" },
    { (void **)&glad_glHint, "glHint" },
    { (void **)&glad_glIsBuffer, "glIsBuffer" },
    { (void **)&glad_glIsProgram, "glIsProgram" },
    { (void **)&glad_glIsSampler, "glIsSampler" },
    { (void **)&glad_glIsSync, "glIsSync" },
    { (void **)&glad_glIsTexture, "glIsTexture" },
    { (void **)&glad_glLinkProgram, "glLinkProgram" },
    { (void **)&glad_glLoadIdentity, "glLoadIdentity" },
    { (void **)&glad_glLoadMatrixf, "glLoadMatrixf" },
    { (void **)&glad_glMapBuffer, "glMapBuffer" },
    { (void **)&glad_glMapBufferRange, "glMapBufferRange" },
    { (void **)&glad_glMatrixMode, "glMatrixMode" },
    { (void **)&glad_glMultMatrixf, "glMultMatrixf" },
    { (void **)&glad_glMultiTexCoord2f, "glMultiTexCoord2f" },
    { (void **)&glad_glOrtho, "glOrtho" },
    { (void **)&glad_glPixelStorei, "glPixelStorei" },
    { (void **)&glad_glPolygonMode, "glPolygonMode" },
    { (void **)&glad_glPopMatrix, "glPopMatrix" },
    { (void **)&glad_glPushMatrix, "glPushMatrix" },
    { (void **)&glad_glReadPixels, "glReadPixels" },
    { (void **)&glad_glRecti, "glRecti" },
    { (void **)&glad_glSamplerParameterf, "glSamplerParameterf" },
    { (void **)&glad_glSamplerParameteri, "glSamplerParameteri" },
    { (void **)&glad_glScalef, "glScalef" },
    { (void **)&glad_glScissor, "glScissor" },
    { (void **)&glad_glShaderSource, "glShaderSource" },
    { (void **)&glad_glStencilFunc, "glStencilFunc" },
    { (void **)&glad_glStencilOp, "glStencilOp" },
    { (void **)&glad_glTexCoord2f, "glTexCoord2f" },
    { (void **)&glad_glTexCoordPointer, "glTexCoordPointer" },
    { (void **)&glad_glTexEnvf, "glTexEnvf" },
    { (void **)&glad_glTexImage2D, "glTexImage2D" },
    { (void **)&glad_glTexParameterf, "glTexParameterf" },
    { (void **)&glad_glTexParameteri, "glTexParameteri" },
    { (void **)&glad_glTexSubImage2D, "glTexSubImage2D" },
    { (void **)&glad_glTranslatef, "glTranslatef" },
    { (void **)&glad_glUniform1f, "glUniform1f" },
    { (void **)&glad_glUniform1i, "glUniform1i" },
    { (void **)&glad_glUniform2f, "glUniform2f" },
    { (void **)&glad_glUniform4f, "glUniform4f" },
    { (void **)&glad_glUniformMatrix4fv, "glUniformMatrix4fv" },
    { (void **)&glad_glUnmapBuffer, "glUnmapBuffer" },
    { (void **)&glad_glUseProgram, "glUseProgram" },
    { (void **)&glad_glVertex2f, "glVertex2f" },
    { (void **)&glad_glVertex2i, "glVertex2i" },
    { (void **)&glad_glVertex3fv, "glVertex3fv" },
    { (void **)&glad_glVertexAttribPointer, "glVertexAttribPointer" },
    { (void **)&glad_glVertexPointer, "glVertexPointer" },
    { (void **)&glad_glViewport, "glViewport" },
};

int PS3_GladFill(void)
{
    int filled = 0;

    for (auto const &u : used)
        if (!*u.ptr)
        {
            *u.ptr = PS3GL_GetProcAddress(u.name);
            filled++;
        }

    return filled;
}
