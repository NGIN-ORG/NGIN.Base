#pragma once

#if defined(_WIN32)
#include <NGIN/Crypto/Random.win32.hpp>
#elif defined(__unix__) || defined(__APPLE__)
#include <NGIN/Crypto/Random.posix.hpp>
#else
#error "No secure random implementation for this platform."
#endif
