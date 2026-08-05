/// @file AllocationStats.hpp
/// @brief Allocation counters collected by instrumentation allocators.
#pragma once

#include <cstddef>

namespace NGIN::Memory
{
    /// @brief Tracks current, peak, and lifetime allocation volume.
    struct AllocationStats
    {
        std::size_t currentBytes {0}; ///< Bytes currently owned by live allocations.
        std::size_t peakBytes {0};    ///< Highest observed current byte count.
        std::size_t totalBytes {0};   ///< Cumulative bytes returned by successful allocations.
        std::size_t currentCount {0}; ///< Number of currently live allocations.
        std::size_t totalCount {0};   ///< Cumulative number of successful allocations.
    };
}// namespace NGIN::Memory
