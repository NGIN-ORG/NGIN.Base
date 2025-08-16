/// @file AllocatorConcept.hpp
/// @brief Core allocator concept and utility traits for the modern NGIN memory system.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <concepts>

namespace NGIN::Memory
{
    // MemoryBlock already defined in legacy interface (IAllocator.hpp). New
    // allocator concepts typically return raw void*; sized metadata can reuse
    // the existing MemoryBlock where desired.

    /// @brief Core allocator concept (compile-time interface expectations).
    template<class A>
    concept AllocatorConcept = requires(A a, std::size_t n, std::size_t align, void* p) {
        { a.Allocate(n, align) } -> std::same_as<void*>;// May return nullptr on failure
        { a.Deallocate(p, n, align) } noexcept;         // size/align may be ignored
        { a.MaxSize() } -> std::same_as<std::size_t>;   // Upper bound of allocatable bytes
        { a.Remaining() } -> std::same_as<std::size_t>; // 0 or MaxSize() for unbounded
        { a.Owns(p) } -> std::convertible_to<bool>;     // Conservative ownership test
    };

    /// @brief Marker type for arenas supporting rollback semantics.
    struct ArenaMarker
    {
        void* ptr {nullptr};
    };

}// namespace NGIN::Memory
