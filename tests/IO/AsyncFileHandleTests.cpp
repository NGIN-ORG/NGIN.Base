#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/IO/AsyncFileHandle.hpp>

#include <array>
#include <memory>

namespace
{
    struct Statistics final
    {
        int  calls {0};
        int  destroyed {0};
        bool closed {false};
    };

    struct State final
    {
        State(std::shared_ptr<Statistics> stats, NGIN::UIntSize value) : statistics(std::move(stats)), value(value) {}
        ~State() { ++statistics->destroyed; }
        std::shared_ptr<Statistics> statistics;
        NGIN::UIntSize              value;
    };

    NGIN::IO::AsyncTask<NGIN::UIntSize> Data(const std::shared_ptr<void>& rawState,
                                             NGIN::Async::TaskContext& ctx, NGIN::UIntSize kind,
                                             NGIN::UInt64 offset, NGIN::UIntSize size)
    {
        co_await ctx.YieldNow();
        auto state = std::static_pointer_cast<State>(rawState);
        ++state->statistics->calls;
        co_return state->value + kind + offset + size;
    }
    NGIN::IO::AsyncTask<NGIN::UIntSize> Read(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx,
                                             std::span<NGIN::Byte> bytes)
    {
        return Data(state, ctx, 0, 0, bytes.size());
    }
    NGIN::IO::AsyncTask<NGIN::UIntSize> Write(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx,
                                              std::span<const NGIN::Byte> bytes)
    {
        return Data(state, ctx, 1, 0, bytes.size());
    }
    NGIN::IO::AsyncTask<NGIN::UIntSize> ReadAt(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx,
                                               NGIN::UInt64 offset, std::span<NGIN::Byte> bytes)
    {
        return Data(state, ctx, 2, offset, bytes.size());
    }
    NGIN::IO::AsyncTask<NGIN::UIntSize> WriteAt(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx,
                                                NGIN::UInt64 offset, std::span<const NGIN::Byte> bytes)
    {
        return Data(state, ctx, 3, offset, bytes.size());
    }
    NGIN::IO::AsyncTaskVoid Control(const std::shared_ptr<void>& rawState, NGIN::Async::TaskContext& ctx, bool close)
    {
        co_await ctx.YieldNow();
        auto state = std::static_pointer_cast<State>(rawState);
        ++state->statistics->calls;
        state->statistics->closed = close;
    }
    NGIN::IO::AsyncTaskVoid Flush(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx)
    {
        return Control(state, ctx, false);
    }
    NGIN::IO::AsyncTaskVoid Close(const std::shared_ptr<void>& state, NGIN::Async::TaskContext& ctx)
    {
        return Control(state, ctx, true);
    }
    bool IsOpen(const std::shared_ptr<void>& state) noexcept
    {
        return !std::static_pointer_cast<State>(state)->statistics->closed;
    }
    const NGIN::IO::AsyncFileHandle::Operations Operations {
            &Read,
            &Write,
            &ReadAt,
            &WriteAt,
            &Flush,
            &Close,
            &IsOpen,
    };

    NGIN::IO::AsyncTask<NGIN::UIntSize> SelectData(NGIN::IO::AsyncFileHandle& file,
                                                   NGIN::Async::TaskContext& ctx, int kind,
                                                   std::span<NGIN::Byte> bytes)
    {
        switch (kind)
        {
            case 0:
                return file.ReadAsync(ctx, bytes);
            case 1:
                return file.WriteAsync(ctx, bytes);
            case 2:
                return file.ReadAtAsync(ctx, 11, bytes);
            default:
                return file.WriteAtAsync(ctx, 11, bytes);
        }
    }
}// namespace

