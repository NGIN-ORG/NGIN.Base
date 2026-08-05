/// @file Process.hpp
/// @brief Cross-platform child-process execution with explicit IO and lifetime policy.
#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/IO/Path.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Utilities/Expected.hpp>

#include <chrono>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace NGIN::IO
{
    enum class ProcessErrorCode : UInt8
    {
        None,
        InvalidArgument,
        StartFailed,
        WaitFailed,
        StreamFailed,
        AlreadyWaited,
        NotRunning,
        NotSupported,
    };

    struct ProcessError
    {
        ProcessErrorCode   code {ProcessErrorCode::None};
        Int32              systemCode {0};
        Path               executable {};
        NGIN::Text::String message {};
    };

    template<typename T>
    using ProcessExpected = NGIN::Utilities::Expected<T, ProcessError>;

    enum class ProcessStreamMode : UInt8
    {
        Inherit,
        Capture,
        Discard,
        File,
    };

    struct ProcessStreamOptions
    {
        ProcessStreamMode mode {ProcessStreamMode::Inherit};
        Path              file {};
        bool              append {false};
    };

    struct ProcessEnvironmentEntry
    {
        std::string                name {};
        std::optional<std::string> value {};
    };

    struct ProcessOptions
    {
        Path                                     executable {};
        std::vector<std::string>                 arguments {};
        std::optional<Path>                      workingDirectory {};
        bool                                     inheritEnvironment {true};
        std::vector<ProcessEnvironmentEntry>     environment {};
        ProcessStreamOptions                     standardInput {};
        ProcessStreamOptions                     standardOutput {};
        ProcessStreamOptions                     standardError {};
        UIntSize                                 maximumOutputBytes {(std::numeric_limits<UIntSize>::max)()};
        std::optional<std::chrono::milliseconds> timeout {};
        std::chrono::milliseconds                terminationGracePeriod {250};
        NGIN::Async::CancellationToken           cancellation {};
        std::function<bool()>                    cancellationProbe {};
        std::function<void(std::string_view)>    standardOutputObserver {};
        std::function<void(std::string_view)>    standardErrorObserver {};
        bool                                     isolateProcessTree {true};
        bool                                     createWindow {false};
    };

    struct ProcessResult
    {
        int                exitCode {0};
        std::optional<int> terminationSignal {};
        std::string        standardOutput {};
        std::string        standardError {};
        bool               timedOut {false};
        bool               canceled {false};
        bool               outputLimitExceeded {false};
    };

    class NGIN_IO_API Process final
    {
    public:
        Process() noexcept;
        ~Process();

        Process(Process&& other) noexcept;
        Process& operator=(Process&& other) noexcept;

        Process(const Process&)            = delete;
        Process& operator=(const Process&) = delete;

        [[nodiscard]] static ProcessExpected<Process> Start(ProcessOptions options);

        [[nodiscard]] bool                           IsValid() const noexcept;
        [[nodiscard]] bool                           IsRunning() const noexcept;
        [[nodiscard]] ProcessExpected<ProcessResult> Wait();
        [[nodiscard]] ProcessExpected<void>          Terminate();

    private:
        struct Impl;
        explicit Process(std::unique_ptr<Impl> implementation) noexcept;

        std::unique_ptr<Impl> m_implementation {};
    };

    [[nodiscard]] NGIN_IO_API ProcessExpected<ProcessResult> RunProcess(ProcessOptions options);
    [[nodiscard]] NGIN_IO_API                                NGIN::Async::Task<ProcessResult, ProcessError>
                                                             RunProcessAsync(NGIN::Async::TaskContext& context, ProcessOptions options);
}// namespace NGIN::IO
