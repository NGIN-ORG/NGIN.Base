#pragma once

#include <NGIN/Memory/AllocationStats.hpp>

namespace NGIN::Benchmarks::Allocations
{
    // Process-wide replacement-new counters. Requested C++ bytes only: excludes
    // allocator bookkeeping, this instrument's headers, C allocation, and kernel
    // storage. Take comparable snapshots after application work has joined.
    NGIN::Memory::AllocationStats Snapshot() noexcept;
    // Call while producers are quiescent; concurrent peaks during reset are not
    // a defined measurement window. Allocation/free tracking is thread-safe.
    void ResetPeak() noexcept;
}// namespace NGIN::Benchmarks::Allocations
