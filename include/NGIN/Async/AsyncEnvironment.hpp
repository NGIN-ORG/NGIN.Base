/// @file AsyncEnvironment.hpp
/// @brief Logical properties inherited by an asynchronous computation.
#pragma once

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Execution/Config.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Time/TimePoint.hpp>

#include <memory_resource>
#include <optional>

namespace NGIN::Async
{
    /// Copies retain existing cancellation state without registering callbacks or
    /// linking tokens. The executor, resource, and metadata are borrowed and must
    /// outlive computations using them. A deadline is monotonic, not a duration.
    struct NGIN_EXECUTION_API AsyncEnvironment final
    {
        NGIN::Execution::ExecutorRef         executor {};
        CancellationToken                    cancellation {};
        std::optional<NGIN::Time::TimePoint> deadline {};
        std::pmr::memory_resource*           resource {std::pmr::get_default_resource()};
        const void*                          metadata {};
        std::optional<int>                   priority {};

        /// @throws std::invalid_argument if the allocation resource is null.
        void Validate() const;
    };
}// namespace NGIN::Async
