/// @file ProcessError.hpp
/// @brief Error information returned by child-process operations.
#pragma once

#include <NGIN/IO/Path.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::IO
{
    /// @brief Categorizes failures produced while starting, observing, or terminating a child process.
    enum class ProcessErrorCode : UInt8
    {
        None,           ///< No process error occurred.
        InvalidArgument,///< Process options were internally inconsistent or incomplete.
        StartFailed,    ///< The operating system could not start the child process.
        WaitFailed,     ///< Waiting for child termination failed.
        StreamFailed,   ///< Standard-stream setup or transfer failed.
        AlreadyWaited,  ///< Wait was requested after the process result had already been consumed.
        NotRunning,     ///< The requested operation requires a running child process.
        NotSupported,   ///< The requested behavior is unavailable on the current platform.
    };

    /// @brief Describes a failed child-process operation.
    struct ProcessError
    {
        ProcessErrorCode   code {ProcessErrorCode::None};///< Portable process error category.
        Int32              systemCode {0};               ///< Optional native operating-system error code.
        Path               executable {};                ///< Executable associated with the failure.
        NGIN::Text::String message {};                   ///< Human-readable diagnostic message.
    };

    /// @brief Expected-like result returned by process operations.
    /// @tparam T Successful result type.
    template<typename T>
    using ProcessExpected = NGIN::Utilities::Expected<T, ProcessError>;
}// namespace NGIN::IO
