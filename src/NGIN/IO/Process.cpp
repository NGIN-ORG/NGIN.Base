#include <NGIN/IO/Process.hpp>

#include <NGIN/Primitives.hpp>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <limits>
#include <map>
#include <new>
#include <thread>
#include <utility>

#if defined(NGIN_PLATFORM_WINDOWS)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cwctype>
#else
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;
#endif

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] ProcessError MakeProcessError(
                ProcessErrorCode code,
                Int32            systemCode,
                const Path&      executable,
                std::string_view message)
        {
            ProcessError error;
            error.code       = code;
            error.systemCode = systemCode;
            error.executable = executable;
            error.message    = NGIN::Text::String {message};
            return error;
        }

        template<typename T>
        [[nodiscard]] ProcessExpected<T> UnexpectedProcessError(ProcessError error)
        {
            return ProcessExpected<T> {NGIN::Utilities::Unexpected<ProcessError> {std::move(error)}};
        }

        [[nodiscard]] bool ContainsNull(std::string_view value) noexcept
        {
            return value.find('\0') != std::string_view::npos;
        }

        [[nodiscard]] std::optional<ProcessError> ValidateOptions(const ProcessOptions& options)
        {
            if (options.executable.IsEmpty())
            {
                return MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        0,
                        options.executable,
                        "process executable must not be empty");
            }
            if (ContainsNull(options.executable.View()))
            {
                return MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        0,
                        options.executable,
                        "process executable contains a null byte");
            }
            if (options.workingDirectory && ContainsNull(options.workingDirectory->View()))
            {
                return MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        0,
                        options.executable,
                        "process working directory contains a null byte");
            }
            if (options.timeout && options.timeout->count() < 0)
            {
                return MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        0,
                        options.executable,
                        "process timeout must not be negative");
            }
            if (options.terminationGracePeriod.count() < 0)
            {
                return MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        0,
                        options.executable,
                        "process termination grace period must not be negative");
            }
            if (options.standardInput.mode == ProcessStreamMode::Capture)
            {
                return MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        0,
                        options.executable,
                        "captured standard input is not supported; use inherited, discarded, or file input");
            }

            const auto validateFile = [&](const ProcessStreamOptions& stream, std::string_view name)
                    -> std::optional<ProcessError> {
                if (stream.mode == ProcessStreamMode::File && stream.file.IsEmpty())
                {
                    return MakeProcessError(
                            ProcessErrorCode::InvalidArgument,
                            0,
                            options.executable,
                            std::string(name) + " file path must not be empty");
                }
                if (stream.mode == ProcessStreamMode::File && ContainsNull(stream.file.View()))
                {
                    return MakeProcessError(
                            ProcessErrorCode::InvalidArgument,
                            0,
                            options.executable,
                            std::string(name) + " file path contains a null byte");
                }
                return std::nullopt;
            };

            if (auto error = validateFile(options.standardInput, "standard input"))
            {
                return error;
            }
            if (auto error = validateFile(options.standardOutput, "standard output"))
            {
                return error;
            }
            if (auto error = validateFile(options.standardError, "standard error"))
            {
                return error;
            }

            for (const auto& argument: options.arguments)
            {
                if (ContainsNull(argument))
                {
                    return MakeProcessError(
                            ProcessErrorCode::InvalidArgument,
                            0,
                            options.executable,
                            "process argument contains a null byte");
                }
            }
            for (const auto& entry: options.environment)
            {
                if (entry.name.empty() || entry.name.find('=') != std::string::npos || ContainsNull(entry.name) ||
                    (entry.value && ContainsNull(*entry.value)))
                {
                    return MakeProcessError(
                            ProcessErrorCode::InvalidArgument,
                            0,
                            options.executable,
                            "process environment entry is invalid");
                }
            }
            return std::nullopt;
        }

    }// namespace

    struct Process::Impl final
    {
        ProcessOptions options {};
        bool           waited {false};

#if defined(NGIN_PLATFORM_WINDOWS)
        HANDLE process {nullptr};
        HANDLE job {nullptr};
        HANDLE standardOutputRead {nullptr};
        HANDLE standardErrorRead {nullptr};

        ~Impl()
        {
            if (!waited && process)
            {
                if (job)
                {
                    ::TerminateJobObject(job, 1);
                }
                else
                {
                    ::TerminateProcess(process, 1);
                }
                ::WaitForSingleObject(process, 5000);
            }
            if (standardOutputRead)
            {
                ::CloseHandle(standardOutputRead);
            }
            if (standardErrorRead)
            {
                ::CloseHandle(standardErrorRead);
            }
            if (process)
            {
                ::CloseHandle(process);
            }
            if (job)
            {
                ::CloseHandle(job);
            }
        }

        [[nodiscard]] static std::optional<std::wstring> ToWide(std::string_view value)
        {
            if (value.empty())
            {
                return std::wstring {};
            }
            const auto required = ::MultiByteToWideChar(
                    CP_UTF8,
                    MB_ERR_INVALID_CHARS,
                    value.data(),
                    static_cast<int>(value.size()),
                    nullptr,
                    0);
            if (required <= 0)
            {
                return std::nullopt;
            }
            std::wstring result(static_cast<std::size_t>(required), L'\0');
            if (::MultiByteToWideChar(
                        CP_UTF8,
                        MB_ERR_INVALID_CHARS,
                        value.data(),
                        static_cast<int>(value.size()),
                        result.data(),
                        required) != required)
            {
                return std::nullopt;
            }
            return result;
        }

        [[nodiscard]] static std::optional<std::wstring> NativePath(const Path& path)
        {
            const auto native = path.ToNative();
            return ToWide(native.View());
        }

        [[nodiscard]] static std::wstring QuoteArgument(std::wstring_view value)
        {
            if (value.empty())
            {
                return L"\"\"";
            }
            if (value.find_first_of(L" \t\"") == std::wstring_view::npos)
            {
                return std::wstring {value};
            }

            std::wstring result {L'\"'};
            std::size_t  slashes = 0;
            for (const wchar_t character: value)
            {
                if (character == L'\\')
                {
                    ++slashes;
                    continue;
                }
                if (character == L'\"')
                {
                    result.append(slashes * 2 + 1, L'\\');
                }
                else
                {
                    result.append(slashes, L'\\');
                }
                slashes = 0;
                result.push_back(character);
            }
            result.append(slashes * 2, L'\\');
            result.push_back(L'\"');
            return result;
        }

        [[nodiscard]] static std::optional<std::vector<wchar_t>> BuildEnvironment(const ProcessOptions& options)
        {
            if (options.inheritEnvironment && options.environment.empty())
            {
                return std::nullopt;
            }

            using EnvironmentValue = std::pair<std::wstring, std::wstring>;
            std::map<std::wstring, EnvironmentValue> environment;
            const auto                               normalize = [](std::wstring value) {
                std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character) {
                    return static_cast<wchar_t>(std::towupper(character));
                });
                return value;
            };

            if (options.inheritEnvironment)
            {
                wchar_t* block = ::GetEnvironmentStringsW();
                if (!block)
                {
                    return std::vector<wchar_t> {};
                }
                for (const wchar_t* entry = block; *entry; entry += std::wcslen(entry) + 1)
                {
                    const std::wstring_view text {entry};
                    const auto              separator = text.find(L'=', text.starts_with(L'=') ? 1 : 0);
                    if (separator == std::wstring_view::npos)
                    {
                        continue;
                    }
                    auto name                    = std::wstring {text.substr(0, separator)};
                    auto value                   = std::wstring {text.substr(separator + 1)};
                    environment[normalize(name)] = {std::move(name), std::move(value)};
                }
                ::FreeEnvironmentStringsW(block);
            }

            for (const auto& overrideEntry: options.environment)
            {
                auto name = ToWide(overrideEntry.name);
                if (!name)
                {
                    return std::vector<wchar_t> {};
                }
                const auto key = normalize(*name);
                if (!overrideEntry.value)
                {
                    environment.erase(key);
                    continue;
                }
                auto value = ToWide(*overrideEntry.value);
                if (!value)
                {
                    return std::vector<wchar_t> {};
                }
                environment[key] = {std::move(*name), std::move(*value)};
            }

            std::vector<wchar_t> result;
            for (const auto& [key, entry]: environment)
            {
                (void) key;
                result.insert(result.end(), entry.first.begin(), entry.first.end());
                result.push_back(L'=');
                result.insert(result.end(), entry.second.begin(), entry.second.end());
                result.push_back(L'\0');
            }
            result.push_back(L'\0');
            if (result.size() == 1)
            {
                result.push_back(L'\0');
            }
            return result;
        }

        struct StartupStreams final
        {
            HANDLE              input {nullptr};
            HANDLE              output {nullptr};
            HANDLE              error {nullptr};
            HANDLE              outputRead {nullptr};
            HANDLE              errorRead {nullptr};
            std::vector<HANDLE> ownedChildHandles {};

            StartupStreams() = default;

            StartupStreams(const StartupStreams&)            = delete;
            StartupStreams& operator=(const StartupStreams&) = delete;

            StartupStreams(StartupStreams&& other) noexcept
                : input(std::exchange(other.input, nullptr)), output(std::exchange(other.output, nullptr)), error(std::exchange(other.error, nullptr)), outputRead(std::exchange(other.outputRead, nullptr)), errorRead(std::exchange(other.errorRead, nullptr)), ownedChildHandles(std::move(other.ownedChildHandles))
            {
                other.ownedChildHandles.clear();
            }

            StartupStreams& operator=(StartupStreams&&) = delete;

            ~StartupStreams()
            {
                for (const HANDLE handle: ownedChildHandles)
                {
                    if (handle)
                    {
                        ::CloseHandle(handle);
                    }
                }
                if (outputRead)
                {
                    ::CloseHandle(outputRead);
                }
                if (errorRead)
                {
                    ::CloseHandle(errorRead);
                }
            }
        };

        [[nodiscard]] static ProcessExpected<StartupStreams> OpenStreams(const ProcessOptions& options)
        {
            StartupStreams      streams;
            SECURITY_ATTRIBUTES security {sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};

            const auto openFile = [&](const ProcessStreamOptions& stream, bool input) -> HANDLE {
                const auto nativePath = NativePath(stream.file);
                if (!nativePath)
                {
                    ::SetLastError(ERROR_NO_UNICODE_TRANSLATION);
                    return INVALID_HANDLE_VALUE;
                }
                const DWORD access      = input ? GENERIC_READ : GENERIC_WRITE;
                const DWORD disposition = input ? OPEN_EXISTING : (stream.append ? OPEN_ALWAYS : CREATE_ALWAYS);
                HANDLE      handle      = ::CreateFileW(
                        nativePath->c_str(),
                        access,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        &security,
                        disposition,
                        FILE_ATTRIBUTE_NORMAL,
                        nullptr);
                if (handle != INVALID_HANDLE_VALUE && !input && stream.append)
                {
                    ::SetFilePointer(handle, 0, nullptr, FILE_END);
                }
                return handle;
            };

            const auto openInput = [&]() -> bool {
                switch (options.standardInput.mode)
                {
                    case ProcessStreamMode::Inherit:
                        streams.input = ::GetStdHandle(STD_INPUT_HANDLE);
                        return true;
                    case ProcessStreamMode::Discard:
                        streams.input = ::CreateFileW(
                                L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
                        break;
                    case ProcessStreamMode::File:
                        streams.input = openFile(options.standardInput, true);
                        break;
                    case ProcessStreamMode::Capture:
                        return false;
                }
                if (streams.input == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                streams.ownedChildHandles.push_back(streams.input);
                return true;
            };

            const auto openOutput = [&](const ProcessStreamOptions& stream, DWORD standardHandle, HANDLE& child, HANDLE& read)
                    -> bool {
                switch (stream.mode)
                {
                    case ProcessStreamMode::Inherit:
                        child = ::GetStdHandle(standardHandle);
                        return true;
                    case ProcessStreamMode::Capture:
                        if (!::CreatePipe(&read, &child, &security, 0))
                        {
                            return false;
                        }
                        streams.ownedChildHandles.push_back(child);
                        if (!::SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0))
                        {
                            return false;
                        }
                        return true;
                    case ProcessStreamMode::Discard:
                        child = ::CreateFileW(
                                L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, 0, nullptr);
                        break;
                    case ProcessStreamMode::File:
                        child = openFile(stream, false);
                        break;
                }
                if (child == INVALID_HANDLE_VALUE)
                {
                    return false;
                }
                streams.ownedChildHandles.push_back(child);
                return true;
            };

            if (!openInput() ||
                !openOutput(options.standardOutput, STD_OUTPUT_HANDLE, streams.output, streams.outputRead) ||
                !openOutput(options.standardError, STD_ERROR_HANDLE, streams.error, streams.errorRead))
            {
                return UnexpectedProcessError<StartupStreams>(MakeProcessError(
                        ProcessErrorCode::StreamFailed,
                        static_cast<Int32>(::GetLastError()),
                        options.executable,
                        "failed to configure child process streams"));
            }
            return ProcessExpected<StartupStreams> {std::move(streams)};
        }

        [[nodiscard]] static ProcessExpected<std::unique_ptr<Impl>> Start(ProcessOptions options)
        {
            auto executable = NativePath(options.executable);
            if (!executable)
            {
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        static_cast<Int32>(ERROR_NO_UNICODE_TRANSLATION),
                        options.executable,
                        "process executable is not valid UTF-8"));
            }

            std::wstring commandLine = QuoteArgument(*executable);
            for (const auto& argument: options.arguments)
            {
                auto wideArgument = ToWide(argument);
                if (!wideArgument)
                {
                    return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                            ProcessErrorCode::InvalidArgument,
                            static_cast<Int32>(ERROR_NO_UNICODE_TRANSLATION),
                            options.executable,
                            "process argument is not valid UTF-8"));
                }
                commandLine.push_back(L' ');
                commandLine += QuoteArgument(*wideArgument);
            }
            std::vector<wchar_t> mutableCommand(commandLine.begin(), commandLine.end());
            mutableCommand.push_back(L'\0');

            std::optional<std::wstring> workingDirectory;
            if (options.workingDirectory)
            {
                workingDirectory = NativePath(*options.workingDirectory);
                if (!workingDirectory)
                {
                    return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                            ProcessErrorCode::InvalidArgument,
                            static_cast<Int32>(ERROR_NO_UNICODE_TRANSLATION),
                            options.executable,
                            "process working directory is not valid UTF-8"));
                }
            }

            auto streams = OpenStreams(options);
            if (!streams.HasValue())
            {
                return UnexpectedProcessError<std::unique_ptr<Impl>>(std::move(streams).TakeError());
            }

            auto environment = BuildEnvironment(options);
            if (environment && environment->empty())
            {
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::InvalidArgument,
                        static_cast<Int32>(ERROR_NO_UNICODE_TRANSLATION),
                        options.executable,
                        "process environment is not valid UTF-8"));
            }

            STARTUPINFOW startup {};
            startup.cb         = sizeof(startup);
            startup.dwFlags    = STARTF_USESTDHANDLES;
            startup.hStdInput  = streams.Value().input;
            startup.hStdOutput = streams.Value().output;
            startup.hStdError  = streams.Value().error;

            PROCESS_INFORMATION processInfo {};
            DWORD               flags = CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT;
            if (!options.createWindow)
            {
                flags |= CREATE_NO_WINDOW;
            }
            if (options.isolateProcessTree)
            {
                flags |= CREATE_NEW_PROCESS_GROUP;
            }

            if (!::CreateProcessW(
                        executable->c_str(),
                        mutableCommand.data(),
                        nullptr,
                        nullptr,
                        TRUE,
                        flags,
                        environment ? environment->data() : nullptr,
                        workingDirectory ? workingDirectory->c_str() : nullptr,
                        &startup,
                        &processInfo))
            {
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::StartFailed,
                        static_cast<Int32>(::GetLastError()),
                        options.executable,
                        "failed to create child process"));
            }

            HANDLE job = nullptr;
            if (options.isolateProcessTree)
            {
                job = ::CreateJobObjectW(nullptr, nullptr);
                JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits {};
                limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
                if (!job ||
                    !::SetInformationJobObject(job, JobObjectExtendedLimitInformation, &limits, sizeof(limits)) ||
                    !::AssignProcessToJobObject(job, processInfo.hProcess))
                {
                    const auto systemCode = static_cast<Int32>(::GetLastError());
                    ::TerminateProcess(processInfo.hProcess, 1);
                    ::CloseHandle(processInfo.hThread);
                    ::CloseHandle(processInfo.hProcess);
                    if (job)
                    {
                        ::CloseHandle(job);
                    }
                    return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                            ProcessErrorCode::StartFailed,
                            systemCode,
                            options.executable,
                            "failed to isolate child process tree"));
                }
            }

            if (::ResumeThread(processInfo.hThread) == static_cast<DWORD>(-1))
            {
                const auto systemCode = static_cast<Int32>(::GetLastError());
                if (job)
                {
                    ::TerminateJobObject(job, 1);
                    ::CloseHandle(job);
                }
                else
                {
                    ::TerminateProcess(processInfo.hProcess, 1);
                }
                ::CloseHandle(processInfo.hThread);
                ::CloseHandle(processInfo.hProcess);
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::StartFailed,
                        systemCode,
                        options.executable,
                        "failed to resume child process"));
            }
            ::CloseHandle(processInfo.hThread);

            auto implementation                = std::make_unique<Impl>();
            implementation->options            = std::move(options);
            implementation->process            = processInfo.hProcess;
            implementation->job                = job;
            implementation->standardOutputRead = streams.Value().outputRead;
            implementation->standardErrorRead  = streams.Value().errorRead;
            streams.Value().outputRead         = nullptr;
            streams.Value().errorRead          = nullptr;
            return ProcessExpected<std::unique_ptr<Impl>> {std::move(implementation)};
        }

        void RequestForcedTermination(UInt32 exitCode) noexcept
        {
            if (job)
            {
                ::TerminateJobObject(job, exitCode);
            }
            else if (process)
            {
                ::TerminateProcess(process, exitCode);
            }
        }

        void DrainPipe(
                HANDLE                                       pipe,
                ProcessResult&                               result,
                std::string&                                 target,
                const std::function<void(std::string_view)>& observer,
                bool&                                        terminationRequested) noexcept
        {
            if (!pipe)
            {
                return;
            }
            std::array<char, 4096> buffer {};
            while (true)
            {
                DWORD available = 0;
                if (!::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || available == 0)
                {
                    break;
                }
                DWORD read = 0;
                if (!::ReadFile(
                            pipe,
                            buffer.data(),
                            static_cast<DWORD>((std::min) (buffer.size(), static_cast<std::size_t>(available))),
                            &read,
                            nullptr) ||
                    read == 0)
                {
                    break;
                }

                const auto consumed          = result.standardOutput.size() + result.standardError.size();
                const auto availableCapacity = implementationMaximumOutput() > consumed
                                                       ? implementationMaximumOutput() - consumed
                                                       : UIntSize {0};
                const auto accepted          = (std::min) (availableCapacity, static_cast<UIntSize>(read));
                if (accepted > 0)
                {
                    target.append(buffer.data(), accepted);
                    if (observer)
                    {
                        observer(std::string_view {buffer.data(), accepted});
                    }
                }
                if (accepted != read)
                {
                    result.outputLimitExceeded = true;
                    if (!terminationRequested)
                    {
                        terminationRequested = true;
                        RequestForcedTermination(3);
                    }
                }
            }
        }

        [[nodiscard]] UIntSize implementationMaximumOutput() const noexcept
        {
            return options.maximumOutputBytes;
        }

        [[nodiscard]] ProcessExpected<ProcessResult> Wait()
        {
            if (waited)
            {
                return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                        ProcessErrorCode::AlreadyWaited,
                        0,
                        options.executable,
                        "child process has already been waited"));
            }

            ProcessResult result;
            const auto    started              = std::chrono::steady_clock::now();
            bool          terminationRequested = false;

            while (true)
            {
                DrainPipe(
                        standardOutputRead,
                        result,
                        result.standardOutput,
                        options.standardOutputObserver,
                        terminationRequested);
                DrainPipe(
                        standardErrorRead,
                        result,
                        result.standardError,
                        options.standardErrorObserver,
                        terminationRequested);

                const auto waitResult = ::WaitForSingleObject(process, 10);
                if (waitResult == WAIT_OBJECT_0)
                {
                    break;
                }
                if (waitResult == WAIT_FAILED)
                {
                    return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                            ProcessErrorCode::WaitFailed,
                            static_cast<Int32>(::GetLastError()),
                            options.executable,
                            "failed while waiting for child process"));
                }

                if (!terminationRequested &&
                    (options.cancellation.IsCancellationRequested() ||
                     (options.cancellationProbe && options.cancellationProbe())))
                {
                    result.canceled      = true;
                    terminationRequested = true;
                    RequestForcedTermination(4);
                }
                if (!terminationRequested && options.timeout &&
                    std::chrono::steady_clock::now() - started >= *options.timeout)
                {
                    result.timedOut      = true;
                    terminationRequested = true;
                    RequestForcedTermination(5);
                }
            }

            DrainPipe(
                    standardOutputRead,
                    result,
                    result.standardOutput,
                    options.standardOutputObserver,
                    terminationRequested);
            DrainPipe(
                    standardErrorRead,
                    result,
                    result.standardError,
                    options.standardErrorObserver,
                    terminationRequested);

            DWORD exitCode = 0;
            if (!::GetExitCodeProcess(process, &exitCode))
            {
                return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                        ProcessErrorCode::WaitFailed,
                        static_cast<Int32>(::GetLastError()),
                        options.executable,
                        "failed to query child process exit code"));
            }
            result.exitCode = static_cast<int>(exitCode);
            waited          = true;
            return ProcessExpected<ProcessResult> {std::move(result)};
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            if (!process || waited)
            {
                return false;
            }
            DWORD exitCode = 0;
            return ::GetExitCodeProcess(process, &exitCode) && exitCode == STILL_ACTIVE;
        }

        [[nodiscard]] ProcessExpected<void> Terminate()
        {
            if (!IsRunning())
            {
                return UnexpectedProcessError<void>(MakeProcessError(
                        ProcessErrorCode::NotRunning,
                        0,
                        options.executable,
                        "child process is not running"));
            }
            RequestForcedTermination(1);
            return ProcessExpected<void> {};
        }
