#pragma once

/// @file BaseVersion.hpp
/// @brief Compile-time NGIN.Base package version.

#include <NGIN/Primitives.hpp>

#include <string_view>

#define NGIN_BASE_VERSION_MAJOR 0
#define NGIN_BASE_VERSION_MINOR 1
#define NGIN_BASE_VERSION_PATCH 0
#define NGIN_BASE_VERSION_STRING "0.1.0"

namespace NGIN
{
    struct BaseVersion final
    {
        static constexpr UInt32           Major  = NGIN_BASE_VERSION_MAJOR;
        static constexpr UInt32           Minor  = NGIN_BASE_VERSION_MINOR;
        static constexpr UInt32           Patch  = NGIN_BASE_VERSION_PATCH;
        static constexpr std::string_view String = NGIN_BASE_VERSION_STRING;
    };
}// namespace NGIN
