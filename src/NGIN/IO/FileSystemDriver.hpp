#pragma once

#include "RuntimeBackend.hpp"
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <atomic>
#include <memory>
#include <mutex>

namespace NGIN::IO::detail
{
    class NativeFileBackend;
    class FileSystemDriver;
    std::shared_ptr<FileSystemDriver> AcquireFileSystemDriver(Runtime& runtime);
    NativeFileBackend*                GetNativeFileBackend(FileSystemDriver&) noexcept;
    const NativeFileBackend*          GetNativeFileBackend(const FileSystemDriver&) noexcept;

    // Private file service. Runtime owns initialization; handles retain the
    // backend while native operations are pending.
    class FileSystemDriver final : public RuntimeService
    {
    public:
        using BackendPreference = Runtime::FileBackendPreference;
        using ActiveBackend     = Runtime::FileBackend;
        using Options           = Runtime::FileOptions;
        explicit FileSystemDriver(Runtime& runtime);
        ~FileSystemDriver() override;
        FileSystemDriver(const FileSystemDriver&)               = delete;
        FileSystemDriver&    operator=(const FileSystemDriver&) = delete;
        void                 Stop() noexcept override;
        Runtime::FileBackend GetFileBackend() const noexcept override { return m_backend; }
        ActiveBackend        GetActiveBackend() const noexcept { return m_backend; }
        bool                 HasNativeBackend() const noexcept
        {
            return !IsStopping() && m_nativeBackend != nullptr;
        }
        bool HasBackend() const noexcept
        {
            return !IsStopping() && m_backend != ActiveBackend::None;
        }
        Runtime& GetRuntime() const noexcept { return m_runtime; }
        bool     IsStopping() const noexcept
        {
            return m_stopped.load(std::memory_order_acquire) || m_runtime.GetState() != Runtime::State::Running;
        }
        NGIN::Execution::ExecutorRef GetExecutor() noexcept
        {
            return NGIN::Execution::ExecutorRef::From(*this);
        }

        NGIN::Execution::ScheduleResult Execute(NGIN::Execution::WorkItem item) noexcept;
        NGIN::Execution::ScheduleResult ExecuteAt(NGIN::Execution::WorkItem, NGIN::Time::TimePoint) noexcept
        {
            return std::unexpected(NGIN::Execution::ScheduleError::Rejected);
        }
        bool HasWorkers() const noexcept;

        // Shared by every handle and continuation executor using this service.
        // Separate from backend requests: gate waiters occupy no worker or SQE.
        NGIN::Execution::ScheduleResult AcquireHandleOperation() noexcept;
        void                            ReleaseHandleOperation() noexcept;

    private:
        friend NativeFileBackend*                             GetNativeFileBackend(FileSystemDriver&) noexcept;
        friend const NativeFileBackend*                       GetNativeFileBackend(const FileSystemDriver&) noexcept;
        std::atomic<bool>                                     m_stopped {false};
        Runtime&                                              m_runtime;
        Options                                               m_options;
        std::atomic<UInt32>                                   m_handleOperations {0};
        ActiveBackend                                         m_backend {ActiveBackend::None};
        mutable std::mutex                                    m_workerMutex;
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler;
        std::shared_ptr<NativeFileBackend>                    m_nativeBackend;
    };
}// namespace NGIN::IO::detail