#else
        pid_t processId {-1};
        int   standardOutputRead {-1};
        int   standardErrorRead {-1};

        ~Impl()
        {
            if (!waited && processId > 0)
            {
                const auto target = options.isolateProcessTree ? -processId : processId;
                ::kill(target, SIGKILL);
                int status = 0;
                while (::waitpid(processId, &status, 0) < 0 && errno == EINTR)
                {
                }
            }
            if (standardOutputRead >= 0)
            {
                ::close(standardOutputRead);
            }
            if (standardErrorRead >= 0)
            {
                ::close(standardErrorRead);
            }
        }

        struct Environment final
        {
            std::vector<std::string> storage {};
            std::vector<char*>       pointers {};
            std::string              path {};
        };

        [[nodiscard]] static Environment BuildEnvironment(const ProcessOptions& options)
        {
            std::map<std::string, std::string> entries;
            if (options.inheritEnvironment)
            {
                for (char** current = environ; current && *current; ++current)
                {
                    const std::string_view entry {*current};
                    const auto             separator = entry.find('=');
                    if (separator != std::string_view::npos)
                    {
                        entries[std::string {entry.substr(0, separator)}] =
                                std::string {entry.substr(separator + 1)};
                    }
                }
            }
            for (const auto& overrideEntry: options.environment)
            {
                if (overrideEntry.value)
                {
                    entries[overrideEntry.name] = *overrideEntry.value;
                }
                else
                {
                    entries.erase(overrideEntry.name);
                }
            }

            Environment result;
            if (const auto path = entries.find("PATH"); path != entries.end())
            {
                result.path = path->second;
            }
            else
            {
                result.path = "/usr/bin:/bin";
            }
            result.storage.reserve(entries.size());
            for (const auto& [name, value]: entries)
            {
                result.storage.push_back(name + "=" + value);
            }
            result.pointers.reserve(result.storage.size() + 1);
            for (auto& entry: result.storage)
            {
                result.pointers.push_back(entry.data());
            }
            result.pointers.push_back(nullptr);
            return result;
        }

        [[nodiscard]] static std::string ResolveExecutable(std::string executable, std::string_view path)
        {
            if (executable.find('/') != std::string::npos)
            {
                return executable;
            }

            std::size_t begin = 0;
            while (begin <= path.size())
            {
                const auto  end       = path.find(':', begin);
                const auto  directory = path.substr(begin, end == std::string_view::npos ? path.size() - begin : end - begin);
                std::string candidate = directory.empty() ? "." : std::string {directory};
                candidate.push_back('/');
                candidate += executable;
                if (::access(candidate.c_str(), X_OK) == 0)
                {
                    return candidate;
                }
                if (end == std::string_view::npos)
                {
                    break;
                }
                begin = end + 1;
            }
            return executable;
        }

        struct StartupStreams final
        {
            int              input {STDIN_FILENO};
            int              output {STDOUT_FILENO};
            int              error {STDERR_FILENO};
            int              outputRead {-1};
            int              errorRead {-1};
            std::vector<int> ownedChildDescriptors {};

            StartupStreams() = default;

            StartupStreams(const StartupStreams&)            = delete;
            StartupStreams& operator=(const StartupStreams&) = delete;

            StartupStreams(StartupStreams&& other) noexcept
                : input(std::exchange(other.input, -1)), output(std::exchange(other.output, -1)), error(std::exchange(other.error, -1)), outputRead(std::exchange(other.outputRead, -1)), errorRead(std::exchange(other.errorRead, -1)), ownedChildDescriptors(std::move(other.ownedChildDescriptors))
            {
                other.ownedChildDescriptors.clear();
            }

            StartupStreams& operator=(StartupStreams&&) = delete;

            ~StartupStreams()
            {
                for (const int descriptor: ownedChildDescriptors)
                {
                    if (descriptor >= 0)
                    {
                        ::close(descriptor);
                    }
                }
                if (outputRead >= 0)
                {
                    ::close(outputRead);
                }
                if (errorRead >= 0)
                {
                    ::close(errorRead);
                }
            }
        };

        [[nodiscard]] static ProcessExpected<StartupStreams> OpenStreams(const ProcessOptions& options)
        {
            StartupStreams streams;
            const auto     openFile = [&](const ProcessStreamOptions& stream, bool input) {
                const auto native = stream.file.ToNative();
                const int  flags  = input ? O_RDONLY : (O_WRONLY | O_CREAT | (stream.append ? O_APPEND : O_TRUNC));
                return ::open(native.CStr(), flags, static_cast<mode_t>(0666));
            };

            switch (options.standardInput.mode)
            {
                case ProcessStreamMode::Inherit:
                    break;
                case ProcessStreamMode::Discard:
                    streams.input = ::open("/dev/null", O_RDONLY);
                    streams.ownedChildDescriptors.push_back(streams.input);
                    break;
                case ProcessStreamMode::File:
                    streams.input = openFile(options.standardInput, true);
                    streams.ownedChildDescriptors.push_back(streams.input);
                    break;
                case ProcessStreamMode::Capture:
                    break;
            }

            const auto openOutput = [&](const ProcessStreamOptions& stream, int inherited, int& child, int& read) {
                switch (stream.mode)
                {
                    case ProcessStreamMode::Inherit:
                        child = inherited;
                        return true;
                    case ProcessStreamMode::Discard:
                        child = ::open("/dev/null", O_WRONLY);
                        streams.ownedChildDescriptors.push_back(child);
                        return child >= 0;
                    case ProcessStreamMode::File:
                        child = openFile(stream, false);
                        streams.ownedChildDescriptors.push_back(child);
                        return child >= 0;
                    case ProcessStreamMode::Capture: {
                        int descriptors[2] {-1, -1};
                        if (::pipe(descriptors) != 0)
                        {
                            return false;
                        }
                        read  = descriptors[0];
                        child = descriptors[1];
                        streams.ownedChildDescriptors.push_back(child);
                        return true;
                    }
                }
                return false;
            };

            if (streams.input < 0 ||
                !openOutput(options.standardOutput, STDOUT_FILENO, streams.output, streams.outputRead) ||
                !openOutput(options.standardError, STDERR_FILENO, streams.error, streams.errorRead))
            {
                return UnexpectedProcessError<StartupStreams>(MakeProcessError(
                        ProcessErrorCode::StreamFailed,
                        errno,
                        options.executable,
                        "failed to configure child process streams"));
            }
            return ProcessExpected<StartupStreams> {std::move(streams)};
        }

        [[nodiscard]] static ProcessExpected<std::unique_ptr<Impl>> Start(ProcessOptions options)
        {
            auto environment      = BuildEnvironment(options);
            auto executableNative = options.executable.ToNative();
            auto executable       = ResolveExecutable(std::string {executableNative.View()}, environment.path);

            std::vector<std::string> argumentStorage;
            argumentStorage.reserve(options.arguments.size() + 1);
            argumentStorage.push_back(executable);
            argumentStorage.insert(argumentStorage.end(), options.arguments.begin(), options.arguments.end());
            std::vector<char*> arguments;
            arguments.reserve(argumentStorage.size() + 1);
            for (auto& argument: argumentStorage)
            {
                arguments.push_back(argument.data());
            }
            arguments.push_back(nullptr);

            std::optional<std::string> workingDirectory;
            if (options.workingDirectory)
            {
                const auto native = options.workingDirectory->ToNative();
                workingDirectory  = std::string {native.View()};
            }

            auto streams = OpenStreams(options);
            if (!streams.HasValue())
            {
                return UnexpectedProcessError<std::unique_ptr<Impl>>(std::move(streams).TakeError());
            }

            int errorPipe[2] {-1, -1};
            if (::pipe(errorPipe) != 0 || ::fcntl(errorPipe[1], F_SETFD, FD_CLOEXEC) != 0)
            {
                if (errorPipe[0] >= 0)
                {
                    ::close(errorPipe[0]);
                    ::close(errorPipe[1]);
                }
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::StartFailed,
                        errno,
                        options.executable,
                        "failed to create child process error channel"));
            }

            const auto processId = ::fork();
            if (processId < 0)
            {
                const auto systemCode = errno;
                ::close(errorPipe[0]);
                ::close(errorPipe[1]);
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::StartFailed,
                        systemCode,
                        options.executable,
                        "failed to fork child process"));
            }

            if (processId == 0)
            {
                ::close(errorPipe[0]);
                if (options.isolateProcessTree && ::setpgid(0, 0) != 0)
                {
                    const auto childError = errno;
                    (void) ::write(errorPipe[1], &childError, sizeof(childError));
                    std::_Exit(126);
                }
                if (::dup2(streams.Value().input, STDIN_FILENO) < 0 ||
                    ::dup2(streams.Value().output, STDOUT_FILENO) < 0 ||
                    ::dup2(streams.Value().error, STDERR_FILENO) < 0)
                {
                    const auto childError = errno;
                    (void) ::write(errorPipe[1], &childError, sizeof(childError));
                    std::_Exit(126);
                }
                if (streams.Value().outputRead >= 0)
                {
                    ::close(streams.Value().outputRead);
                }
                if (streams.Value().errorRead >= 0)
                {
                    ::close(streams.Value().errorRead);
                }
                for (const int descriptor: streams.Value().ownedChildDescriptors)
                {
                    if (descriptor > STDERR_FILENO)
                    {
                        ::close(descriptor);
                    }
                }
                if (workingDirectory && ::chdir(workingDirectory->c_str()) != 0)
                {
                    const auto childError = errno;
                    (void) ::write(errorPipe[1], &childError, sizeof(childError));
                    std::_Exit(126);
                }
                ::execve(executable.c_str(), arguments.data(), environment.pointers.data());
                const auto childError = errno;
                (void) ::write(errorPipe[1], &childError, sizeof(childError));
                std::_Exit(childError == ENOENT ? 127 : 126);
            }

            ::close(errorPipe[1]);
            int     childError = 0;
            ssize_t errorBytes = 0;
            do
            {
                errorBytes = ::read(errorPipe[0], &childError, sizeof(childError));
            } while (errorBytes < 0 && errno == EINTR);
            ::close(errorPipe[0]);

            if (errorBytes > 0)
            {
                int status = 0;
                while (::waitpid(processId, &status, 0) < 0 && errno == EINTR)
                {
                }
                return UnexpectedProcessError<std::unique_ptr<Impl>>(MakeProcessError(
                        ProcessErrorCode::StartFailed,
                        childError,
                        options.executable,
                        "failed to start child process"));
            }

            if (streams.Value().outputRead >= 0)
            {
                (void) ::fcntl(
                        streams.Value().outputRead,
                        F_SETFL,
                        ::fcntl(streams.Value().outputRead, F_GETFL) | O_NONBLOCK);
            }
            if (streams.Value().errorRead >= 0)
            {
                (void) ::fcntl(
                        streams.Value().errorRead,
                        F_SETFL,
                        ::fcntl(streams.Value().errorRead, F_GETFL) | O_NONBLOCK);
            }

            auto implementation                = std::make_unique<Impl>();
            implementation->options            = std::move(options);
            implementation->processId          = processId;
            implementation->standardOutputRead = streams.Value().outputRead;
            implementation->standardErrorRead  = streams.Value().errorRead;
            streams.Value().outputRead         = -1;
            streams.Value().errorRead          = -1;
            return ProcessExpected<std::unique_ptr<Impl>> {std::move(implementation)};
        }

        void Signal(int signal) noexcept
        {
            if (processId > 0)
            {
                ::kill(options.isolateProcessTree ? -processId : processId, signal);
            }
        }

        void DrainDescriptor(
                int                                          descriptor,
                ProcessResult&                               result,
                std::string&                                 target,
                const std::function<void(std::string_view)>& observer,
                bool&                                        forceKilled) noexcept
        {
            if (descriptor < 0)
            {
                return;
            }
            std::array<char, 4096> buffer {};
            while (true)
            {
                const auto count = ::read(descriptor, buffer.data(), buffer.size());
                if (count > 0)
                {
                    const auto consumed  = result.standardOutput.size() + result.standardError.size();
                    const auto available = options.maximumOutputBytes > consumed
                                                   ? options.maximumOutputBytes - consumed
                                                   : UIntSize {0};
                    const auto accepted  = (std::min) (available, static_cast<UIntSize>(count));
                    if (accepted > 0)
                    {
                        target.append(buffer.data(), accepted);
                        if (observer)
                        {
                            observer(std::string_view {buffer.data(), accepted});
                        }
                    }
                    if (accepted != static_cast<UIntSize>(count))
                    {
                        result.outputLimitExceeded = true;
                        if (!forceKilled)
                        {
                            forceKilled = true;
                            Signal(SIGKILL);
                        }
                    }
                    continue;
                }
                if (count < 0 && errno == EINTR)
                {
                    continue;
                }
                break;
            }
        }

        [[nodiscard]] ProcessExpected<ProcessResult> Wait()
        {
            if (waited)
            {
                return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                        ProcessErrorCode::AlreadyWaited,
                        0,
                        options.executable,
                        "child process has already been waited"));
            }

            ProcessResult                                        result;
            const auto                                           started = std::chrono::steady_clock::now();
            std::optional<std::chrono::steady_clock::time_point> terminationStarted;
            bool                                                 forceKilled = false;
            int                                                  status      = 0;

            while (true)
            {
                DrainDescriptor(
                        standardOutputRead,
                        result,
                        result.standardOutput,
                        options.standardOutputObserver,
                        forceKilled);
                DrainDescriptor(
                        standardErrorRead,
                        result,
                        result.standardError,
                        options.standardErrorObserver,
                        forceKilled);

                const auto waitResult = ::waitpid(processId, &status, WNOHANG);
                if (waitResult == processId)
                {
                    break;
                }
                if (waitResult < 0 && errno != EINTR)
                {
                    return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                            ProcessErrorCode::WaitFailed,
                            errno,
                            options.executable,
                            "failed while waiting for child process"));
                }

                const auto now = std::chrono::steady_clock::now();
                if (!terminationStarted &&
                    (options.cancellation.IsCancellationRequested() ||
                     (options.cancellationProbe && options.cancellationProbe())))
                {
                    result.canceled    = true;
                    terminationStarted = now;
                    Signal(SIGTERM);
                }
                if (!terminationStarted && options.timeout && now - started >= *options.timeout)
                {
                    result.timedOut    = true;
                    terminationStarted = now;
                    Signal(SIGTERM);
                }
                if (terminationStarted && !forceKilled && now - *terminationStarted >= options.terminationGracePeriod)
                {
                    forceKilled = true;
                    Signal(SIGKILL);
                }

                pollfd descriptors[2] {
                        {.fd = standardOutputRead, .events = POLLIN, .revents = 0},
                        {.fd = standardErrorRead, .events = POLLIN, .revents = 0},
                };
                (void) ::poll(descriptors, 2, 10);
            }

            DrainDescriptor(
                    standardOutputRead,
                    result,
                    result.standardOutput,
                    options.standardOutputObserver,
                    forceKilled);
            DrainDescriptor(
                    standardErrorRead,
                    result,
                    result.standardError,
                    options.standardErrorObserver,
                    forceKilled);

            if (WIFEXITED(status))
            {
                result.exitCode = WEXITSTATUS(status);
            }
            else if (WIFSIGNALED(status))
            {
                result.terminationSignal = WTERMSIG(status);
                result.exitCode          = 128 + *result.terminationSignal;
            }
            else
            {
                result.exitCode = 1;
            }
            waited = true;
            return ProcessExpected<ProcessResult> {std::move(result)};
        }

        [[nodiscard]] bool IsRunning() const noexcept
        {
            if (waited || processId <= 0)
            {
                return false;
            }
            return ::kill(processId, 0) == 0 || errno == EPERM;
        }

        [[nodiscard]] ProcessExpected<void> Terminate()
        {
            if (!IsRunning())
            {
                return UnexpectedProcessError<void>(MakeProcessError(
                        ProcessErrorCode::NotRunning,
                        0,
                        options.executable,
                        "child process is not running"));
            }
            if (::kill(options.isolateProcessTree ? -processId : processId, SIGTERM) != 0)
            {
                return UnexpectedProcessError<void>(MakeProcessError(
                        ProcessErrorCode::WaitFailed,
                        errno,
                        options.executable,
                        "failed to terminate child process"));
            }
            return ProcessExpected<void> {};
        }
