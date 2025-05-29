#pragma once

#ifdef __linux__
    #define RWC_PLATFORM_LINUX 1
#elif defined(_WIN32) || defined(WIN32) || defined(__CYGWIN__) || defined(__MINGW32__)
    #define RWC_PLATFORM_WINDOWS 1
#else
    #error "Platform not supported"
#endif

#ifdef RWC_BUILD_SHARED
    #ifdef RWC_PLATFORM_WINDOWS
        #if defined(RWC_EXPORTS)
            #define RWC_API __declspec(dllexport)
        #else
            #define RWC_API __declspec(dllimport)
        #endif
    #elif defined(RWC_PLATFORM_LINUX) && (defined(__GNUC__) || defined(__clang__))
        #if defined(RWC_EXPORTS)
            #define RWC_API __attribute__((visibility("default")))
        #else
            #define RWC_API
        #endif
    #else
        #define RWC_API
    #endif
#else
    #define RWC_API
#endif

#ifndef NDEBUG
    #define RWC_DEBUG_MODE 1
#endif
