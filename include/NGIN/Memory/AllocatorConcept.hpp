/// @file AllocatorConcept.hpp
/// @brief Core allocator concepts and traits for the modern NGIN memory system.
#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <concepts>
#include <limits>

namespace NGIN::Memory
{
    // -------------------------------------------------------------------------
    // MemoryBlock: Optional rich allocation result for extended allocators
    // -------------------------------------------------------------------------

    class MemoryBlock
    {
    public:
        using Pointer = void*;

        Pointer        PointerValue {nullptr};// Base pointer to the allocated block
        std::size_t    SizeInBytes {0};       // Granted size in bytes (0 if unknown)
        std::size_t    AlignmentInBytes {0};  // Actual alignment met (0 if unknown)
        std::uintptr_t Cookie {0};            // Optional routing or debugging tag

        MemoryBlock() = default;

        MemoryBlock(Pointer p, std::size_t size, std::size_t alignment = 0, std::uintptr_t cookie = 0) noexcept
            : PointerValue(p), SizeInBytes(size), AlignmentInBytes(alignment), Cookie(cookie) {}

        explicit operator bool() const noexcept { return PointerValue != nullptr; }

        template<class T>
        T* As() const noexcept
        {
            return static_cast<T*>(PointerValue);
        }
    };

    // -------------------------------------------------------------------------
    // Core allocator concept (minimal, hot-path friendly)
    // -------------------------------------------------------------------------
    //
    // Intentionally small: only Allocate/Deallocate are required.
    // Size and alignment parameters to Deallocate may be ignored by implementations.

    template<class A>
    concept AllocatorConcept =
            requires(A a, std::size_t n, std::size_t align, void* p) {
                { a.Allocate(n, align) } -> std::same_as<void*>;// May return nullptr on failure
                { a.Deallocate(p, n, align) } noexcept;         // Size/align may be ignored
            };

    // -------------------------------------------------------------------------
    // Optional capabilities (detected with concepts)
    // -------------------------------------------------------------------------

    template<class A>
    concept AllocatorOwnsPointer =
            requires(const A a, const void* p) {
                { a.Owns(p) } -> std::convertible_to<bool>;
            };

    template<class A>
    concept AllocatorReportsMaxSize =
            requires(const A a) {
                { a.MaxSize() } -> std::same_as<std::size_t>;
            };

    template<class A>
    concept AllocatorReportsRemainingBytes =
            requires(const A a) {
                { a.Remaining() } -> std::same_as<std::size_t>;
            };

    // Extended allocation result: AllocateEx returning MemoryBlock
    template<class A>
    concept ExtendedAllocatorConcept =
            requires(A a, std::size_t n, std::size_t align) {
                { a.AllocateEx(n, align) } -> std::same_as<MemoryBlock>;
            };

    // -------------------------------------------------------------------------
    // Traits with safe defaults (prefer these instead of hard dependencies)
    // -------------------------------------------------------------------------

    template<class A>
    struct AllocatorTraits
    {
        static constexpr bool HasOwnsPointerCapability        = AllocatorOwnsPointer<A>;
        static constexpr bool HasMaxSizeCapability            = AllocatorReportsMaxSize<A>;
        static constexpr bool HasRemainingBytesCapability     = AllocatorReportsRemainingBytes<A>;
        static constexpr bool HasExtendedAllocationCapability = ExtendedAllocatorConcept<A>;

        // Provide MaxSize with a conservative default
        static std::size_t MaxSize(const A& allocator) noexcept
        {
            if constexpr (HasMaxSizeCapability)
            {
                return allocator.MaxSize();
            }
            else
            {
                return std::numeric_limits<std::size_t>::max();
            }
        }

        // Provide Remaining with a conservative default
        static std::size_t Remaining(const A& allocator) noexcept
        {
            if constexpr (HasRemainingBytesCapability)
            {
                return allocator.Remaining();
            }
            else
            {
                // Unknown or unbounded; mirror MaxSize by default
                return MaxSize(allocator);
            }
        }

        // Provide Owns with a conservative default (assume "maybe")
        static bool Owns(const A& allocator, const void* pointer) noexcept
        {
            if constexpr (HasOwnsPointerCapability)
            {
                return allocator.Owns(pointer);
            }
            else
            {
                return true;// Conservative default useful for debug assertions and tooling
            }
        }

        // Unified extended allocation: prefer AllocateEx when available, otherwise synthesize a MemoryBlock
        static MemoryBlock AllocateEx(A& allocator, std::size_t sizeInBytes, std::size_t alignmentInBytes) noexcept
        {
            if constexpr (HasExtendedAllocationCapability)
            {
                return allocator.AllocateEx(sizeInBytes, alignmentInBytes);
            }
            else
            {
                void* ptr = allocator.Allocate(sizeInBytes, alignmentInBytes);
                // Size and alignment are reported as "requested" when the allocator does not support extended info.
                return MemoryBlock(ptr, ptr ? sizeInBytes : 0, alignmentInBytes, 0);
            }
        }
    };

    // -------------------------------------------------------------------------
    // Arena marker for allocators supporting rollback semantics
    // -------------------------------------------------------------------------

    struct ArenaMarker
    {
        void* ptr {nullptr};
    };

}// namespace NGIN::Memory
