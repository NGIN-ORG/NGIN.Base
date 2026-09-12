#include "FileSystemDriver.hpp"

#include "NativeFileSystemBackend.hpp"
#include <cassert>

namespace NGIN::IO::detail
{
    NativeFileBackend* GetNativeFileBackend(FileSystemDriver& driver) noexcept
    {
        return driver.HasNativeBackend() ? driver.m_nativeBackend.get() : nullptr;
    }

    const NativeFileBackend* GetNativeFileBackend(const FileSystemDriver& driver) noexcept
    {
        return driver.HasNativeBackend() ? driver.m_nativeBackend.get() : nullptr;
    }
}// namespace NGIN::IO::detail

namespace NGIN::IO::detail
{
    std::shared_ptr<FileSystemDriver> AcquireFileSystemDriver(Runtime& runtime)
    {
        return std::static_pointer_cast<FileSystemDriver>(RuntimeAccess::Acquire(
                runtime, RuntimeServiceKind::Files, +[](Runtime& owner) -> std::shared_ptr<RuntimeService> {
                    return std::make_shared<FileSystemDriver>(owner);
                }));
    }

    FileSystemDriver::FileSystemDriver(Runtime& runtime)
        : m_runtime(runtime), m_options(runtime.GetOptions().files)
    {
        if (m_options.backendPreference != BackendPreference::Fallback)
        {
            m_nativeBackend = detail::CreateNativeFileBackend(*this);
            if (m_nativeBackend != nullptr)
            {
                m_backend = m_nativeBackend->GetActiveBackend();
                return;
            }
            if (m_options.backendPreference == BackendPreference::Native)
            {
                m_backend = ActiveBackend::None;
                return;
            }
        }

        m_backend = ActiveBackend::WorkerFallback;
    }

    FileSystemDriver::~FileSystemDriver() = default;

    void FileSystemDriver::Stop() noexcept
    {
        std::lock_guard lock(m_workerMutex);
        m_stopped.store(true, std::memory_order_release);
    }

    bool FileSystemDriver::HasWorkers() const noexcept
    {
        std::lock_guard lock(m_workerMutex);
        return m_scheduler != nullptr;
    }

    NGIN::Execution::ScheduleResult FileSystemDriver::AcquireHandleOperation() noexcept
    {
        if (IsStopping())
            return std::unexpected(NGIN::Execution::ScheduleError::Stopped);
        UInt32 count = m_handleOperations.load(std::memory_order_relaxed);
        do
        {
            if (count >= m_options.queueDepthHint)
                return std::unexpected(NGIN::Execution::ScheduleError::ResourceExhausted);
        } while (!m_handleOperations.compare_exchange_weak(count, count + 1, std::memory_order_relaxed));
        // Stop may race acquisition. Backend admission independently rejects it;
        // the gate lease only accounts for storage owned by application tasks.
        return {};
    }

    void FileSystemDriver::ReleaseHandleOperation() noexcept
    {
        const UInt32 previous = m_handleOperations.fetch_sub(1, std::memory_order_relaxed);
        assert(previous != 0);
        (void) previous;
    }

    NGIN::Execution::ScheduleResult FileSystemDriver::Execute(NGIN::Execution::WorkItem item) noexcept
    {
        using namespace NGIN::Execution;
        std::lock_guard lock(m_workerMutex);
        if (!HasBackend())
            return std::unexpected(ScheduleError::Stopped);
        if (item.IsEmpty())
            return std::unexpected(ScheduleError::Rejected);
        // Keep runtime shutdown from overtaking destruction of the worker job's
        // captured handles, even if its result was already transferred earlier.
        auto retired = m_runtime.GetExecutor().ReserveCompletion(WorkItem([] {}));
        if (!retired)
            return std::unexpected(retired.error());
        try
        {
            if (!m_scheduler)
                m_scheduler = std::make_shared<ThreadPoolScheduler>(m_options.workerThreads, m_options.queueDepthHint);
            auto job = m_scheduler->ReserveCompletion(WorkItem(
                    [work = std::move(item), terminal = std::move(*retired)]() mutable noexcept {
                        work.Invoke();
                        work = {};
                        terminal.Dispatch();
                    }));
            if (!job)
                return std::unexpected(job.error());
            job->Dispatch();
            return {};
        } catch (const std::bad_alloc&)
        {
            return std::unexpected(ScheduleError::ResourceExhausted);
        } catch (...)
        {
            return std::unexpected(ScheduleError::Rejected);
        }
    }
}// namespace NGIN::IO::detail
