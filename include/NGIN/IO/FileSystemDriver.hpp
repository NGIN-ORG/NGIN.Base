#pragma once

#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/IOResult.hpp>

#include <memory>
#include <utility>

namespace NGIN::IO
{
    class FileSystemDriver;

    namespace detail
    {
        class NativeFileBackend;
        NativeFileBackend* GetNativeFileBackend(FileSystemDriver& driver) noexcept;
        const NativeFileBackend* GetNativeFileBackend(const FileSystemDriver& driver) noexcept;
    }

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
            NativeIoUring,
            NativeIocp,
            WorkerFallback,
        };

        struct Options
        {
            UInt32            workerThreads {1};
            UInt32            queueDepthHint {1024};
            BackendPreference backendPreference {BackendPreference::Auto};
        };

        FileSystemDriver();
        explicit FileSystemDriver(Options options);
        ~FileSystemDriver();

        FileSystemDriver(const FileSystemDriver&)            = delete;
        FileSystemDriver& operator=(const FileSystemDriver&) = delete;
        FileSystemDriver(FileSystemDriver&&)                 = delete;
        FileSystemDriver& operator=(FileSystemDriver&&)      = delete;

        [[nodiscard]] const Options& GetOptions() const noexcept
        {
            return m_options;
        }

        [[nodiscard]] ActiveBackend GetActiveBackend() const noexcept
        {
            return m_backend;
        }

        [[nodiscard]] bool HasNativeBackend() const noexcept
        {
            return m_nativeBackend != nullptr;
        }

        [[nodiscard]] bool HasBackend() const noexcept
        {
            return static_cast<bool>(m_scheduler);
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
        friend detail::NativeFileBackend* detail::GetNativeFileBackend(FileSystemDriver&) noexcept;
        friend const detail::NativeFileBackend* detail::GetNativeFileBackend(const FileSystemDriver&) noexcept;

        Options                                                m_options {};
        ActiveBackend                                          m_backend {ActiveBackend::None};
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler {};
        std::unique_ptr<detail::NativeFileBackend>            m_nativeBackend {};
    };
}// namespace NGIN::IO
