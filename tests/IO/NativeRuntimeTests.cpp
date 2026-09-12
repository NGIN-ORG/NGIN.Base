#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string().c_str()), options));
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("No native file backend is available on this platform");
    auto opened = opening.TakeResult();
    REQUIRE(opened.Succeeded());
    auto                                                                   file  = std::move(opened.Value());
    constexpr std::size_t                                                  count = 96;
    std::vector<std::vector<NGIN::Byte>>                                   buffers(count, std::vector<NGIN::Byte>(64));
    std::vector<NGIN::Async::Operation<NGIN::UIntSize, NGIN::IO::IOError>> operations;
    operations.reserve(count);
    for (std::size_t index = 0; index < count; ++index)
        operations.push_back(NGIN::Async::Spawn(context, file.ReadAtAsync(context, index % 32, buffers[index])));
    scheduler.RunUntilIdle();
    REQUIRE(Pump(runtime, scheduler, [&] {
        return std::all_of(operations.begin(), operations.end(), [](const auto& operation) { return operation.IsCompleted(); });
    }));
    for (std::size_t index = 0; index < count; ++index)
    {
        auto result = operations[index].TakeResult();
        REQUIRE(result.Succeeded());
        REQUIRE(result.Value() == 64);
        REQUIRE(std::all_of(buffers[index].begin(), buffers[index].end(), [](NGIN::Byte byte) { return byte == NGIN::Byte {'x'}; }));
    }
    auto closing = NGIN::Async::Spawn(context, file.CloseAsync(context));
    REQUIRE(Pump(runtime, scheduler, [&] { return closing.IsCompleted(); }));
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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options));
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
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
        auto reading = NGIN::Async::Spawn(context, std::move(read));
        REQUIRE(Pump(runtime, scheduler, [&] { return reading.IsCompleted(); }));
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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options));
    scheduler.RunUntilIdle();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (OpenDescriptorsFor(temporary.path) == 0 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool openedBeforeCancellation = OpenDescriptorsFor(temporary.path) == 1;
    source.Cancel();
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    REQUIRE(opening.TakeResult().IsCanceled());
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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options));
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 3> a {}, b {}, c {}, independent {}, late {};
    auto                      first = NGIN::Async::Spawn(context, file.ReadAsync(context, a));
    scheduler.RunUntilIdle();
    auto second = NGIN::Async::Spawn(context, file.ReadAsync(context, b));
    scheduler.RunUntilIdle();
    auto at = NGIN::Async::Spawn(context, file.ReadAtAsync(context, 1, independent));
    scheduler.RunUntilIdle();
    auto third = NGIN::Async::Spawn(context, file.ReadAsync(context, c));
    scheduler.RunUntilIdle();
    auto close = NGIN::Async::Spawn(context, file.CloseAsync(context));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(close.IsCompleted());
    REQUIRE_FALSE(file.IsOpen());
#if defined(__linux__)
    REQUIRE(OpenDescriptorsFor(temporary.path) == 1);
#endif
    auto rejected = NGIN::Async::Spawn(context, file.ReadAtAsync(context, 0, late));
    scheduler.RunUntilIdle();
    auto rejection = rejected.TakeResult();
    REQUIRE(rejection.IsDomainError());
    REQUIRE(rejection.DomainError().code == NGIN::IO::IOErrorCode::Busy);
    REQUIRE(Pump(runtime, scheduler, [&] { return close.IsCompleted(); }));
    REQUIRE(first.TakeResult().Value() == 3);
    REQUIRE(second.TakeResult().Value() == 3);
    REQUIRE(third.TakeResult().Value() == 1);
    REQUIRE(at.TakeResult().Value() == 3);
    REQUIRE(a == std::array<NGIN::Byte, 3> {NGIN::Byte {'a'}, NGIN::Byte {'b'}, NGIN::Byte {'c'}});
    REQUIRE(b == std::array<NGIN::Byte, 3> {NGIN::Byte {'d'}, NGIN::Byte {'e'}, NGIN::Byte {'f'}});
    REQUIRE(c[0] == NGIN::Byte {'g'});
    REQUIRE(independent == std::array<NGIN::Byte, 3> {NGIN::Byte {'b'}, NGIN::Byte {'c'}, NGIN::Byte {'d'}});
    REQUIRE(close.TakeResult());
