#include <NGIN/Async/TaskSupervisor.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include "../../src/NGIN/IO/NativeFileDispatch.hpp"
#include "../../src/NGIN/IO/RuntimeBackend.hpp"
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace
{
    struct TemporaryFile
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("ngin-native-loop-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TemporaryFile()
        {
            std::ofstream output(path, std::ios::binary);
            output << std::string(4096, 'x');
            if (!output)
                throw std::runtime_error("native file fixture creation failed");
        }
        ~TemporaryFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };

    template<typename Predicate>
    bool Pump(NGIN::IO::Runtime& runtime, NGIN::Execution::CooperativeScheduler& scheduler, Predicate ready)
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        do
        {
            runtime.PollOnce();
            scheduler.RunUntilIdle();
            if (ready())
                return true;
            std::this_thread::yield();
        } while (std::chrono::steady_clock::now() < deadline);
        return false;
    }
}// namespace

TEST_CASE("Native file completions progress through one-callback runtime batches", "[IO][Runtime][Native]")
{
    TemporaryFile                         temporary;
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files     = {.queueDepthHint = 128, .backendPreference = NGIN::IO::Runtime::FileBackendPreference::Native},
                                                   .batchSize = 1});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::IO::FileOpenOptions             options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string().c_str()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("No native file backend is available on this platform");
    auto opened = opening.TakeResult();
    REQUIRE(opened.Succeeded());
    auto                                                                   file  = std::move(opened.Value());
    constexpr std::size_t                                                  count = 96;
    std::vector<std::vector<NGIN::Byte>>                                   buffers(count, std::vector<NGIN::Byte>(64));
    std::vector<NGIN::Async::Operation<NGIN::UIntSize, NGIN::IO::IOError>> operations;
    operations.reserve(count);
    NGIN::Async::TaskSupervisor<NGIN::IO::IOError> readRoots(context.GetEnvironment(), count);
    for (std::size_t index = 0; index < count; ++index)
        operations.push_back(readRoots.Spawn(file.ReadAtAsync(context, index % 32, buffers[index])).value());
    scheduler.RunUntilIdle();
    REQUIRE(Pump(runtime, scheduler, [&] {
        return std::all_of(operations.begin(), operations.end(), [](const auto& operation) { return operation.IsCompleted(); });
    }));
    REQUIRE(readRoots.TryJoin());
    for (std::size_t index = 0; index < count; ++index)
    {
        auto result = operations[index].TakeResult();
        REQUIRE(result.Succeeded());
        REQUIRE(result.Value() == 64);
        REQUIRE(std::all_of(buffers[index].begin(), buffers[index].end(), [](NGIN::Byte byte) { return byte == NGIN::Byte {'x'}; }));
    }
    auto closingTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(closingTask)::ErrorType> closingRoot(context.GetEnvironment(), 1);

    auto closing = closingRoot.Spawn(std::move(closingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return closing.IsCompleted(); }));
    REQUIRE(closingRoot.TryJoin());
    REQUIRE(closing.TakeResult().Succeeded());
    runtime.Shutdown();
    REQUIRE(runtime.IsStopped());
}

#if defined(__linux__)
namespace
{
    std::size_t OpenDescriptorsFor(const std::filesystem::path& path)
    {
        std::size_t count = 0;
        for (const auto& entry: std::filesystem::directory_iterator("/proc/self/fd"))
        {
            std::error_code error;
            const auto      target = std::filesystem::read_symlink(entry.path(), error);
            if (!error && target == path)
                ++count;
        }
        return count;
    }
}// namespace

