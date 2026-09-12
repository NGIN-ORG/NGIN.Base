#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#if defined(_WIN32)
#include "../../src/NGIN/IO/NativeFileSystemBackend.hpp"
#include "../../src/NGIN/IO/RuntimeLoop.hpp"
#include <NGIN/Async/Cancellation.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <array>
#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace
{
    using namespace NGIN::IO::detail;

    struct Handle final
    {
        ~Handle()
        {
            if (value != INVALID_HANDLE_VALUE && value)
                CloseHandle(value);
        }
        HANDLE value {INVALID_HANDLE_VALUE};
    };

    struct Pipe final
    {
        Pipe()
        {
            static std::atomic<unsigned> sequence {};
            const std::wstring           name = L"\\\\.\\pipe\\ngin-runtime-test-" + std::to_wstring(GetCurrentProcessId()) +
                                      L"-" + std::to_wstring(sequence.fetch_add(1));
            server.value = CreateNamedPipeW(name.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, nullptr);
            if (server.value == INVALID_HANDLE_VALUE)
                throw std::runtime_error("CreateNamedPipeW test fixture failed");
            client.value = CreateFileW(name.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_OVERLAPPED, nullptr);
            if (client.value == INVALID_HANDLE_VALUE)
                throw std::runtime_error("CreateFileW test pipe client failed");
            Handle event;
            event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            if (!event.value)
                throw std::runtime_error("CreateEventW test pipe connection failed");
            OVERLAPPED connection {};
            connection.hEvent = event.value;
            if (!ConnectNamedPipe(server.value, &connection))
            {
                const DWORD error = GetLastError();
                DWORD       ignored {};
                if (error != ERROR_PIPE_CONNECTED &&
                    (error != ERROR_IO_PENDING || !GetOverlappedResult(server.value, &connection, &ignored, TRUE)))
                    throw std::runtime_error("ConnectNamedPipe test fixture failed");
            }
        }
        Handle server, client;
    };

    struct Shutdown final
    {
        NGIN::IO::Runtime& runtime;
        ~Shutdown() { runtime.Shutdown(); }
    };

    struct Result final
    {
        RuntimeLoop*         loop {};
        bool                 done {false};
        bool                 affine {false};
        NativeFileCompletion completion {};
        static void          Complete(void* raw, NativeFileCompletion completion) noexcept
        {
            auto& result      = *static_cast<Result*>(raw);
            result.affine     = result.loop->IsCurrent();
            result.completion = std::move(completion);
            result.done       = true;
        }
    };
}// namespace

TEST_CASE("Windows native file cancellation drains IOCP before releasing pending state", "[IO][Native][IOCP][Cancellation]")
{
    const bool        stopping = GENERATE(false, true);
    Pipe              pipe;
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Native}});
    auto              driver  = AcquireFileSystemDriver(runtime);
    auto*             backend = GetNativeFileBackend(*driver);
    REQUIRE(backend);
    REQUIRE(backend->GetActiveBackend() == NGIN::IO::Runtime::FileBackend::NativeIocp);
    REQUIRE_FALSE(driver->HasWorkers());
    NGIN::Async::CancellationSource cancellation;
    std::array<NGIN::Byte, 16>      buffer {};
    Result                          result {&RuntimeAccess::Loop(runtime)};
    Shutdown                        shutdown {runtime};
    REQUIRE(backend->Submit(NativeFileRequest {
                                    .kind        = NativeFileOperationKind::Read,
                                    .handleValue = reinterpret_cast<std::uintptr_t>(pipe.server.value),
                                    .buffer      = buffer.data(),
                                    .size        = static_cast<NGIN::UInt32>(buffer.size()),
                                    .userData    = &result,
                                    .completion  = &Result::Complete,
                            },
                            cancellation.GetToken()));
    REQUIRE_FALSE(result.done);
    if (!stopping)
    {
        cancellation.Cancel();
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!result.done && std::chrono::steady_clock::now() < deadline)
        {
            runtime.PollOnce();
            std::this_thread::yield();
        }
        REQUIRE(result.done);
        REQUIRE(runtime.GetState() == NGIN::IO::Runtime::State::Running);
    }
    runtime.Shutdown();
    REQUIRE(result.done);
    REQUIRE(result.affine);
    REQUIRE(result.completion.status == NativeFileCompletion::Status::Completed);
    REQUIRE(result.completion.systemCode == ERROR_OPERATION_ABORTED);
    REQUIRE_FALSE(driver->HasWorkers());
}

TEST_CASE("Windows native file reads use the shared loop without starting workers", "[IO][Native][IOCP]")
{
    Pipe              pipe;
    NGIN::IO::Runtime runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Native}});
    auto              driver  = AcquireFileSystemDriver(runtime);
    auto*             backend = GetNativeFileBackend(*driver);
    REQUIRE(backend);
    std::array<NGIN::Byte, 16> buffer {};
    Result                     result {&RuntimeAccess::Loop(runtime)};
    Shutdown                   shutdown {runtime};
    REQUIRE(backend->Submit(NativeFileRequest {
                                    .kind        = NativeFileOperationKind::Read,
                                    .handleValue = reinterpret_cast<std::uintptr_t>(pipe.server.value),
                                    .buffer      = buffer.data(),
                                    .size        = static_cast<NGIN::UInt32>(buffer.size()),
                                    .userData    = &result,
                                    .completion  = &Result::Complete,
                            },
                            {}));
    Handle event;
    event.value = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    REQUIRE(event.value);
    OVERLAPPED write {};
    write.hEvent        = event.value;
    const BOOL accepted = WriteFile(pipe.client.value, "abc", 3, nullptr, &write);
    REQUIRE((accepted || GetLastError() == ERROR_IO_PENDING));
    DWORD transferred {};
    REQUIRE(GetOverlappedResult(pipe.client.value, &write, &transferred, TRUE));
    REQUIRE(transferred == 3);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!result.done && std::chrono::steady_clock::now() < deadline)
    {
        runtime.PollOnce();
        std::this_thread::yield();
    }
    REQUIRE(result.done);
    REQUIRE(result.affine);
    REQUIRE(result.completion.status == NativeFileCompletion::Status::Completed);
    REQUIRE(result.completion.systemCode == 0);
    REQUIRE(result.completion.value == 3);
    REQUIRE(buffer[0] == NGIN::Byte {'a'});
    REQUIRE(buffer[2] == NGIN::Byte {'c'});
    REQUIRE_FALSE(driver->HasWorkers());
    runtime.Shutdown();
}
#else
TEST_CASE("Windows native file IOCP requires Windows", "[IO][Native][IOCP]")
{
    SKIP("Windows IOCP native completion tests");
}
#endif