#if defined(__linux__)
    REQUIRE(OpenDescriptorsFor(temporary.path) == 0);
#endif
    auto repeated = NGIN::Async::Spawn(context, file.CloseAsync(context));
    scheduler.RunUntilIdle();
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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options));
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 1> buffer {};
    auto                      invalid = NGIN::Async::Spawn(context, file.ReadAtAsync(context, std::numeric_limits<NGIN::UInt64>::max(), buffer));
    auto                      writeAt = NGIN::Async::Spawn(context, file.WriteAtAsync(context, 0, buffer));
    scheduler.RunUntilIdle();
    auto invalidResult = invalid.TakeResult();
    auto writeResult   = writeAt.TakeResult();
    REQUIRE(invalidResult.IsDomainError());
    REQUIRE(invalidResult.DomainError().code == (access == NGIN::IO::FileAccess::Read
                                                         ? NGIN::IO::IOErrorCode::InvalidArgument
                                                         : NGIN::IO::IOErrorCode::NotSupported));
    REQUIRE(writeResult.IsDomainError());
    REQUIRE(writeResult.DomainError().code == NGIN::IO::IOErrorCode::NotSupported);
    if (access == NGIN::IO::FileAccess::Read)
    {
        auto sequential = NGIN::Async::Spawn(context, file.ReadAsync(context, buffer));
        REQUIRE(Pump(runtime, scheduler, [&] { return sequential.IsCompleted(); }));
        REQUIRE(sequential.TakeResult().Value() == 1);
        REQUIRE(buffer[0] == NGIN::Byte {'x'});
    }
    auto closing = NGIN::Async::Spawn(context, file.CloseAsync(context));
    REQUIRE(Pump(runtime, scheduler, [&] { return closing.IsCompleted(); }));
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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options));
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 1> firstBuffer {}, secondBuffer {};
    auto                      first = NGIN::Async::Spawn(context, file.ReadAsync(context, firstBuffer));
    scheduler.RunUntilIdle();
    auto close = NGIN::Async::Spawn(closeContext, file.CloseAsync(closeContext));
    scheduler.RunUntilIdle();
    REQUIRE_FALSE(file.IsOpen());
    cancellation.Cancel();
    scheduler.RunUntilIdle();
    REQUIRE(close.TakeResult().IsCanceled());
    REQUIRE(file.IsOpen());
    REQUIRE_FALSE(first.IsCompleted());
    auto second = NGIN::Async::Spawn(context, file.ReadAtAsync(context, 2, secondBuffer));
    scheduler.RunUntilIdle();
    REQUIRE(Pump(runtime, scheduler, [&] { return first.IsCompleted() && second.IsCompleted(); }));
    REQUIRE(first.TakeResult().Value() == 1);
    REQUIRE(second.TakeResult().Value() == 1);
    auto closing = NGIN::Async::Spawn(context, file.CloseAsync(context));
    REQUIRE(Pump(runtime, scheduler, [&] { return closing.IsCompleted(); }));
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
    auto opening        = NGIN::Async::Spawn(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string()), options));
    REQUIRE(Pump(runtime, scheduler, [&] { return opening.IsCompleted(); }));
    if (runtime.GetFileBackend() == NGIN::IO::Runtime::FileBackend::None)
        SKIP("Requested native file backend is unavailable");
    auto opened = opening.TakeResult();
    REQUIRE(opened);
    auto                      file = std::move(opened.Value());
    std::array<NGIN::Byte, 1> firstBuffer {}, secondBuffer {};
    auto                      first = NGIN::Async::Spawn(context, file.ReadAsync(context, firstBuffer));
    scheduler.RunUntilIdle();
    auto second = NGIN::Async::Spawn(context, file.ReadAsync(context, secondBuffer));
    scheduler.RunUntilIdle();
    auto close = NGIN::Async::Spawn(context, file.CloseAsync(context));
    scheduler.RunUntilIdle();
    runtime.RequestStop();
    REQUIRE(Pump(runtime, scheduler, [&] { return first.IsCompleted() && second.IsCompleted() && close.IsCompleted(); }));
    REQUIRE(first.TakeResult().IsCanceled());
    REQUIRE(second.TakeResult().IsCanceled());
    REQUIRE(close.TakeResult().IsCanceled());
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
