/// @file Runtime.hpp
/// @brief Application-owned event loop for I/O, timers, and continuations.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/IO/NativeWaitSource.hpp>
#include <NGIN/Primitives.hpp>

#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <optional>
#include <span>
#include <system_error>

namespace NGIN::IO
{
    namespace detail
    {
        struct RuntimeAccess;
    }

    /// @brief Owns one externally driven event loop and lazily initialized I/O services.
    /// @details Construction starts no threads. Resources borrow this runtime;
    /// it must outlive them and all their operations. TaskContext may select the
    /// runtime executor or an independently owned external executor.
    class NGIN_IORUNTIME_API Runtime final
    {
    public:
        /// @brief Native requires a native file backend; Auto permits worker fallback.
        enum class FileBackendPreference : UInt8
        {
            Auto,
            Native,
            Fallback
        };
        /// @brief Selected file backend, or None before initialization / if unavailable.
        enum class FileBackend : UInt8
        {
            None,
            NativeIoUring,
            NativeIocp,
            WorkerFallback
        };
        /// @brief Stop rejects new work before draining accepted completions.
        enum class State : UInt8
        {
            Running,
            Stopping,
            Stopped
        };

        /// @brief File backend selection and positive worker/queue capacities.
        struct FileOptions
        {
            UInt32 workerThreads {1};
            /// @brief Bounds handle operations (queued plus active), worker jobs,
            /// and native requests as separate service-wide budgets.
            UInt32                queueDepthHint {1024};
            FileBackendPreference backendPreference {FileBackendPreference::Auto};
        };
        /// @brief Positive memory/admission limits and maximum callbacks per poll.
        /// @details Completion storage is reserved separately from ordinary work.
        /// A callback cannot be preempted by batchSize; move blocking or expensive
        /// processing to an external executor.
        struct Options
        {
            FileOptions files {};
            std::size_t submissionCapacity {4096};
            std::size_t timerCapacity {4096};
            std::size_t completionCapacity {8192};
            std::size_t operationCapacity {4096};
            std::size_t registrationCapacity {4096};
            std::size_t batchSize {64};
        };

        Runtime();
        /// @throws std::invalid_argument for invalid policies or zero capacities.
        /// @throws std::system_error when the platform wait source cannot initialize.
        explicit Runtime(Options options);
        /// @brief Completes shutdown under the same fixed thread-ownership rules.
        /// @details A manually driven runtime must be shut down on its owner
        /// before destruction on another thread. Ownership violations terminate.
        ~Runtime();
        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(Runtime&&)      = delete;

        [[nodiscard]] const Options& GetOptions() const noexcept;
        /// @brief Thread-safe queued executor; borrowed references must not outlive the runtime.
        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() noexcept;
        [[nodiscard]] bool                         HasFileBackend() const noexcept;
        [[nodiscard]] bool                         HasNetworkBackend() const noexcept;
        [[nodiscard]] FileBackend                  GetFileBackend() const noexcept;
        [[nodiscard]] State                        GetState() const noexcept;
        [[nodiscard]] bool                         IsStopped() const noexcept;

        /// @brief Drives the whole runtime until shutdown finishes, sleeping when idle.
        /// @throws std::logic_error for concurrent/reentrant driving or a different owner thread.
        /// The first Run, PollOnce, Shutdown, or RuntimeRunner binds ownership permanently.
        void Run();
        /// @brief Processes one nonblocking bounded batch and reports immediately ready backlog.
        /// @details Continue polling while true before returning to a host wait.
        /// False is a snapshot; new work signals the platform wait source.
        bool PollOnce();
        /// @brief Nonblocking, thread-safe stop request, including from runtime callbacks.
        /// @details Rejects new I/O and ordinary work, wakes the loop, and drains
        /// previously reserved completions on the owner. Does not join unrelated
        /// application tasks on external executors.
        void RequestStop() noexcept;
        /// @brief Requests stop and waits for runtime-owned work to drain.
        /// @details An unbound runtime binds to the caller. The owner may drive
        /// shutdown; another thread may wait for an active Run/RuntimeRunner.
        /// @throws std::logic_error inside a callback, or off-owner without a committed driver.
        /// Shutdown after Stopped is an idempotent no-op.
        void Shutdown();

        /// @brief Monotonic next-timer snapshot for an embedding application loop.
        [[nodiscard]] std::optional<NGIN::Time::TimePoint> NextDeadline() const noexcept;
        /// @brief Copies the POSIX host wait snapshot without allocating or consuming readiness.
        /// @return Number of sources written, no_buffer_space without writing if the
        /// span is too small, or operation_not_supported on Windows (use RuntimeRunner).
        /// @details Thread-safe snapshot. Allocate registrationCapacity + 1 elements once for portable poll;
        /// epoll/kqueue need one. Pump PollOnce until false, copy sources, then read
        /// NextDeadline and wait for any source or that deadline. Monitor errors/hangup
        /// too. Refresh after every notification, including descriptor invalidation.
        /// Concurrent registration/deadline changes signal the included wake source.
        /// Sources are borrowed snapshots: never read, close, or dispatch them yourself.
        [[nodiscard]] std::expected<std::size_t, std::error_code>
        CopyNativeWaitSources(std::span<NativeWaitSource> destination) const noexcept;
        /// @brief Native epoll/kqueue descriptor or IOCP handle; -1 for portable poll.
        /// @details Borrowed: do not read, close, or dequeue packets from it. IOCP is
        /// not a general Win32 wait handle; Windows hosts should use RuntimeRunner.
        [[nodiscard]] std::intptr_t NativeWaitHandle() const noexcept;

    private:
        friend struct detail::RuntimeAccess;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::IO
