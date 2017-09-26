#pragma once

// Flag shows if bot was compiled in debug mode
#if defined(BOT_DEBUG)
#define BOT_DEBUG 1
#else
#define BOT_DEBUG 0
#endif

#if __EMSCRIPTEN__
#include <emscripten.h>
#define EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define EXPORT extern "C"
#endif

#if defined(__APPLE__)
#define PLATFORM_APPLE 1
#else
#define PLATFORM_APPLE 0
#endif

#if NDEBUG
#define RELEASE_MODE 1
#define DEBUG_MODE 0
#else
#define RELEASE_MODE 0
#define DEBUG_MODE 1
#endif
