#pragma once

#include <NGIN/Text/String.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>

namespace NGIN::IO
{
    using NGIN::Text::String;

    /// @brief Error codes for low-level IO operations.
    enum class IOErrorCode : UInt8
    {
        None,
        EndOfStream,
        InvalidArgument,
        SystemError,
        NotSupported,
    };

    /// @brief IO error payload with optional system code.
    struct IOError
    {
        IOErrorCode code {IOErrorCode::None};
        Int32       systemCode {0};
        String      message {};
    };
}// namespace NGIN::IO
