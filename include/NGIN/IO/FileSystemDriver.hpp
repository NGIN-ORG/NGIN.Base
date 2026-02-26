#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/IOResult.hpp>

namespace NGIN::IO
{
    class NGIN_BASE_API FileSystemDriver
    {
    public:
        enum class BackendPreference : UInt8
        {
            Auto,
            Native,
            Fallback,
        };

        struct Options
        {
            UInt32            workerThreads {1};
            UInt32            queueDepthHint {1024};
            BackendPreference backendPreference {BackendPreference::Auto};
        };

        explicit FileSystemDriver(const Options& options = {})
            : m_options(options)
            , m_scheduler(static_cast<std::size_t>(options.workerThreads == 0 ? 1 : options.workerThreads))
        {
        }

        [[nodiscard]] const Options& GetOptions() const noexcept
        {
            return m_options;
        }

        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() noexcept
        {
            return NGIN::Execution::ExecutorRef::From(m_scheduler);
        }

        [[nodiscard]] NGIN::Async::TaskContext MakeTaskContext(NGIN::Async::CancellationToken cancellation = {}) noexcept
        {
            return NGIN::Async::TaskContext {GetExecutor(), std::move(cancellation)};
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            return m_scheduler.RunOne();
        }

        void RunUntilIdle() noexcept
        {
            m_scheduler.RunUntilIdle();
        }

        void CancelAll() noexcept
        {
            m_scheduler.CancelAll();
        }

    private:
        Options                            m_options {};
        NGIN::Execution::ThreadPoolScheduler m_scheduler;
    };
}// namespace NGIN::IO

