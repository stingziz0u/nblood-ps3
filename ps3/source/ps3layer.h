// PS3 interface layer for the Build Engine (ps3layer.cpp)
// What the game sees of it, as sdlayer.h for SDL.

#ifndef build_interface_layer_
#define build_interface_layer_ PS3

#include "baselayer.h"
#include "compat.h"

extern int32_t maxrefreshfreq;

static inline void idle(int const msec = 1)
{
    timespec req = { 0, msec * 1000000 };
    nanosleep(&req, nullptr);
}

static inline void idle_waitevent_timeout(uint32_t timeout)
{
    idle(timeout > 10 ? 10 : (int)timeout);
}

static inline void idle_waitevent(void)
{
    idle(1);
}

#else
#if (build_interface_layer_ != PS3)
#error "Already using the " build_interface_layer_ ". Can't now use PS3."
#endif
#endif // build_interface_layer_
