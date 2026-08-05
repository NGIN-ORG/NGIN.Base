/// @file Process.hpp
/// @brief Cross-platform child-process execution with explicit IO and lifetime policy.
#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/IO/ProcessError.hpp>
#include <NGIN/IO/ProcessOptions.hpp>
#include <NGIN/IO/ProcessResult.hpp>

#include <memory>

namespace NGIN::IO
{
    /// @brief Owns a running child process and provides explicit wait and termination operations.
    class NGIN_IO_API Process final
    {
    public:
        /// @brief Constructs an empty process handle.
        Process() noexcept;
        /// @brief Releases the platform process state owned by this handle.
        ~Process();

        /// @brief Transfers process ownership from another handle.
        Process(Process&& other) noexcept;
        /// @brief Replaces this handle with another process handle.
        Process& operator=(Process&& other) noexcept;

        Process(const Process&)            = delete;
        Process& operator=(const Process&) = delete;

        /// @brief Starts a child process using the supplied launch policy.
        /// @param options Launch and supervision policy transferred into the operation.
        /// @return A running process handle or a structured startup error.
        [[nodiscard]] static ProcessExpected<Process> Start(ProcessOptions options);

        /// @brief Returns whether this object owns platform process state.
        [[nodiscard]] bool IsValid() const noexcept;
        /// @brief Returns whether the owned child has not yet terminated.
        [[nodiscard]] bool IsRunning() const noexcept;
        /// @brief Waits for termination and consumes the child result.
        [[nodiscard]] ProcessExpected<ProcessResult> Wait();
        /// @brief Requests termination of the running child process tree.
        [[nodiscard]] ProcessExpected<void> Terminate();

    private:
        struct Impl;
        explicit Process(std::unique_ptr<Impl> implementation) noexcept;

        std::unique_ptr<Impl> m_implementation {};
    };

    /// @brief Starts a child process and waits synchronously for its terminal result.
    [[nodiscard]] NGIN_IO_API ProcessExpected<ProcessResult> RunProcess(ProcessOptions options);
    /// @brief Starts a child process and waits asynchronously for its terminal result.
    [[nodiscard]] NGIN_IO_API NGIN::Async::Task<ProcessResult, ProcessError>
                              RunProcessAsync(NGIN::Async::TaskContext& context, ProcessOptions options);
}// namespace NGIN::IO
