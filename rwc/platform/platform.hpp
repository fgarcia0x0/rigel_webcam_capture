#pragma once

#ifdef __linux__
    #define RWC_PLATFORM_LINUX 1
#elif defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
    #define RWC_PLATFORM_WIN 1
#else
    #error "Platform not supported"
#endif

#ifndef NDEBUG
    #define RWC_DEBUG_MODE 1
#endif
