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
#elif defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
    #define CTL_HOST_PLATFORM_WASM
#else
    #error Unsupported platform
#endif

#if defined(__SIZEOF_POINTER__)
    #if __SIZEOF_POINTER__ == 4
        #define CTL_ARCH_32BIT
    #elif __SIZEOF_POINTER__ == 8
        #define CTL_ARCH_64BIT
    #else
        #error Unsupported pointer size
    #endif
#elif defined(_WIN64) || defined(__x86_64__) || defined(__aarch64__) || defined(__wasm64__)
    #define CTL_ARCH_64BIT
#else
    #define CTL_ARCH_32BIT
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

#if defined(__wasm__) || defined(__wasm32__) || defined(__wasm64__)
    #define CTL_ARCH_WASM
#elif defined(__x86_64__) || defined(_M_X64)
    #define CTL_ARCH_X64
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define CTL_ARCH_ARM64
#endif

#if defined(CTL_HOST_PLATFORM_WASM) && defined(CTL_COMPILER_CLANG)
    #define CTL_WASM_IMPORT(mod, name) __attribute__((import_module(#mod), import_name(#name)))
    #define CTL_WASM_EXPORT extern "C" __attribute__((visibility("default")))
#else
    #define CTL_WASM_IMPORT(mod, name)
    #define CTL_WASM_EXPORT
#endif

#if defined(CTL_COMPILER_GCC) || defined(CTL_COMPILER_CLANG)
    #define CTL_FORMAT_PRINTF(fmt_idx, args_idx) __attribute__((format(printf, fmt_idx, args_idx)))
#else
    #define CTL_FORMAT_PRINTF(fmt_idx, args_idx)
#endif

#endif // CTL_INFO_H
