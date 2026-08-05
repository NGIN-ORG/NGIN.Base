/// @file AtomicWriteOptions.hpp
/// @brief Policy for replace-by-rename filesystem writes.
#pragma once

#include <string_view>

namespace NGIN::IO
{
    /// @brief Controls temporary-file creation and durability for atomic write helpers.
    struct AtomicWriteOptions
    {
        bool             createParentDirectories {false};///< Create missing parent directories before writing.
        bool             bestEffortDurable {true};       ///< Flush file and directory metadata when the platform supports it.
        std::string_view tempPrefix {".ngin-tmp-"};      ///< Prefix used for the sibling temporary file.
    };
}// namespace NGIN::IO