TEST_CASE("Async file data operations retain their original backend across handle moves", "[IO][Lifetime]")
{
    const int                             kind    = GENERATE(0, 1, 2, 3);
    const bool                            pending = GENERATE(false, true);
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    std::array<NGIN::Byte, 4>             bytes {};
    auto                                  stats = std::make_shared<Statistics>();
    auto                                  state = std::make_shared<State>(stats, 100);
    NGIN::IO::AsyncFileHandle             file(state, &Operations);
    state.reset();
    auto                                                      task = SelectData(file, ctx, kind, bytes);
    NGIN::Async::Operation<NGIN::UIntSize, NGIN::IO::IOError> operation;
    if (pending)
    {
        operation = NGIN::Async::Spawn(ctx, std::move(task));
        REQUIRE(scheduler.RunOne());
    }
    auto moved            = std::move(file);
    auto replacementStats = std::make_shared<Statistics>();
    moved                 = NGIN::IO::AsyncFileHandle(std::make_shared<State>(replacementStats, 200), &Operations);
    REQUIRE_FALSE(file.IsValid());
    REQUIRE(stats->destroyed == 0);
    if (!pending)
        operation = NGIN::Async::Spawn(ctx, std::move(task));
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    auto result = operation.TakeResult();
    REQUIRE(result);
    REQUIRE(result.Value() == 100 + kind + (kind >= 2 ? 11 : 0) + bytes.size());
    REQUIRE(stats->calls == 1);
    REQUIRE(replacementStats->calls == 0);
    operation = {};
    REQUIRE(stats->destroyed == 1);
}

TEST_CASE("Async file control operations retain their backend when the handle is destroyed", "[IO][Lifetime]")
{
    const bool                                      close   = GENERATE(false, true);
    const bool                                      pending = GENERATE(false, true);
    NGIN::Execution::CooperativeScheduler           scheduler;
    NGIN::Async::TaskContext                        ctx(scheduler);
    auto                                            stats = std::make_shared<Statistics>();
    NGIN::IO::AsyncFileHandle                       file(std::make_shared<State>(stats, 100), &Operations);
    auto                                            task = close ? file.CloseAsync(ctx) : file.FlushAsync(ctx);
    NGIN::Async::Operation<void, NGIN::IO::IOError> operation;
    if (pending)
    {
        operation = NGIN::Async::Spawn(ctx, std::move(task));
        REQUIRE(scheduler.RunOne());
    }
    file = {};
    REQUIRE(stats->destroyed == 0);
    if (!pending)
        operation = NGIN::Async::Spawn(ctx, std::move(task));
    scheduler.RunUntilIdle();
    REQUIRE(operation.IsCompleted());
    REQUIRE(operation.TakeResult());
    REQUIRE(stats->calls == 1);
    REQUIRE(stats->closed == close);
    operation = {};
    REQUIRE(stats->destroyed == 1);
}

TEST_CASE("Cold async file calls keep their original invalid binding", "[IO][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    std::array<NGIN::Byte, 1>             bytes {};
    NGIN::IO::AsyncFileHandle             file;
    auto                                  read  = file.ReadAsync(ctx, bytes);
    auto                                  close = file.CloseAsync(ctx);
    auto                                  stats = std::make_shared<Statistics>();
    file                                        = NGIN::IO::AsyncFileHandle(std::make_shared<State>(stats, 100), &Operations);
    auto readOperation                          = NGIN::Async::Spawn(ctx, std::move(read));
    auto closeOperation                         = NGIN::Async::Spawn(ctx, std::move(close));
    scheduler.RunUntilIdle();
    auto readResult  = readOperation.TakeResult();
    auto closeResult = closeOperation.TakeResult();
    REQUIRE(readResult.IsDomainError());
    REQUIRE(closeResult.IsDomainError());
    REQUIRE(readResult.DomainError().code == NGIN::IO::IOErrorCode::InvalidArgument);
    REQUIRE(closeResult.DomainError().code == NGIN::IO::IOErrorCode::InvalidArgument);
    REQUIRE(stats->calls == 0);
    REQUIRE(file.IsOpen());
}

TEST_CASE("Abandoning a cold async file task releases its backend ownership", "[IO][Lifetime]")
{
    NGIN::Execution::CooperativeScheduler scheduler;
    NGIN::Async::TaskContext              ctx(scheduler);
    std::array<NGIN::Byte, 1>             bytes {};
    auto                                  stats = std::make_shared<Statistics>();
    NGIN::IO::AsyncFileHandle             file(std::make_shared<State>(stats, 100), &Operations);
    auto                                  task = file.ReadAsync(ctx, bytes);
    file                                       = {};
    REQUIRE(stats->destroyed == 0);
    task = {};
    REQUIRE(stats->destroyed == 1);
    REQUIRE(stats->calls == 0);
}
