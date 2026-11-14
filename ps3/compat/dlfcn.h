/* PS3: no dynamic libraries. glad.c and glbuild.cpp include this; the GL
   entry points come from ps3gl through gladLoadGLLoader (ps3layer.cpp). */
#ifndef PS3_COMPAT_DLFCN_H
#define PS3_COMPAT_DLFCN_H

#define RTLD_LAZY   1
#define RTLD_NOW    2
#define RTLD_GLOBAL 0x100
#define RTLD_LOCAL  0

static inline void *dlopen(const char *f, int m) { (void)f; (void)m; return 0; }
static inline void *dlsym(void *h, const char *s) { (void)h; (void)s; return 0; }
static inline int dlclose(void *h) { (void)h; return 0; }
static inline char *dlerror(void) { return (char *)"no dynamic libraries on the PS3"; }

#endif