TEST_CASE("Async file native ownership survives a cold read and retires with its last owner", "[IO][Runtime][Lifetime]")
{
    if (!std::filesystem::is_directory("/proc/self/fd"))
        SKIP("Descriptor lifetime observation requires procfs");
    const auto                            preference       = GENERATE(NGIN::IO::Runtime::FileBackendPreference::Native,
                                                                      NGIN::IO::Runtime::FileBackendPreference::Fallback);
    const bool                            readAfterRelease = GENERATE(false, true);
    TemporaryFile                         temporary;
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = preference}});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::IO::FileOpenOptions             options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto file = std::move(opened.Value());
    opening   = {};
    REQUIRE(OpenDescriptorsFor(temporary.path) == 1);
    std::array<NGIN::Byte, 32>          buffer {};
    NGIN::IO::AsyncTask<NGIN::UIntSize> read;
    if (readAfterRelease)
        read = file.ReadAtAsync(context, 17, buffer);
    file = {};
    if (readAfterRelease)
    {
        REQUIRE(OpenDescriptorsFor(temporary.path) == 1);
        auto readingTask = std::move(read);

        NGIN::Async::TaskSupervisor<typename decltype(readingTask)::ErrorType> readingRoot(context.GetEnvironment(), 1);

        auto reading = readingRoot.Spawn(std::move(readingTask)).value();
        REQUIRE(Pump(runtime, scheduler, [&] { return reading.IsCompleted(); }));
        REQUIRE(readingRoot.TryJoin());
        auto result = reading.TakeResult();
        REQUIRE(result);
        REQUIRE(result.Value() == buffer.size());
        REQUIRE(std::all_of(buffer.begin(), buffer.end(), [](NGIN::Byte byte) { return byte == NGIN::Byte {'x'}; }));
        reading = {};
    }
    runtime.Shutdown();
    REQUIRE(OpenDescriptorsFor(temporary.path) == 0);
}

TEST_CASE("Canceling delivery of an opened async file releases its unclaimed descriptor", "[IO][Runtime][Lifetime]")
{
    if (!std::filesystem::is_directory("/proc/self/fd"))
        SKIP("Descriptor lifetime observation requires procfs");
    TemporaryFile                         temporary;
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = NGIN::IO::Runtime::FileBackendPreference::Fallback}});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::CancellationSource       source;
    NGIN::Async::TaskContext              context(scheduler, source.GetToken());
    NGIN::IO::FileOpenOptions             options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    scheduler.RunUntilIdle();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (OpenDescriptorsFor(temporary.path) == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool openedBeforeCancellation = OpenDescriptorsFor(temporary.path) == 1;
    source.Cancel();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    REQUIRE(opening.TakeResult().IsStopped());
    opening = {};
    runtime.Shutdown();
    REQUIRE(openedBeforeCancellation);
    REQUIRE(OpenDescriptorsFor(temporary.path) == 0);
}
#endif

