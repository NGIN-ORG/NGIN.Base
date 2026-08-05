/// @file NoError.hpp
/// @brief Domain-error marker for asynchronous operations that cannot report a typed error.
#pragma once

namespace NGIN::Async
{
    /// @brief Empty domain-error type used by tasks with no recoverable typed failure.
    struct NoError final
    {
    };
}// namespace NGIN::Async
