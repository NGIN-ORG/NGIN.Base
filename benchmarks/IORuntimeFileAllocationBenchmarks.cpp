#include "AllocationInstrumentation.hpp"

#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <memory_resource>
#include <stdexcept>
#include <thread>
#include <vector>

namespace
{
    using namespace NGIN;
    namespace Allocation            = NGIN::Benchmarks::Allocations;
    using Stats                     = Memory::AllocationStats;
    constexpr std::size_t capacity  = 64;
    constexpr std::size_t excess    = 256;
    constexpr std::size_t rounds    = 16;
    constexpr std::size_t producers = 4;

    void Require(bool condition, const char* message)
    {
        if (!condition)
            throw std::runtime_error(message);
    }
    void RequireRetired(const Stats& floor)
    {
        const auto now = Allocation::Snapshot();
        if (now.currentBytes != floor.currentBytes || now.currentCount != floor.currentCount)
        {
            std::cerr << "allocation mismatch floor_bytes=" << floor.currentBytes << " now_bytes=" << now.currentBytes
                      << " floor_count=" << floor.currentCount << " now_count=" << now.currentCount << '\n';
            throw std::runtime_error("file fixture retained tracked storage after shutdown and destruction");
        }
    }
    struct TemporaryFile final
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("ngin-file-budget-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        void Prepare()
        {
            std::ofstream     output(path, std::ios::binary);
            const std::string payload(128 * 1024, 'x');
            output.write(payload.data(), static_cast<std::streamsize>(payload.size()));
            Require(static_cast<bool>(output), "temporary file setup failed");
        }
        ~TemporaryFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };

    template<typename Operation>
    void Wait(IO::Runtime& runtime, Operation& operation)
    {
        while (!operation.IsCompleted())
        {
            runtime.PollOnce();
            std::this_thread::yield();
        }
    }

    struct Pending final
    {
        IO::Runtime&                                               runtime;
        Async::CancellationSource                                  cancellation;
        std::array<Execution::CooperativeScheduler, producers>     schedulers;
        std::array<std::unique_ptr<Async::TaskContext>, producers> contexts;
        std::array<std::array<Byte, 64>, capacity + 1>             buffers {};
        std::vector<Async::Operation<UIntSize, IO::IOError>>       operations;
        explicit Pending(IO::Runtime& owner) : runtime(owner)
        {
            operations.reserve(capacity + 1);
            for (std::size_t index = 0; index != producers; ++index)
                contexts[index] = std::make_unique<Async::TaskContext>(schedulers[index], cancellation.GetToken());
        }
        void CancelAndJoin() noexcept
        {
            cancellation.Cancel();
            while (!std::all_of(operations.begin(), operations.end(), [](const auto& operation) { return operation.IsCompleted(); }))
            {
                runtime.PollOnce();
                for (auto& scheduler: schedulers)
                    scheduler.RunUntilIdle();
                std::this_thread::yield();
            }
        }
        ~Pending()
        {
            CancelAndJoin();
            operations.clear();
            for (auto& scheduler: schedulers)
                scheduler.RunUntilIdle();
        }
    };

