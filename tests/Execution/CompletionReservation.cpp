#include <catch2/catch_test_macros.hpp>

#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/detail/CompletionQueue.hpp>

#include <atomic>
#include <future>
#include <latch>
#include <memory>
#include <thread>

using namespace NGIN::Execution;

TEST_CASE("Completion reservations queue exactly once and recover capacity", "[Execution][Reservation]")
{
    detail::CompletionQueue queue(1);
    int                     calls = 0;
    auto                    first = queue.Reserve(WorkItem([&] { ++calls; }));
    REQUIRE(first);
    auto saturated = queue.Reserve(WorkItem([] {}));
    REQUIRE_FALSE(saturated);
    REQUIRE(saturated.error() == ScheduleError::ResourceExhausted);
    first->Dispatch();
    first->Dispatch();
    REQUIRE(calls == 0);
    REQUIRE(queue.Outstanding() == 1);
    REQUIRE(queue.RunOne());
    REQUIRE_FALSE(queue.RunOne());
    REQUIRE(calls == 1);
    REQUIRE(queue.Outstanding() == 0);
    auto recovered = queue.Reserve(WorkItem([&] { ++calls; }));
    REQUIRE(recovered);
    recovered->Reset();
    REQUIRE(queue.Outstanding() == 0);
    REQUIRE(calls == 1);
}

TEST_CASE("Completion storage is reserved before admission and survives shutdown", "[Execution][Reservation]")
{
    detail::CompletionQueue queue(2);
    auto                    lifetime = std::make_shared<int>(42);
    std::weak_ptr<int>      weak     = lifetime;
    int                     observed = 0;
    auto                    ticket   = queue.Reserve(WorkItem([owner = lifetime, &observed] { observed = *owner; }));
    REQUIRE(ticket);
    lifetime.reset();
    queue.Close();
    auto rejected = queue.Reserve(WorkItem([] {}));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == ScheduleError::Stopped);
    REQUIRE_FALSE(weak.expired());
    ticket->Dispatch();
    REQUIRE(queue.RunOne());
    REQUIRE(observed == 42);
    REQUIRE(weak.expired());
}

TEST_CASE("Completion retirement wakes only a closed queue's final drain", "[Execution][Reservation]")
{
    unsigned                wakes = 0;
    detail::CompletionQueue queue(2, +[](void* state) noexcept { ++*static_cast<unsigned*>(state); }, &wakes);
    auto                    unused = queue.Reserve(WorkItem([] {}));
    REQUIRE(unused);
    unused->Reset();
    REQUIRE(wakes == 0);
    auto published = queue.Reserve(WorkItem([] {}));
    REQUIRE(published);
    published->Dispatch();
    REQUIRE(wakes == 1);
    REQUIRE(queue.RunOne());
    REQUIRE(wakes == 1);
    auto first = queue.Reserve(WorkItem([] {}));
    auto last  = queue.Reserve(WorkItem([] {}));
    REQUIRE(first);
    REQUIRE(last);
    queue.Close();
    REQUIRE(wakes == 2);
    first->Reset();
    REQUIRE(wakes == 2);
    last->Reset();
    REQUIRE(wakes == 3);
    REQUIRE(queue.Outstanding() == 0);
}

TEST_CASE("Completion reservation allocation failure rejects admission without leaking capacity", "[Execution][Reservation]")
{
    class Resource final : public std::pmr::memory_resource
    {
    public:
        bool fail = true;

    private:
        void* do_allocate(std::size_t size, std::size_t alignment) override
        {
            if (fail)
                throw std::bad_alloc();
            return std::pmr::new_delete_resource()->allocate(size, alignment);
        }
        void do_deallocate(void* pointer, std::size_t size, std::size_t alignment) override
        {
            std::pmr::new_delete_resource()->deallocate(pointer, size, alignment);
        }
        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
    } resource;
    detail::CompletionQueue queue(1, nullptr, nullptr, &resource);
    auto                    rejected = queue.Reserve(WorkItem([] {}));
    REQUIRE_FALSE(rejected);
    REQUIRE(rejected.error() == ScheduleError::ResourceExhausted);
    REQUIRE(queue.Outstanding() == 0);
    resource.fail = false;
    auto accepted = queue.Reserve(WorkItem([] {}));
    REQUIRE(accepted);
    resource.fail = true;
    accepted->Dispatch();
    REQUIRE(queue.RunOne());
    REQUIRE(queue.Outstanding() == 0);
}

TEST_CASE("ExecutorRef rejects unsupported completion reservations before execution", "[Execution][Reservation]")
{
    ExecutorRef empty;
    auto        invalid = empty.ReserveCompletion(WorkItem([] {}));
    REQUIRE_FALSE(invalid);
    REQUIRE(invalid.error() == ScheduleError::InvalidExecutor);
    InlineScheduler scheduler;
    auto            unsupported = ExecutorRef::From(scheduler).ReserveCompletion(WorkItem([] {}));
    REQUIRE_FALSE(unsupported);
    REQUIRE(unsupported.error() == ScheduleError::Rejected);
}

