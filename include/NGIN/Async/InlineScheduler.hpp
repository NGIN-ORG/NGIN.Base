// InlineScheduler.hpp
#pragma once

#include "IScheduler.hpp"
#include <coroutine>
#include <chrono>
#include <thread>

namespace NGIN::Async
{

    /// \brief “Scheduler” that runs coroutines or Jobs inline on the current thread.
    ///
    /// All Schedule()/ScheduleAfter() calls execute immediately (or after blocking sleep),
    /// so Task<T> never needs to special-case inline resumes itself.
    class InlineScheduler : public IScheduler
    {
    public:
        InlineScheduler()           = default;
        ~InlineScheduler() override = default;

        /// Enqueue a coroutine to run immediately → resume inline.
        void Schedule(std::coroutine_handle<> handle) override
        {
            if (handle && !handle.done())
                handle.resume();
        }

        /// Enqueue a generic Job to run immediately → invoke inline.
        void Schedule(Job job) override
        {
            if (job)
                job();
        }

        /// Enqueue a coroutine after a delay → block, then resume inline.
        void ScheduleAfter(std::coroutine_handle<> handle,
                           std::chrono::milliseconds delay) override
        {
            std::this_thread::sleep_for(delay);
            if (handle && !handle.done())
                handle.resume();
        }

        /// Enqueue a Job after a delay → block, then invoke inline.
        void ScheduleAfter(Job job,
                           std::chrono::milliseconds delay) override
        {
            std::this_thread::sleep_for(delay);
            if (job)
                job();
        }
    };

}// namespace NGIN::Async
