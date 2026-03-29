#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <utility>

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

        enum class ActiveBackend : UInt8
        {
            None,
            WorkerFallback,
        };

        struct Options
        {
            UInt32            workerThreads {1};
            UInt32            queueDepthHint {1024};
            BackendPreference backendPreference {BackendPreference::Auto};
        };

        FileSystemDriver()
            : FileSystemDriver(Options {})
        {
        }

        explicit FileSystemDriver(Options options)
            : m_options(std::move(options))
        {
            if (m_options.backendPreference == BackendPreference::Native)
            {
                m_backend = ActiveBackend::None;
                return;
            }

            m_scheduler = std::make_shared<NGIN::Execution::ThreadPoolScheduler>(
                    static_cast<std::size_t>(m_options.workerThreads == 0 ? 1 : m_options.workerThreads));
            m_backend = ActiveBackend::WorkerFallback;
        }

        [[nodiscard]] const Options& GetOptions() const noexcept
        {
            return m_options;
        }

        [[nodiscard]] ActiveBackend GetActiveBackend() const noexcept
        {
            return m_backend;
        }

        [[nodiscard]] bool HasBackend() const noexcept
        {
            return m_backend != ActiveBackend::None && static_cast<bool>(m_scheduler);
        }

        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() noexcept
        {
            if (!m_scheduler)
            {
                return {};
            }
            return NGIN::Execution::ExecutorRef::From(*m_scheduler);
        }

        [[nodiscard]] NGIN::Async::TaskContext MakeTaskContext(NGIN::Async::CancellationToken cancellation = {}) noexcept
        {
            return NGIN::Async::TaskContext {GetExecutor(), std::move(cancellation)};
        }

        [[nodiscard]] bool RunOne() noexcept
        {
            return m_scheduler ? m_scheduler->RunOne() : false;
        }

        void RunUntilIdle() noexcept
        {
            if (m_scheduler)
            {
                m_scheduler->RunUntilIdle();
            }
        }

        void CancelAll() noexcept
        {
            if (m_scheduler)
            {
                m_scheduler->CancelAll();
            }
        }

    private:
        Options                                          m_options {};
        ActiveBackend                                    m_backend {ActiveBackend::None};
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler {};
    };
}// namespace NGIN::IO
