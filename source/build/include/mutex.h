#ifndef mutex_h_
#define mutex_h_

/* Mutual exclusion mechanism wrappers for the different platforms */

#ifdef RENDERTYPESDL
# define SDL_MAIN_HANDLED
# include "sdl_inc.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

#if defined __PPU__
// PS3: lv2's lightweight mutex (user space when uncontended)
# include <lv2/mutex.h>
typedef sys_lwmutex_t mutex_t;
#elif SDL_MAJOR_VERSION >= 2
typedef SDL_SpinLock mutex_t;
#elif defined _WIN32
# include "windows_inc.h"
typedef CRITICAL_SECTION mutex_t;
#elif SDL_MAJOR_VERSION == 1
typedef SDL_mutex * mutex_t;
#else
# error No mutex implementation provided.
#endif

extern int32_t mutex_init(mutex_t *mutex);
extern void mutex_destroy(mutex_t *mutex);

static FORCE_INLINE void mutex_lock(mutex_t *mutex)
{
#if defined __PPU__
    sysLwMutexLock(mutex, 0);
#elif SDL_MAJOR_VERSION >= 2
    SDL_AtomicLock(mutex);
#elif defined _WIN32
    EnterCriticalSection(mutex);
#elif SDL_MAJOR_VERSION == 1
    SDL_LockMutex(*mutex);
#endif
}

static FORCE_INLINE void mutex_unlock(mutex_t *mutex)
{
#if defined __PPU__
    sysLwMutexUnlock(mutex);
#elif SDL_MAJOR_VERSION >= 2
    SDL_AtomicUnlock(mutex);
#elif defined _WIN32
    LeaveCriticalSection(mutex);
#elif SDL_MAJOR_VERSION == 1
    SDL_UnlockMutex(*mutex);
#endif
}

#if SDL_MAJOR_VERSION != 1
static FORCE_INLINE bool mutex_try(mutex_t *mutex)
{
#if defined __PPU__
    return sysLwMutexTryLock(mutex) == 0;
#elif SDL_MAJOR_VERSION >= 2
    return SDL_AtomicTryLock(mutex);
#elif defined _WIN32
    return TryEnterCriticalSection(mutex);
#else
# error No mutex try implementation provided.
#endif
}
#endif

#ifdef __cplusplus
}
#endif

#endif
