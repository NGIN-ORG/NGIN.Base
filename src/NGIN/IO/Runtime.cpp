#include <NGIN/IO/Runtime.hpp>

#include "FileSystemDriver.hpp"
#include "RuntimeBackend.hpp"

#include <NGIN/Execution/Thread.hpp>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <stdexcept>

namespace NGIN::IO
{
    struct Runtime::Impl
    {
        explicit Impl(Options configured) : options(configured) {}
        Options                                             options;
        mutable std::mutex                                  mutex;
        std::mutex                                          stopMutex;
        std::condition_variable                             ready;
        bool                                                stopped {false};
        std::shared_ptr<NGIN::IO::detail::FileSystemDriver> files;
        std::shared_ptr<detail::NetworkBackend>             network;
        NGIN::Execution::Thread                             networkThread;
    };

    Runtime::Runtime() : Runtime(Options {}) {}

    Runtime::Runtime(Options options) : m_impl(std::make_unique<Impl>(options))
    {
        if ((options.network.mode != NetworkMode::Background && options.network.mode != NetworkMode::Manual) ||
            (options.files.backendPreference != FileBackendPreference::Auto &&
             options.files.backendPreference != FileBackendPreference::Native &&
             options.files.backendPreference != FileBackendPreference::Fallback))
        {
            throw std::invalid_argument("Unknown I/O runtime backend policy or network mode");
        }
        if (options.files.workerThreads == 0 || options.files.queueDepthHint == 0 ||
            !std::isfinite(options.network.pollInterval.GetValue()) || options.network.pollInterval.GetValue() < 1.0)
        {
            throw std::invalid_argument("I/O runtime requires positive file capacities and a finite network poll interval >= 1 ms");
        }
    }

    Runtime::~Runtime()
    {
        Stop();
    }
    const Runtime::Options& Runtime::GetOptions() const noexcept
    {
        return m_impl->options;
    }

    bool Runtime::HasFileBackend() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->files != nullptr;
    }

    bool Runtime::HasNetworkBackend() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->network != nullptr;
    }

    Runtime::FileBackend Runtime::GetFileBackend() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->files ? m_impl->files->GetActiveBackend() : FileBackend::None;
    }

    bool Runtime::IsStopped() const noexcept
    {
        std::lock_guard lock(m_impl->mutex);
        return m_impl->stopped;
    }

    void Runtime::Run()
    {
        if (m_impl->options.network.mode != NetworkMode::Manual)
            throw std::logic_error("Runtime::Run requires Manual network mode");
        std::unique_lock lock(m_impl->mutex);
        m_impl->ready.wait(lock, [this] { return m_impl->stopped || m_impl->network != nullptr; });
        if (m_impl->stopped)
            return;
        std::shared_ptr<detail::NetworkBackend> network = m_impl->network;
        lock.unlock();
        network->Run();
    }

    void Runtime::PollOnce()
    {
        if (m_impl->options.network.mode != NetworkMode::Manual)
            throw std::logic_error("Runtime::PollOnce requires Manual network mode");
        std::shared_ptr<detail::NetworkBackend> network;
        {
            std::lock_guard lock(m_impl->mutex);
            if (m_impl->stopped)
                return;
            network = m_impl->network;
        }
        if (network)
            network->PollOnce();
    }

    void Runtime::Stop() noexcept
    {
        std::lock_guard stopLock(m_impl->stopMutex);
        {
            std::lock_guard lock(m_impl->mutex);
            m_impl->stopped = true;
            if (m_impl->files)
                m_impl->files->Stop();
            if (m_impl->network)
                m_impl->network->Stop();
        }
        m_impl->ready.notify_all();
        if (m_impl->networkThread.IsJoinable())
            m_impl->networkThread.Join();
    }

    std::shared_ptr<NGIN::IO::detail::FileSystemDriver> detail::RuntimeAccess::Files(Runtime& runtime)
    {
        std::lock_guard lock(runtime.m_impl->mutex);
        if (runtime.m_impl->stopped)
            return {};
        if (!runtime.m_impl->files)
            runtime.m_impl->files = std::make_shared<NGIN::IO::detail::FileSystemDriver>(runtime.m_impl->options.files);
        return runtime.m_impl->files;
    }

    std::shared_ptr<detail::NetworkBackend> detail::RuntimeAccess::Network(Runtime& runtime, NetworkFactory factory)
    {
        std::lock_guard lock(runtime.m_impl->mutex);
        if (runtime.m_impl->stopped)
            return {};
        if (!runtime.m_impl->network)
        {
            std::shared_ptr<NetworkBackend> backend = factory(runtime.m_impl->options.network);
            if (runtime.m_impl->options.network.mode == Runtime::NetworkMode::Background)
            {
                NGIN::Execution::Thread::Options options;
                options.name = NGIN::Execution::ThreadName("NGIN.IO.Net");
                runtime.m_impl->networkThread.Start([backend] { backend->Run(); }, options);
            }
            runtime.m_impl->network = std::move(backend);
            runtime.m_impl->ready.notify_all();
        }
        return runtime.m_impl->network;
    }
}// namespace NGIN::IO
