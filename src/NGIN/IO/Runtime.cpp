#include <NGIN/IO/Runtime.hpp>

#include "RuntimeBackend.hpp"
#include "RuntimeLoop.hpp"

#include <mutex>
#include <stdexcept>

namespace NGIN::IO
{
    struct Runtime::Impl
    {
        explicit Impl(Options configured)
            : options(configured), loop({configured.submissionCapacity, configured.timerCapacity,
                                         configured.completionCapacity, configured.operationCapacity, configured.registrationCapacity, configured.batchSize},
                                        NGIN::Execution::WorkItem([this] { StopServices(); })) {}

        void StopServices() noexcept
        {
            std::shared_ptr<detail::RuntimeService> fileService;
            std::shared_ptr<detail::RuntimeService> networkService;
            {
                std::lock_guard lock(mutex);
                fileService    = files;
                networkService = network;
            }
            if (fileService)
                fileService->Stop();
            if (networkService)
                networkService->Stop();
        }

        const Options                           options;
        mutable std::mutex                      mutex;
        detail::RuntimeLoop                     loop;
        std::shared_ptr<detail::RuntimeService> files;
        std::shared_ptr<detail::RuntimeService> network;
    };

    Runtime::Runtime() : Runtime(Options {}) {}
    Runtime::Runtime(Options options)
    {
        if (options.files.backendPreference != FileBackendPreference::Auto &&
            options.files.backendPreference != FileBackendPreference::Native &&
            options.files.backendPreference != FileBackendPreference::Fallback)
            throw std::invalid_argument("Unknown I/O runtime file backend policy");
        if (options.files.workerThreads == 0 || options.files.queueDepthHint == 0)
            throw std::invalid_argument("I/O runtime requires positive file capacities");
        m_impl = std::make_unique<Impl>(options);
    }

    Runtime::~Runtime()
    {
        try
        {
            Shutdown();
        } catch (...)
        {
            std::terminate();
        }
    }
    const Runtime::Options& Runtime::GetOptions() const noexcept
    {
        return m_impl->options;
    }
    NGIN::Execution::ExecutorRef Runtime::GetExecutor() noexcept
    {
        return m_impl->loop.GetExecutor();
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
        return m_impl->files ? m_impl->files->GetFileBackend() : FileBackend::None;
    }
    Runtime::State Runtime::GetState() const noexcept
    {
        switch (m_impl->loop.GetState())
        {
            case detail::RuntimeLoop::State::Running:
                return State::Running;
            case detail::RuntimeLoop::State::Stopping:
                return State::Stopping;
            case detail::RuntimeLoop::State::Stopped:
                return State::Stopped;
        }
        std::terminate();
    }
    bool Runtime::IsStopped() const noexcept
    {
        return GetState() == State::Stopped;
    }
    void Runtime::Run()
    {
        m_impl->loop.Run();
    }
    bool Runtime::PollOnce()
    {
        return m_impl->loop.PollOnce();
    }
    void Runtime::RequestStop() noexcept
    {
        m_impl->loop.RequestStop();
    }
    void Runtime::Shutdown()
    {
        m_impl->loop.Shutdown();
    }
    std::optional<NGIN::Time::TimePoint> Runtime::NextDeadline() const noexcept
    {
        return m_impl->loop.NextDeadline();
    }
    std::intptr_t Runtime::NativeWaitHandle() const noexcept
    {
        return m_impl->loop.NativeHandle();
    }

    std::expected<std::size_t, std::error_code>
    Runtime::CopyNativeWaitSources(std::span<NativeWaitSource> destination) const noexcept
    {
        return m_impl->loop.CopyNativeWaitSources(destination);
    }

    std::shared_ptr<detail::RuntimeService> detail::RuntimeAccess::Acquire(
            Runtime& runtime, RuntimeServiceKind kind, Factory factory)
    {
        std::lock_guard lock(runtime.m_impl->mutex);
        if (runtime.GetState() != Runtime::State::Running)
            return {};
        auto& service = kind == RuntimeServiceKind::Files ? runtime.m_impl->files : runtime.m_impl->network;
        if (!service)
            service = factory(runtime);
        return service;
    }
    detail::RuntimeLoop& detail::RuntimeAccess::Loop(Runtime& runtime) noexcept
    {
        return runtime.m_impl->loop;
    }
    std::expected<NGIN::Execution::CompletionReservation, NGIN::Execution::ScheduleError>
    detail::RuntimeAccess::ReserveOperation(Runtime& runtime, NGIN::Execution::WorkItem completion) noexcept
    {
        return runtime.m_impl->loop.ReserveOperation(std::move(completion));
    }
    void detail::RuntimeAccess::Run(Runtime& runtime, NGIN::Execution::WorkItem entered)
    {
        runtime.m_impl->loop.Run(std::move(entered));
    }
}// namespace NGIN::IO
