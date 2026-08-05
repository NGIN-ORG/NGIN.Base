/// @file ProcessOptions.hpp
/// @brief Child-process launch, environment, stream, cancellation, and timeout policy.
#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/IO/Path.hpp>
#include <NGIN/Primitives.hpp>

#include <chrono>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::IO
{
    /// @brief Selects how a child process obtains or publishes a standard stream.
    enum class ProcessStreamMode : UInt8
    {
        Inherit,///< Inherit the corresponding stream from the parent process.
        Capture,///< Capture child output for ProcessResult or provide managed input.
        Discard,///< Connect the child stream to the platform null device.
        File,   ///< Redirect the child stream to a filesystem path.
    };

    /// @brief Configures one standard stream for a child process.
    struct ProcessStreamOptions
    {
        ProcessStreamMode mode {ProcessStreamMode::Inherit};///< Stream redirection mode.
        Path              file {};                          ///< Redirection path when mode is File.
        bool              append {false};                   ///< Append instead of truncating redirected output files.
    };

    /// @brief Adds, replaces, or removes one child-process environment variable.
    struct ProcessEnvironmentEntry
    {
        std::string                name {}; ///< Environment variable name.
        std::optional<std::string> value {};///< Value to set, or no value to remove the variable.
    };

    /// @brief Describes how to launch and supervise a child process.
    struct ProcessOptions
    {
        Path                                     executable {};                                              ///< Executable path to launch.
        std::vector<std::string>                 arguments {};                                               ///< Arguments excluding the executable name.
        std::optional<Path>                      workingDirectory {};                                        ///< Optional initial working directory.
        bool                                     inheritEnvironment {true};                                  ///< Whether to copy the parent environment.
        std::vector<ProcessEnvironmentEntry>     environment {};                                             ///< Environment overrides applied after inheritance.
        ProcessStreamOptions                     standardInput {};                                           ///< Standard-input policy.
        ProcessStreamOptions                     standardOutput {};                                          ///< Standard-output policy.
        ProcessStreamOptions                     standardError {};                                           ///< Standard-error policy.
        UIntSize                                 maximumOutputBytes {(std::numeric_limits<UIntSize>::max)()};///< Combined capture limit.
        std::optional<std::chrono::milliseconds> timeout {};                                                 ///< Optional execution timeout.
        std::chrono::milliseconds                terminationGracePeriod {250};                               ///< Grace period before forced termination.
        NGIN::Async::CancellationToken           cancellation {};                                            ///< Cancellation token observed while waiting.
        std::function<bool()>                    cancellationProbe {};                                       ///< Optional external cancellation callback.
        std::function<void(std::string_view)>    standardOutputObserver {};                                  ///< Incremental standard-output observer.
        std::function<void(std::string_view)>    standardErrorObserver {};                                   ///< Incremental standard-error observer.
        bool                                     isolateProcessTree {true};                                  ///< Place the child in a terminable process group when supported.
        bool                                     createWindow {false};                                       ///< Allow a visible window on platforms that support it.
    };
}// namespace NGIN::IO
