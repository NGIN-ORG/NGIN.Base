/// @file ProcessResult.hpp
/// @brief Exit status and captured output from a completed child process.
#pragma once

#include <optional>
#include <string>

namespace NGIN::IO
{
    /// @brief Describes the terminal state and captured streams of a child process.
    struct ProcessResult
    {
        int                exitCode {0};               ///< Platform-normalized process exit code.
        std::optional<int> terminationSignal {};       ///< POSIX signal that terminated the child, when applicable.
        std::string        standardOutput {};          ///< Captured standard output.
        std::string        standardError {};           ///< Captured standard error.
        bool               timedOut {false};           ///< Whether timeout policy initiated termination.
        bool               canceled {false};           ///< Whether cancellation policy initiated termination.
        bool               outputLimitExceeded {false};///< Whether capture stopped after reaching the configured limit.
    };
}// namespace NGIN::IO