#endif
    };

    Process::Process() noexcept                           = default;
    Process::~Process()                                   = default;
    Process::Process(Process&& other) noexcept            = default;
    Process& Process::operator=(Process&& other) noexcept = default;

    Process::Process(std::unique_ptr<Impl> implementation) noexcept
        : m_implementation(std::move(implementation))
    {
    }

    ProcessExpected<Process> Process::Start(ProcessOptions options)
    {
        if (auto validationError = ValidateOptions(options))
        {
            return UnexpectedProcessError<Process>(std::move(*validationError));
        }
        const auto executable = options.executable;
        try
        {
            auto implementation = Impl::Start(std::move(options));
            if (!implementation.HasValue())
            {
                return UnexpectedProcessError<Process>(std::move(implementation).TakeError());
            }
            return ProcessExpected<Process> {Process {std::move(implementation).TakeValue()}};
        } catch (const std::bad_alloc&)
        {
            return UnexpectedProcessError<Process>(MakeProcessError(
                    ProcessErrorCode::StartFailed,
                    0,
                    executable,
                    "allocation failed while starting child process"));
        } catch (...)
        {
            return UnexpectedProcessError<Process>(MakeProcessError(
                    ProcessErrorCode::StartFailed,
                    0,
                    executable,
                    "unexpected failure while starting child process"));
        }
    }

    bool Process::IsValid() const noexcept
    {
        return static_cast<bool>(m_implementation);
    }

    bool Process::IsRunning() const noexcept
    {
        return m_implementation && m_implementation->IsRunning();
    }

    ProcessExpected<ProcessResult> Process::Wait()
    {
        if (!m_implementation)
        {
            return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                    ProcessErrorCode::NotRunning,
                    0,
                    {},
                    "process object is empty"));
        }
        try
        {
            return m_implementation->Wait();
        } catch (const std::bad_alloc&)
        {
            return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                    ProcessErrorCode::WaitFailed,
                    0,
                    m_implementation->options.executable,
                    "allocation failed while collecting child process output"));
        } catch (...)
        {
            return UnexpectedProcessError<ProcessResult>(MakeProcessError(
                    ProcessErrorCode::WaitFailed,
                    0,
                    m_implementation->options.executable,
                    "unexpected failure while waiting for child process"));
        }
    }

    ProcessExpected<void> Process::Terminate()
    {
        if (!m_implementation)
        {
            return UnexpectedProcessError<void>(MakeProcessError(
                    ProcessErrorCode::NotRunning,
                    0,
                    {},
                    "process object is empty"));
        }
        try
        {
            return m_implementation->Terminate();
        } catch (...)
        {
            return UnexpectedProcessError<void>(MakeProcessError(
                    ProcessErrorCode::WaitFailed,
                    0,
                    m_implementation->options.executable,
                    "unexpected failure while terminating child process"));
        }
    }

    ProcessExpected<ProcessResult> RunProcess(ProcessOptions options)
    {
        auto process = Process::Start(std::move(options));
        if (!process.HasValue())
        {
            return UnexpectedProcessError<ProcessResult>(std::move(process).TakeError());
        }
        return process.Value().Wait();
    }

    NGIN::Async::Task<ProcessResult, ProcessError>
    RunProcessAsync(NGIN::Async::TaskContext& context, ProcessOptions options)
    {
        const auto contextCancellation = context.GetCancellationToken();
        const auto processCancellation = options.cancellation;
        auto       cancellationProbe   = std::move(options.cancellationProbe);
        options.cancellationProbe      = [contextCancellation,
                                          processCancellation,
                                          cancellationProbe = std::move(cancellationProbe)] {
            return contextCancellation.IsCancellationRequested() ||
                   processCancellation.IsCancellationRequested() ||
                   (cancellationProbe && cancellationProbe());
        };

        co_await context.YieldNow();
        auto result = RunProcess(std::move(options));
        if (!result.HasValue())
        {
            co_return NGIN::Utilities::Unexpected<ProcessError>(std::move(result).TakeError());
        }
        co_return std::move(result).TakeValue();
    }
}// namespace NGIN::IO
