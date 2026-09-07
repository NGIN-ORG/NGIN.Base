/// @file Runtime.hpp
/// @brief Application-owned, lazily initialized filesystem and network I/O services.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Units.hpp>

#include <memory>

namespace NGIN::IO
{
    namespace detail
    {
        struct RuntimeAccess;
    }

    /// @brief Owns the backends that make filesystem and socket operations progress.
    /// @note Resources borrow this runtime. It must outlive them and all their operations.
    /// TaskContext independently selects the executor used for coroutine continuations.
    /// Construction and resource binding start no workers. Backends initialize on first async use.
    class NGIN_IO_API Runtime final
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
        /// @brief Background owns a network thread; Manual requires Run() or PollOnce().
        enum class NetworkMode : UInt8
        {
            Background,
            Manual
        };

        /// @brief File backend selection and positive worker/queue capacities.
        struct FileOptions
        {
            UInt32                workerThreads {1};
            UInt32                queueDepthHint {1024};
            FileBackendPreference backendPreference {FileBackendPreference::Auto};
        };

        /// @brief Network polling ownership and maximum polling wait interval.
        struct NetworkOptions
        {
            NetworkMode               mode {NetworkMode::Background};
            NGIN::Units::Milliseconds pollInterval {1.0};
        };

        /// @brief Independent configuration for the lazily initialized services.
        struct Options
        {
            FileOptions    files {};
            NetworkOptions network {};
        };

        /// @brief Constructs an idle runtime with default options.
        Runtime();
        /// @brief Stores configuration without initializing either backend.
        /// @throws std::invalid_argument if worker capacity or poll interval is invalid.
        explicit Runtime(Options options);
        /// @brief Stops polling and joins the owned thread. Await all operations first.
        ~Runtime();
        Runtime(const Runtime&)            = delete;
        Runtime& operator=(const Runtime&) = delete;
        Runtime(Runtime&&)                 = delete;
        Runtime& operator=(Runtime&&)      = delete;

        /// @brief Returns immutable construction options.
        [[nodiscard]] const Options& GetOptions() const noexcept;
        /// @brief Returns whether the file service has been initialized.
        [[nodiscard]] bool HasFileBackend() const noexcept;
        /// @brief Returns whether the network service has been initialized.
        [[nodiscard]] bool HasNetworkBackend() const noexcept;
        /// @brief Returns the selected file backend without initializing it.
        [[nodiscard]] FileBackend GetFileBackend() const noexcept;
        /// @brief Returns whether Stop() has permanently disabled new async work.
        [[nodiscard]] bool IsStopped() const noexcept;

        /// @brief In Manual mode, waits for network work and polls until Stop().
        /// @throws std::logic_error in Background mode. Use only one polling thread.
        void Run();
        /// @brief Performs one nonblocking network poll in Manual mode; no-op before first use.
        /// @throws std::logic_error in Background mode. Do not race Run() or another PollOnce().
        void PollOnce();
        /// @brief Permanently rejects new async work, stops polling, and joins the owned thread.
        /// Thread-safe and idempotent. Cancel and await pending operations before calling;
        /// this is not cancellation or a task-draining API. Externally owned Run() threads
        /// must be joined by their caller. Synchronous resource operations remain usable.
        void Stop() noexcept;

    private:
        friend struct detail::RuntimeAccess;
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::IO
