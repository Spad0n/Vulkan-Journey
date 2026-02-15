#include "ctl/info.hpp"

#if defined(CTL_HOST_PLATFORM_WINDOWS)
#pragma message("INFO: Compiling Windows system")
#include "system_windows.cpp"
#elif defined(CTL_HOST_PLATFORM_POSIX)
#pragma message("INFO: Compiling POSIX System")
#include "system_posix.cpp"
#else
#error "Unknown Platform"
#endif
