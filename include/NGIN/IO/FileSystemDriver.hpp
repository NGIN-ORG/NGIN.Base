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
        NativeFileBackend*       GetNativeFileBackend(FileSystemDriver& driver) noexcept;
        const NativeFileBackend* GetNativeFileBackend(const FileSystemDriver& driver) noexcept;
    }// namespace detail

    /// @brief Owns the scheduler and optional native backend used for asynchronous filesystem operations.
    class NGIN_IO_API FileSystemDriver
    {
    public:
        /// @brief Backend-selection policy used during driver construction.
        enum class BackendPreference : UInt8
        {
            Auto,
            Native,
            Fallback,
        };

        /// @brief Backend selected and active for this driver.
        enum class ActiveBackend : UInt8
        {
            None,
            NativeIoUring,
            NativeIocp,
            WorkerFallback,
        };

        /// @brief Construction options for worker and native backend capacity.
        struct Options
        {
            UInt32            workerThreads {1};
            UInt32            queueDepthHint {1024};
            BackendPreference backendPreference {BackendPreference::Auto};
        };

        /// @brief Constructs a driver using default options.
        FileSystemDriver();
        /// @brief Constructs a driver using the requested backend policy and capacity.
        explicit FileSystemDriver(Options options);
        /// @brief Cancels outstanding driver work and releases backend resources.
        ~FileSystemDriver();

        /// @brief Drivers are non-copyable because they own scheduler and platform resources.
        FileSystemDriver(const FileSystemDriver&) = delete;
        /// @brief Drivers are non-copy-assignable because they own scheduler and platform resources.
        FileSystemDriver& operator=(const FileSystemDriver&) = delete;
        /// @brief Drivers are immovable because backend state retains their address.
        FileSystemDriver(FileSystemDriver&&) = delete;
        /// @brief Drivers are non-move-assignable because backend state retains their address.
        FileSystemDriver& operator=(FileSystemDriver&&) = delete;

        /// @brief Returns the options used to construct this driver.
        [[nodiscard]] const Options& GetOptions() const noexcept
        {
            return m_options;
        }

        /// @brief Returns the backend selected during construction.
        [[nodiscard]] ActiveBackend GetActiveBackend() const noexcept
        {
            return m_backend;
        }

        /// @brief Returns whether a native asynchronous backend is active.
        [[nodiscard]] bool HasNativeBackend() const noexcept
        {
            return m_nativeBackend != nullptr;
        }

        /// @brief Returns whether the driver has a scheduler capable of accepting work.
        [[nodiscard]] bool HasBackend() const noexcept
        {
            return static_cast<bool>(m_scheduler);
        }

        /// @brief Returns a non-owning executor reference to the driver scheduler.
        /// @note The reference is valid only while this driver remains alive.
        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() noexcept
        {
            if (!m_scheduler)
            {
                return {};
            }
            return NGIN::Execution::ExecutorRef::From(*m_scheduler);
        }

        /// @brief Creates a task context bound to this driver and an optional cancellation token.
        [[nodiscard]] NGIN::Async::TaskContext MakeTaskContext(NGIN::Async::CancellationToken cancellation = {}) noexcept
        {
            return NGIN::Async::TaskContext {GetExecutor(), std::move(cancellation)};
        }

        /// @brief Executes one queued cooperative item when available.
        [[nodiscard]] bool RunOne() noexcept
        {
            return m_scheduler ? m_scheduler->RunOne() : false;
        }

        /// @brief Executes queued cooperative work until no immediately runnable item remains.
        void RunUntilIdle() noexcept
        {
            if (m_scheduler)
            {
                m_scheduler->RunUntilIdle();
            }
        }

        /// @brief Requests cancellation for all work owned by the driver scheduler.
        void CancelAll() noexcept
        {
            if (m_scheduler)
            {
                m_scheduler->CancelAll();
            }
        }

    private:
        friend detail::NativeFileBackend*       detail::GetNativeFileBackend(FileSystemDriver&) noexcept;
        friend const detail::NativeFileBackend* detail::GetNativeFileBackend(const FileSystemDriver&) noexcept;

        Options                                               m_options {};
        ActiveBackend                                         m_backend {ActiveBackend::None};
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler {};
        std::unique_ptr<detail::NativeFileBackend>            m_nativeBackend {};
    };
}// namespace NGIN::IO