    std::size_t RunFixture(const IO::Path& path, IO::Runtime::FileBackendPreference preference)
    {
        IO::Runtime                        runtime({.files = {.queueDepthHint = capacity, .backendPreference = preference}});
        IO::LocalFileSystem                files(runtime);
        Async::TaskContext                 context(runtime.GetExecutor());
        std::array<IO::AsyncFileHandle, 2> handles;
        for (auto& handle: handles)
        {
            auto opening = Async::Spawn(context, files.OpenFileAsync(context, path, {}));
            Wait(runtime, opening);
            auto result = opening.TakeResult();
            Require(result.Succeeded(), "file open failed");
            handle = std::move(result).Value();
        }
        std::size_t retainedGrowth = 0;
        {
            Pending pending(runtime);
            // Drive only selected executors while filling the gate. With no loop
            // delivery, the first read on each handle remains admitted even if
            // the backend has already finished accessing its buffer.
            for (std::size_t index = 0; index != capacity; ++index)
            {
                const std::size_t producer = index % producers;
                auto&             selected = *pending.contexts[producer];
                pending.operations.push_back(Async::Spawn(selected,
                                                          handles[index % handles.size()].ReadAsync(selected, pending.buffers[index])));
                pending.schedulers[producer].RunUntilIdle();
                Require(!pending.operations.back().IsCompleted(), "file handle admission failed before capacity");
            }
            const Stats full = Allocation::Snapshot();
            Stats       rejectionFloor;
            for (std::size_t index = 0; index != excess; ++index)
            {
                const std::size_t producer = index % producers;
                auto&             selected = *pending.contexts[producer];
                pending.operations.push_back(Async::Spawn(selected,
                                                          handles[index % handles.size()].ReadAsync(selected, pending.buffers.back())));
                pending.schedulers[producer].RunUntilIdle();
                auto& rejected = pending.operations.back();
                Require(rejected.IsCompleted(), "file overload admitted an extra read");
                {
                    const auto result = rejected.TakeResult();
                    Require(result.IsFault() && result.Fault().code == Async::AsyncFaultCode::SchedulerDispatchFailed &&
                                    result.Fault().native == static_cast<int>(Execution::ScheduleError::ResourceExhausted),
                            "file overload did not report resource exhaustion");
                }
                pending.operations.pop_back();
                const Stats now = Allocation::Snapshot();
                if (index == 0)
                {
                    rejectionFloor = now;
                    retainedGrowth = std::max(full.currentBytes, now.currentBytes) - full.currentBytes;
                }
                // Workers can still retire existing captures while the loop is
                // parked. Decreases are valid; later rejections must not grow
                // live storage past the first rejection's cancellation-table size.
                Require(now.currentBytes <= rejectionFloor.currentBytes && now.currentCount <= rejectionFloor.currentCount,
                        "repeated rejected file reads grew retained storage");
            }
            pending.CancelAndJoin();
            for (auto& operation: pending.operations)
                Require(operation.TakeResult().IsCanceled(), "accepted file read did not finish cancellation");
        }
        for (auto& handle: handles)
        {
            auto closing = Async::Spawn(context, handle.CloseAsync(context));
            Wait(runtime, closing);
            Require(closing.TakeResult().Succeeded(), "file close failed after budget recovery");
        }
        runtime.Shutdown();
        return retainedGrowth;
    }
}// namespace

int main()
{
    try
    {
        std::cout.setf(std::ios::unitbuf);
        std::cout << "File allocation workload: requested C++ bytes; includes runtime/executor/file initialization and teardown; no latency acceptance data\n"
                  << "capacity=" << capacity << " handles=2 external_cooperative_executors=" << producers
                  << " rounds=" << rounds << " accepted_per_round=" << capacity << " rejected_per_round=" << excess << '\n';
        const Stats check     = Allocation::Snapshot();
        auto*       resource  = std::pmr::new_delete_resource();
        void*       block     = resource->allocate(96, 32);
        const Stats allocated = Allocation::Snapshot();
        resource->deallocate(block, 96, 32);
        Require(allocated.currentBytes == check.currentBytes + 96 && allocated.currentCount == check.currentCount + 1,
                "replacement new does not cover the standard PMR resource");
        RequireRetired(check);
        TemporaryFile temporary;
        temporary.Prepare();
        const IO::Path path(temporary.path.string());
        for (auto preference: {IO::Runtime::FileBackendPreference::Native, IO::Runtime::FileBackendPreference::Fallback})
        {
            RunFixture(path, preference);
            Allocation::ResetPeak();
            const Stats before = Allocation::Snapshot();
            std::size_t growth = 0;
            for (std::size_t round = 0; round != rounds; ++round)
            {
                growth = std::max(growth, RunFixture(path, preference));
                RequireRetired(before);
            }
            const Stats after = Allocation::Snapshot();
            std::cout << (preference == IO::Runtime::FileBackendPreference::Native ? "native" : "fallback")
                      << " operations=" << rounds * (capacity + excess)
                      << " allocations=" << after.totalCount - before.totalCount
                      << " requested_bytes=" << after.totalBytes - before.totalBytes
                      << " peak_live_bytes_above_floor=" << after.peakBytes - before.currentBytes
                      << " first_rejection_retained_bytes_max=" << growth
                      << " retained_bytes_above_floor=0\n";
        }
        return 0;
    } catch (const std::exception& exception)
    {
        std::cerr << "file allocation benchmark failed: " << exception.what() << '\n';
        return 1;
    }
}