TEST_CASE("Reserved completion dispatch preserves cooperative executor context", "[Execution][Reservation]")
{
    CooperativeScheduler scheduler;
    ExecutorRef          executor = ExecutorRef::From(scheduler);
    REQUIRE_FALSE(executor.IsCurrent());
    bool current = false;
    auto ticket  = executor.ReserveCompletion(WorkItem([&] { current = executor.IsCurrent(); }));
    REQUIRE(ticket);
    std::thread producer([delivery = std::move(*ticket)]() mutable { delivery.Dispatch(); });
    producer.join();
    REQUIRE_FALSE(current);
    REQUIRE(scheduler.RunOne());
    REQUIRE(current);
    REQUIRE_FALSE(executor.IsCurrent());
}

TEST_CASE("Thread pool destruction drains reserved delivery on its worker", "[Execution][Reservation]")
{
    auto               scheduler = std::make_unique<ThreadPoolScheduler>(1);
    ExecutorRef        executor  = ExecutorRef::From(*scheduler);
    std::promise<bool> completed;
    auto               outcome = completed.get_future();
    auto               ticket  = executor.ReserveCompletion(WorkItem([&] { completed.set_value(executor.IsCurrent()); }));
    REQUIRE(ticket);
    std::thread shutdown([owner = std::move(scheduler)]() mutable { owner.reset(); });
    // The outstanding ticket keeps the executor's destructor in its drain phase.
    while (executor.Execute(WorkItem([] {})))
        std::this_thread::yield();
    ticket->Dispatch();
    const bool correctExecutor = outcome.get();
    shutdown.join();
    REQUIRE(correctExecutor);
}

TEST_CASE("Thread pool destruction drains an unpublished reservation released off worker", "[Execution][Reservation]")
{
    auto        scheduler = std::make_unique<ThreadPoolScheduler>(1);
    ExecutorRef executor  = ExecutorRef::From(*scheduler);
    bool        invoked   = false;
    auto        ticket    = executor.ReserveCompletion(WorkItem([&] { invoked = true; }));
    REQUIRE(ticket);
    std::thread shutdown([owner = std::move(scheduler)]() mutable { owner.reset(); });
    while (executor.Execute(WorkItem([] {})))
        std::this_thread::yield();
    ticket->Reset();
    shutdown.join();
    REQUIRE_FALSE(invoked);
}

TEST_CASE("An admitted continuation reuses its reservation after admission closes", "[Execution][Reservation]")
{
    detail::CompletionQueue queue(1);
    CompletionReservation   ticket;
    int                     calls    = 0;
    auto                    reserved = queue.Reserve(WorkItem([&] {
        ++calls;
        if (calls < 100)
            ticket.Schedule();
        else
            ticket.Reset();
    }));
    REQUIRE(reserved);
    ticket = std::move(*reserved);
    queue.Close();
    ticket.Schedule();
    for (int expected = 1; expected <= 100; ++expected)
    {
        REQUIRE(queue.RunOne());
        REQUIRE(calls == expected);
        REQUIRE(queue.Outstanding() == (expected < 100 ? 1 : 0));
    }
    REQUIRE_FALSE(queue.RunOne());
}

TEST_CASE("Releasing a reusable continuation preserves its already queued invocation", "[Execution][Reservation]")
{
    detail::CompletionQueue queue(1);
    int                     calls  = 0;
    auto                    ticket = queue.Reserve(WorkItem([&] { ++calls; }));
    REQUIRE(ticket);
    ticket->Schedule();
    ticket->Reset();
    REQUIRE(queue.Outstanding() == 1);
    REQUIRE(queue.RunOne());
    REQUIRE(calls == 1);
    REQUIRE(queue.Outstanding() == 0);
}

TEST_CASE("A foreign completion can reschedule a reservation during its current invocation", "[Execution][Reservation]")
{
    detail::CompletionQueue queue(1);
    CompletionReservation   ticket;
    std::latch              running(1);
    std::latch              published(1);
    int                     calls    = 0;
    auto                    reserved = queue.Reserve(WorkItem([&] {
        ++calls;
        if (calls == 1)
        {
            running.count_down();
            published.wait();
        }
    }));
    REQUIRE(reserved);
    ticket = std::move(*reserved);
    ticket.Schedule();
    std::thread producer([&] {
        running.wait();
        ticket.Schedule();
        published.count_down();
    });
    const bool  first = queue.RunOne();
    producer.join();
    REQUIRE(first);
    REQUIRE(calls == 1);
    REQUIRE(queue.RunOne());
    REQUIRE(calls == 2);
    REQUIRE(queue.Outstanding() == 1);
    ticket.Reset();
    REQUIRE(queue.Outstanding() == 0);
}
