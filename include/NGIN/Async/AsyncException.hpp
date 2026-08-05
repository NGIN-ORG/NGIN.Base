/// @file AsyncException.hpp
/// @brief Aggregate include for exception-based asynchronous error adapters.
#pragma once

#include <NGIN/Async/AsyncConfig.hpp>

#if NGIN_ASYNC_HAS_EXCEPTIONS
#include <NGIN/Async/AsyncCanceledException.hpp>
#include <NGIN/Async/AsyncExceptionTraits.hpp>
#include <NGIN/Async/AsyncFaultException.hpp>
#endif
