#include <NGIN/IO/FileSystemDriver.hpp>

#include "NativeFileSystemBackend.hpp"

namespace NGIN::IO::detail
{
    NativeFileBackend* GetNativeFileBackend(FileSystemDriver& driver) noexcept
    {
        return driver.m_nativeBackend.get();
    }

    const NativeFileBackend* GetNativeFileBackend(const FileSystemDriver& driver) noexcept
    {
        return driver.m_nativeBackend.get();
    }
}// namespace NGIN::IO::detail

namespace NGIN::IO
{
    FileSystemDriver::FileSystemDriver()
        : FileSystemDriver(Options {})
    {
    }

    FileSystemDriver::FileSystemDriver(Options options)
        : m_options(std::move(options))
    {
        m_scheduler = std::make_shared<NGIN::Execution::ThreadPoolScheduler>(
                static_cast<std::size_t>(m_options.workerThreads == 0 ? 1 : m_options.workerThreads));

        if (m_options.backendPreference != BackendPreference::Fallback)
        {
            m_nativeBackend = detail::CreateNativeFileBackend(m_options);
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
}// namespace NGIN::IO