TEST_CASE("Async file position ordering and close drain work on native and fallback backends", "[IO][Runtime][Admission]")
{
    const auto    preference = GENERATE(NGIN::IO::Runtime::FileBackendPreference::Native,
                                        NGIN::IO::Runtime::FileBackendPreference::Fallback);
    TemporaryFile temporary;
    {
        std::ofstream(temporary.path, std::ios::binary | std::ios::trunc) << "abcdefg";
    }
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = preference}});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::IO::FileOpenOptions             options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 3> a {}, b {}, c {}, independent {}, late {};
    auto                      firstTask = file.ReadAsync(context, a);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(context.GetEnvironment(), 1);

    auto first = firstRoot.Spawn(std::move(firstTask)).value();
    scheduler.RunUntilIdle();
    auto secondTask = file.ReadAsync(context, b);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(context.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    scheduler.RunUntilIdle();
    auto atTask = file.ReadAtAsync(context, 1, independent);

    NGIN::Async::TaskSupervisor<typename decltype(atTask)::ErrorType> atRoot(context.GetEnvironment(), 1);

    auto at = atRoot.Spawn(std::move(atTask)).value();
    scheduler.RunUntilIdle();
    auto thirdTask = file.ReadAsync(context, c);

    NGIN::Async::TaskSupervisor<typename decltype(thirdTask)::ErrorType> thirdRoot(context.GetEnvironment(), 1);

    auto third = thirdRoot.Spawn(std::move(thirdTask)).value();
    scheduler.RunUntilIdle();
    auto closeTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(closeTask)::ErrorType> closeRoot(context.GetEnvironment(), 1);

    auto close = closeRoot.Spawn(std::move(closeTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(close.IsCompleted());
    REQUIRE_FALSE(file.IsOpen());
#if defined(__linux__)
    REQUIRE(OpenDescriptorsFor(temporary.path) == 1);
#endif
    auto rejectedTask = file.ReadAtAsync(context, 0, late);

    NGIN::Async::TaskSupervisor<typename decltype(rejectedTask)::ErrorType> rejectedRoot(context.GetEnvironment(), 1);

    auto rejected = rejectedRoot.Spawn(std::move(rejectedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(rejectedRoot.TryJoin());
    auto rejection = rejected.TakeResult();
    REQUIRE((rejection.HasError() && rejection.Error().IsDomainError()));
    REQUIRE(rejection.Error().DomainError().code == NGIN::IO::IOErrorCode::Busy);
    REQUIRE(Pump(runtime, scheduler, [&] { return close.IsCompleted(); }));
    REQUIRE(firstRoot.TryJoin());
    REQUIRE(first.TakeResult().Value() == 3);
    REQUIRE(secondRoot.TryJoin());
    REQUIRE(second.TakeResult().Value() == 3);
    REQUIRE(thirdRoot.TryJoin());
    REQUIRE(third.TakeResult().Value() == 1);
    REQUIRE(atRoot.TryJoin());
    REQUIRE(at.TakeResult().Value() == 3);
    REQUIRE(a == std::array<NGIN::Byte, 3> {NGIN::Byte {'a'}, NGIN::Byte {'b'}, NGIN::Byte {'c'}});
    REQUIRE(b == std::array<NGIN::Byte, 3> {NGIN::Byte {'d'}, NGIN::Byte {'e'}, NGIN::Byte {'f'}});
    REQUIRE(c[0] == NGIN::Byte {'g'});
    REQUIRE(independent == std::array<NGIN::Byte, 3> {NGIN::Byte {'b'}, NGIN::Byte {'c'}, NGIN::Byte {'d'}});
    REQUIRE(closeRoot.TryJoin());
    REQUIRE(close.TakeResult());
#if defined(__linux__)
    REQUIRE(OpenDescriptorsFor(temporary.path) == 0);
#endif
    auto repeatedTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(repeatedTask)::ErrorType> repeatedRoot(context.GetEnvironment(), 1);

    auto repeated = repeatedRoot.Spawn(std::move(repeatedTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(repeatedRoot.TryJoin());
    REQUIRE(repeated.TakeResult());
    runtime.Shutdown();
}

TEST_CASE("Async file explicit offsets and access modes are validated consistently", "[IO][Runtime][Admission]")
{
    const auto                            preference = GENERATE(NGIN::IO::Runtime::FileBackendPreference::Native,
                                                                NGIN::IO::Runtime::FileBackendPreference::Fallback);
    const auto                            access     = GENERATE(NGIN::IO::FileAccess::Read, NGIN::IO::FileAccess::Append);
    TemporaryFile                         temporary;
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = preference}});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::IO::FileOpenOptions             options;
    options.access      = access;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 1> buffer {};
    auto                      invalidTask = file.ReadAtAsync(context, std::numeric_limits<NGIN::UInt64>::max(), buffer);

    NGIN::Async::TaskSupervisor<typename decltype(invalidTask)::ErrorType> invalidRoot(context.GetEnvironment(), 1);

    auto invalid     = invalidRoot.Spawn(std::move(invalidTask)).value();
    auto writeAtTask = file.WriteAtAsync(context, 0, buffer);

    NGIN::Async::TaskSupervisor<typename decltype(writeAtTask)::ErrorType> writeAtRoot(context.GetEnvironment(), 1);

    auto writeAt = writeAtRoot.Spawn(std::move(writeAtTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(invalidRoot.TryJoin());
    auto invalidResult = invalid.TakeResult();
    REQUIRE(writeAtRoot.TryJoin());
    auto writeResult = writeAt.TakeResult();
    REQUIRE((invalidResult.HasError() && invalidResult.Error().IsDomainError()));
    REQUIRE(invalidResult.Error().DomainError().code == (access == NGIN::IO::FileAccess::Read
                                                                 ? NGIN::IO::IOErrorCode::InvalidArgument
                                                                 : NGIN::IO::IOErrorCode::NotSupported));
    REQUIRE((writeResult.HasError() && writeResult.Error().IsDomainError()));
    REQUIRE(writeResult.Error().DomainError().code == NGIN::IO::IOErrorCode::NotSupported);
    if (access == NGIN::IO::FileAccess::Read)
    {
        auto sequentialTask = file.ReadAsync(context, buffer);

        NGIN::Async::TaskSupervisor<typename decltype(sequentialTask)::ErrorType> sequentialRoot(context.GetEnvironment(), 1);

        auto sequential = sequentialRoot.Spawn(std::move(sequentialTask)).value();
        REQUIRE(Pump(runtime, scheduler, [&] { return sequential.IsCompleted(); }));
        REQUIRE(sequentialRoot.TryJoin());
        REQUIRE(sequential.TakeResult().Value() == 1);
        REQUIRE(buffer[0] == NGIN::Byte {'x'});
    }
    auto closingTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(closingTask)::ErrorType> closingRoot(context.GetEnvironment(), 1);

    auto closing = closingRoot.Spawn(std::move(closingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return closing.IsCompleted(); }));
    REQUIRE(closingRoot.TryJoin());
    REQUIRE(closing.TakeResult());
    runtime.Shutdown();
}

TEST_CASE("Canceling queued file close reopens admission without closing an active handle", "[IO][Runtime][Admission][Cancellation]")
{
    const auto                            preference = GENERATE(NGIN::IO::Runtime::FileBackendPreference::Native,
                                                                NGIN::IO::Runtime::FileBackendPreference::Fallback);
    TemporaryFile                         temporary;
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = preference}});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::Async::CancellationSource       cancellation;
    NGIN::Async::TaskContext              closeContext(scheduler, cancellation.GetToken());
    NGIN::IO::FileOpenOptions             options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 1> firstBuffer {}, secondBuffer {};
    auto                      firstTask = file.ReadAsync(context, firstBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(context.GetEnvironment(), 1);

    auto first = firstRoot.Spawn(std::move(firstTask)).value();
    scheduler.RunUntilIdle();
    auto closeTask = file.CloseAsync(closeContext);

    NGIN::Async::TaskSupervisor<typename decltype(closeTask)::ErrorType> closeRoot(closeContext.GetEnvironment(), 1);

    auto close = closeRoot.Spawn(std::move(closeTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(file.IsOpen());
    cancellation.Cancel();
    scheduler.RunUntilIdle();
    REQUIRE(closeRoot.TryJoin());
    REQUIRE(close.TakeResult().IsStopped());
    REQUIRE(file.IsOpen());
    REQUIRE_FALSE(first.IsCompleted());
    auto secondTask = file.ReadAtAsync(context, 2, secondBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(context.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(Pump(runtime, scheduler, [&] { return first.IsCompleted() && second.IsCompleted(); }));
    REQUIRE(firstRoot.TryJoin());
    REQUIRE(first.TakeResult().Value() == 1);
    REQUIRE(secondRoot.TryJoin());
    REQUIRE(second.TakeResult().Value() == 1);
    auto closingTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(closingTask)::ErrorType> closingRoot(context.GetEnvironment(), 1);

    auto closing = closingRoot.Spawn(std::move(closingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return closing.IsCompleted(); }));
    REQUIRE(closingRoot.TryJoin());
    REQUIRE(closing.TakeResult());
    runtime.Shutdown();
}

TEST_CASE("Runtime stop cancels queued file leases without admitting new backend work", "[IO][Runtime][Admission][Shutdown]")
{
    const auto                            preference = GENERATE(NGIN::IO::Runtime::FileBackendPreference::Native,
                                                                NGIN::IO::Runtime::FileBackendPreference::Fallback);
    TemporaryFile                         temporary;
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::IO::Runtime                     runtime({.files = {.backendPreference = preference}});
    NGIN::IO::LocalFileSystem             files(runtime);
    NGIN::Async::TaskContext              context(scheduler);
    NGIN::IO::FileOpenOptions             options;
    options.access      = NGIN::IO::FileAccess::Read;
    options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
    auto openingTask    = files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options);

    NGIN::Async::TaskSupervisor<typename decltype(openingTask)::ErrorType> openingRoot(context.GetEnvironment(), 1);

    auto opening = openingRoot.Spawn(std::move(openingTask)).value();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(openingRoot.TryJoin());
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 1> firstBuffer {}, secondBuffer {};
    auto                      firstTask = file.ReadAsync(context, firstBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(firstTask)::ErrorType> firstRoot(context.GetEnvironment(), 1);

    auto first = firstRoot.Spawn(std::move(firstTask)).value();
    scheduler.RunUntilIdle();
    auto secondTask = file.ReadAsync(context, secondBuffer);

    NGIN::Async::TaskSupervisor<typename decltype(secondTask)::ErrorType> secondRoot(context.GetEnvironment(), 1);

    auto second = secondRoot.Spawn(std::move(secondTask)).value();
    scheduler.RunUntilIdle();
    auto closeTask = file.CloseAsync(context);

    NGIN::Async::TaskSupervisor<typename decltype(closeTask)::ErrorType> closeRoot(context.GetEnvironment(), 1);

    auto close = closeRoot.Spawn(std::move(closeTask)).value();
    scheduler.RunUntilIdle();
    runtime.RequestStop();
    REQUIRE(Pump(runtime, scheduler, [&] { return first.IsCompleted() && second.IsCompleted() && close.IsCompleted(); }));
    REQUIRE(firstRoot.TryJoin());
    REQUIRE(first.TakeResult().StopReason() == NGIN::Async::StopReason::RuntimeShutdown);
    REQUIRE(secondRoot.TryJoin());
    REQUIRE(second.TakeResult().StopReason() == NGIN::Async::StopReason::RuntimeShutdown);
    REQUIRE(closeRoot.TryJoin());
    REQUIRE(close.TakeResult().IsStopped());
    REQUIRE(file.IsOpen());
    first  = {};
    second = {};
    close  = {};
    file   = {};
    runtime.Shutdown();
#if defined(__linux__)
    REQUIRE(OpenDescriptorsFor(temporary.path) == 0);
#endif
}

TEST_CASE("Native adapter cancellation after backend completion retires before user delivery", "[IO][Native][Race]")
{
    using namespace NGIN;
    using namespace NGIN::IO::detail;
    struct ControlledBackend final : NativeFileBackend
    {
        NativeFileRequest               pending;
        int                             completions = 0;
        FileSystemDriver::ActiveBackend GetActiveBackend() const noexcept override
        {
            return FileSystemDriver::ActiveBackend::None;// Controlled adapter fixture, not an OS backend.
        }
        bool Submit(NativeFileRequest request, const Async::CancellationToken&) noexcept override
        {
            pending = request;
            return true;
        }
        void Complete()
        {
            *static_cast<int*>(pending.buffer) = 42;
            auto request                       = std::exchange(pending, {});
            ++completions;
            request.completion(request.userData, NativeFileCompletion {NativeFileCompletion::Status::Completed, sizeof(int)});
        }
    } backend;
    IO::Runtime                     runtime;
    FileSystemDriver                driver(runtime);
    Execution::CooperativeScheduler scheduler;
    Async::CancellationSource       source;
    Async::TaskContext              context(scheduler, source.GetToken());
    int                             borrowed = 0;
    int                             resumes  = 0;
    auto                            read     = [&](Async::TaskContext& ctx) -> Async::Task<NativeOperationCompletion> {
        auto result = co_await NativeFileAwaiter(driver, backend, ctx, NativeFileRequest {.buffer = &borrowed, .size = sizeof(borrowed)});
        ++resumes;
        co_return result;
    };
    auto operationTask = read(context);

    NGIN::Async::TaskSupervisor<typename decltype(operationTask)::ErrorType> operationRoot(context.GetEnvironment(), 1);

    auto operation = operationRoot.Spawn(std::move(operationTask)).value();
    scheduler.RunUntilIdle();
    REQUIRE(backend.pending.buffer == &borrowed);
    backend.Complete();
    source.Cancel(Async::StopReason::Deadline);// Backend has ended all buffer access; the common delivery is still queued.
    REQUIRE_FALSE(operation.IsCompleted());
    REQUIRE(Pump(runtime, scheduler, [&] { return operation.IsCompleted(); }));
    REQUIRE(operationRoot.TryJoin());
    auto result = operation.TakeResult();
    REQUIRE(result.Value().status == NativeOperationCompletion::Status::Canceled);
    REQUIRE(result.Value().stopReason == Async::StopReason::Deadline);
    REQUIRE(borrowed == 42);
    REQUIRE(resumes == 1);
    REQUIRE(backend.completions == 1);
    REQUIRE(backend.pending.buffer == nullptr);
    source.Cancel();
    runtime.Shutdown();
    scheduler.RunUntilIdle();
    REQUIRE(resumes == 1);
}
