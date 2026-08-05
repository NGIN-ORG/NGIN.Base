/// @file ResolverDriver.hpp
/// @brief Explicit worker backend for asynchronous name resolution.
#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Primitives.hpp>

#include <memory>

namespace NGIN::Net
{
    /// @brief Owns the worker scheduler used by asynchronous resolver requests.
    class NGIN_NET_API ResolverDriver final
    {
    public:
        /// @brief Construction policy for the resolver worker backend.
        struct Options final
        {
            NGIN::UInt32 workerThreads {1};///< Number of blocking resolver workers.
        };

        /// @brief Constructs a resolver driver with one worker thread.
        ResolverDriver();
        /// @brief Constructs a resolver driver with explicit worker policy.
        explicit ResolverDriver(Options options);
        /// @brief Stops resolver workers and releases backend state.
        ~ResolverDriver();

        /// @brief Resolver drivers are non-copyable because they own worker state.
        ResolverDriver(const ResolverDriver&) = delete;
        /// @brief Resolver drivers are non-copy-assignable because they own worker state.
        ResolverDriver& operator=(const ResolverDriver&) = delete;
        /// @brief Resolver drivers are immovable because executor references retain scheduler state.
        ResolverDriver(ResolverDriver&&) = delete;
        /// @brief Resolver drivers are non-move-assignable because executor references retain scheduler state.
        ResolverDriver& operator=(ResolverDriver&&) = delete;

        /// @brief Returns the executor used to dispatch blocking resolver work.
        [[nodiscard]] NGIN::Execution::ExecutorRef GetExecutor() noexcept;
        /// @brief Returns whether the resolver worker backend was created successfully.
        [[nodiscard]] bool HasBackend() const noexcept;

    private:
        Options                                               m_options {};
        std::shared_ptr<NGIN::Execution::ThreadPoolScheduler> m_scheduler {};
    };
}// namespace NGIN::Net
