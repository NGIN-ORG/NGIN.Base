/// @file RuntimeRunner.hpp
/// @brief Explicit RAII ownership of one runtime event-loop thread.
#pragma once

#include <NGIN/Defines.hpp>
#include <memory>

namespace NGIN::IO
{
    class Runtime;

    /// @brief Drives a borrowed runtime on one owned thread until shutdown.
    /// @details Construction waits until loop ownership is established. A runtime
    /// already driven by another thread cannot be transferred to a runner.
    /// The runtime must outlive the runner; external executors must remain alive
    /// until their application tasks have joined.
    class NGIN_IORUNTIME_API RuntimeRunner final
    {
    public:
        /// @throws std::logic_error if runtime ownership conflicts with this thread.
        /// @throws std::system_error if the thread cannot be started.
        explicit RuntimeRunner(Runtime& runtime);
        /// @brief Requests shutdown and joins the owned thread. Never call from its callbacks.
        ~RuntimeRunner();
        RuntimeRunner(const RuntimeRunner&)            = delete;
        RuntimeRunner& operator=(const RuntimeRunner&) = delete;
        RuntimeRunner(RuntimeRunner&&)                 = delete;
        RuntimeRunner& operator=(RuntimeRunner&&)      = delete;

        /// @brief Requests shutdown, joins once, and reports any event-loop failure.
        /// @details Call from one controlling thread. The destructor performs the
        /// same operation and terminates if shutdown cannot complete safely.
        void Shutdown();

    private:
        struct Impl;
        std::unique_ptr<Impl> m_impl;
    };
}// namespace NGIN::IO
