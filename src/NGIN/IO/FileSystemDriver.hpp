#pragma once

#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Runtime.hpp>
#include <atomic>
#include <memory>

namespace NGIN::IO::detail
{
    class NativeFileBackend;
    class FileSystemDriver;
    NativeFileBackend*       GetNativeFileBackend(FileSystemDriver&) noexcept;
    const NativeFileBackend* GetNativeFileBackend(const FileSystemDriver&) noexcept;

    // Private file service. Runtime owns initialization; handles retain the
    // backend while native operations are pending.
    class FileSystemDriver final
    {
    public:
        using BackendPreference = Runtime::FileBackendPreference;
        using ActiveBackend     = Runtime::FileBackend;
        using Options           = Runtime::FileOptions;
        explicit FileSystemDriver(Options options);
        ~FileSystemDriver();
        FileSystemDriver(const FileSystemDriver&)            = delete;
        FileSystemDriver& operator=(const FileSystemDriver&) = delete;
        void              Stop() noexcept { m_stopped.store(true, std::memory_order_release); }
        ActiveBackend     GetActiveBackend() const noexcept { return m_backend; }
        bool              HasNativeBackend() const noexcept
        {
            return !m_stopped.load(std::memory_order_acquire) && m_nativeBackend != nullptr;
        }
        bool HasBackend() const noexcept
        {
            return !m_stopped.load(std::memory_order_acquire) && m_backend != ActiveBackend::None;
        }
        NGIN::Execution::ExecutorRef GetExecutor() noexcept
        {
            return NGIN::Execution::ExecutorRef::From(*m_scheduler);
        }

    private:
        friend NativeFileBackend*                             GetNativeFileBackend(FileSystemDriver&) noexcept;
        friend const NativeFileBackend*                       GetNativeFileBackend(const FileSystemDriver&) noexcept;
        std::atomic<bool>                                     m_stopped {false};
        Options                                               m_options;
        ActiveBackend                                         m_backend {ActiveBackend::None};
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler;
        std::unique_ptr<NativeFileBackend>                    m_nativeBackend;
    };
}// namespace NGIN::IO::detail
