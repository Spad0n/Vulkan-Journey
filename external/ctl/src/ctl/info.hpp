#ifndef CTL_INFO_H
#define CTL_INFO_H

// Work out the compiler being used.
#if defined(__clang__)
    #define CTL_COMPILER_CLANG
#elif defined(__GNUC__) || defined(__GNUG__)
    #define CTL_COMPILER_GCC
#elif defined(_MSC_VER)
    #define CTL_COMPILER_MSVC
#else
    #error Unsupported compiler
#endif

// Work out the host platform being targeted.
#if defined(_WIN32)
    #define CTL_HOST_PLATFORM_WINDOWS
#elif defined(__linux__)
    #define CTL_HOST_PLATFORM_LINUX
    #define CTL_HOST_PLATFORM_POSIX
#elif defined(__APPLE__) && defined(__MACH__)
    #define CTL_HOST_PLATFORM_MACOS
    #define CTL_HOST_PLATFORM_POSIX
#else
    #error Unsupported platform
#endif

#if defined(CTL_COMPILER_CLANG) || defined(CTL_COMPILER_GCC)
    #define CTL_FORCEINLINE __attribute__((__always_inline__)) inline
#elif defined(CTL_COMPILER_MSVC)
    #define CTL_FORCEINLINE __forceinline
#endif

#if defined(__has_builtin)
    #define CTL_HAS_BUILTIN(...) __has_builtin(__VA_ARGS__)
#else
    #define CTL_HAS_BUILTIN(...) 0
#endif

#if defined(__has_feature)
    #define CTL_HAS_FEATURE(...) __has_feature(__VA_ARGS__)
#else
    #define CTL_HAS_FEATURE(...) 0
#endif

#if defined(__has_include)
    #define CTL_HAS_INCLUDE(...) __has_include(__VA_ARGS__)
#else
    #define CTL_HAS_INCLUDE(...) 0
#endif

#if defined(CTL_COMPILER_MSVC)
#pragma warning(disable : 4200) // Zero-sized array
#endif

// These are debug build options
//#define CTL_CFG_USE_MALLOC 1

#endif // CTL_INFO_H
