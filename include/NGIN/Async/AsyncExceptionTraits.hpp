/// @file AsyncExceptionTraits.hpp
/// @brief Customization point for translating typed asynchronous errors into exceptions.
#pragma once

#include <NGIN/Async/AsyncDomainErrorException.hpp>

#if NGIN_ASYNC_HAS_EXCEPTIONS
namespace NGIN::Async
{
    /// @brief Default adapter that throws AsyncDomainErrorException for a typed domain error.
    /// @tparam E Domain error type translated by the adapter.
    template<typename E>
    struct AsyncExceptionTraits
    {
        /// @brief Throws an exception containing the supplied domain error.
        /// @param error Domain error copied into the exception.
        [[noreturn]] static void Throw(const E& error)
        {
            throw AsyncDomainErrorException<E>(error);
        }
    };

    /// @brief Backward-compatible name for AsyncExceptionTraits.
    template<typename E>
    using AsyncExceptionAdapter = AsyncExceptionTraits<E>;
}// namespace NGIN::Async
#endif
