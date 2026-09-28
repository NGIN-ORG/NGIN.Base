/// @file CurrentEnvironment.hpp
/// @brief Access to the current computation's logical environment.
#pragma once

#include <NGIN/Async/AsyncEnvironment.hpp>
#include <coroutine>

namespace NGIN::Async
{
    namespace detail
    {
        struct CurrentEnvironmentAwaiter final
        {
            const AsyncEnvironment* environment {};
            bool                    await_ready() const noexcept { return false; }
            template<typename Promise>
            bool await_suspend(std::coroutine_handle<Promise> awaiting) noexcept
            {
                environment = &awaiting.promise().m_environment;
                return false;
            }
            AsyncEnvironment await_resume() const noexcept { return *environment; }
        };
    }// namespace detail

    /// Returns an allocation-free snapshot without suspending or scheduling.
    [[nodiscard]] inline detail::CurrentEnvironmentAwaiter CurrentEnvironment() noexcept
    {
        return {};
    }
}// namespace NGIN::Async
