#pragma once

#ifndef libasync_config_h__
#define libasync_config_h__

#define LIBASYNC_STATIC
#define LIBASYNC_NO_EXCEPTIONS

#define LIBASYNC_ALIGNED_ALLOC(size, align) Xaligned_alloc(size, align)
#define LIBASYNC_ALIGNED_FREE(ptr)          Xaligned_free(ptr)

#ifdef __PPU__
// No std::thread on the PS3: the tasks run at once (ps3/compat/ps3_async.h)
# include "ps3_async.h"
#else
# include "libasync.h"
#endif

#endif  // libasync_config_h__
