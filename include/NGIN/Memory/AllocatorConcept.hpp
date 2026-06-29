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
    /// @brief Describes whether an allocator can prove ownership of a pointer.
    /// @details Ownership queries are intentionally tri-state. Allocators that cannot
    /// answer ownership precisely should report @ref Ownership::Unknown rather than
    /// guessing, because routing a deallocation to the wrong allocator is unsafe.
    enum class Ownership : std::uint8_t
    {
        /// @brief The allocator owns the queried pointer.
        Owns,
        /// @brief The allocator can prove that it does not own the queried pointer.
        DoesNotOwn,
        /// @brief The allocator cannot determine pointer ownership.
        Unknown,
    };

    /// @brief Rich allocation result returned by allocators with extended reporting.
    /// @details The core allocator contract only requires a raw pointer. Extended
    /// allocators may return a MemoryBlock to expose granted size, effective alignment,
    /// or an implementation-defined cookie used for diagnostics or deallocation routing.
    class MemoryBlock
    {
    public:
        using Pointer = void*;

        /// @brief Base pointer to the allocated block, or nullptr when allocation failed.
        Pointer ptr {nullptr};
        /// @brief Granted block size in bytes, or 0 when unknown or allocation failed.
        std::size_t SizeInBytes {0};
        /// @brief Effective alignment in bytes, or 0 when the allocator does not report it.
        std::size_t AlignmentInBytes {0};
        /// @brief Optional implementation-defined value for routing, validation, or diagnostics.
        std::uintptr_t Cookie {0};

        MemoryBlock() = default;

        /// @brief Creates a reported allocation block.
        /// @param p Base pointer returned by the allocator.
        /// @param size Granted size in bytes, or 0 when unknown.
        /// @param alignment Effective alignment in bytes, or 0 when unknown.
        /// @param cookie Optional allocator-defined routing or diagnostic value.
        MemoryBlock(Pointer p, std::size_t size, std::size_t alignment = 0, std::uintptr_t cookie = 0) noexcept
            : ptr(p), SizeInBytes(size), AlignmentInBytes(alignment), Cookie(cookie) {}

        /// @brief Returns true when this block contains a non-null allocation pointer.
        explicit operator bool() const noexcept { return ptr != nullptr; }

        /// @brief Returns the block pointer cast to @p T.
        /// @tparam T Target object type for the pointer view.
        template<class T>
        T* As() const noexcept
        {
            return static_cast<T*>(ptr);
        }
    };

    /// @brief Minimal allocator interface used by NGIN containers and memory utilities.
    /// @details The concept is deliberately small for hot paths: implementations must
    /// provide allocation and non-throwing deallocation only. @c Allocate may return
    /// nullptr on failure. @c Deallocate receives the original size and alignment when
    /// callers know them, but allocators that do not need that information may ignore it.
    template<class A>
    concept AllocatorConcept =
            requires(A a, std::size_t n, std::size_t align, void* p) {
                { a.Allocate(n, align) } -> std::same_as<void*>;
                { a.Deallocate(p, n, align) } noexcept;
            };

    /// @brief Detects allocators that can report whether they own a pointer.
    /// @details A positive result enables safe deallocation routing in composite
    /// allocators. Absence of this capability is treated as unknown ownership.
    template<class A>
    concept AllocatorOwnsPointer =
            requires(const A a, const void* p) {
                { a.Owns(p) } -> std::convertible_to<bool>;
            };

    /// @brief Detects allocators that expose an upper bound for allocation size.
    template<class A>
    concept AllocatorReportsMaxSize =
            requires(const A a) {
                { a.MaxSize() } -> std::same_as<std::size_t>;
            };

    /// @brief Detects allocators that can report currently available bytes.
    template<class A>
    concept AllocatorReportsRemainingBytes =
            requires(const A a) {
                { a.Remaining() } -> std::same_as<std::size_t>;
            };

    /// @brief Detects allocators that return rich allocation metadata from AllocateEx.
    template<class A>
    concept ExtendedAllocatorConcept =
            requires(A a, std::size_t n, std::size_t align) {
                { a.AllocateEx(n, align) } -> std::same_as<MemoryBlock>;
            };

    /// @brief Capability adapter for allocator implementations.
    /// @details Prefer this traits layer when consuming optional allocator features.
    /// It centralizes conservative defaults so containers can use richer allocators
    /// without hard-requiring every allocator to implement every query.
    template<class A>
    struct AllocatorTraits
    {
        /// @brief True when @p A provides @c Owns(const void*).
        static constexpr bool HasOwnsPointerCapability        = AllocatorOwnsPointer<A>;
        /// @brief True when @p A provides @c MaxSize().
        static constexpr bool HasMaxSizeCapability            = AllocatorReportsMaxSize<A>;
        /// @brief True when @p A provides @c Remaining().
        static constexpr bool HasRemainingBytesCapability     = AllocatorReportsRemainingBytes<A>;
        /// @brief True when @p A provides @c AllocateEx(size, alignment).
        static constexpr bool HasExtendedAllocationCapability = ExtendedAllocatorConcept<A>;

        /// @brief Returns the allocator's maximum supported allocation size.
        /// @details Allocators without @c MaxSize() are treated as unbounded from the
        /// caller's perspective and return the maximum representable @c std::size_t.
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

        /// @brief Returns currently available bytes when the allocator can report them.
        /// @details Allocators without @c Remaining() fall back to @ref MaxSize. This
        /// means "unknown or effectively unbounded" rather than a precise availability
        /// measurement.
        static std::size_t Remaining(const A& allocator) noexcept
        {
            if constexpr (HasRemainingBytesCapability)
            {
                return allocator.Remaining();
            }
            else
            {
                return MaxSize(allocator);
            }
        }

        /// @brief Returns a tri-state ownership answer for @p pointer.
        /// @details When @p A has no ownership query, this returns @ref Ownership::Unknown.
        /// Callers must not treat @ref Ownership::Unknown as ownership for deallocation
        /// routing decisions.
        static Ownership OwnershipOf(const A& allocator, const void* pointer) noexcept
        {
            if constexpr (HasOwnsPointerCapability)
            {
                return allocator.Owns(pointer) ? Ownership::Owns : Ownership::DoesNotOwn;
            }
            else
            {
                return Ownership::Unknown;
            }
        }

        /// @brief Returns true only when the allocator positively reports ownership.
        /// @details Unknown ownership maps to false, making this safe for boolean routing
        /// checks that must avoid accidental deallocation through the wrong allocator.
        static bool Owns(const A& allocator, const void* pointer) noexcept
        {
            return OwnershipOf(allocator, pointer) == Ownership::Owns;
        }

        /// @brief Allocates memory and returns a MemoryBlock regardless of allocator richness.
        /// @details Allocators with @c AllocateEx are queried directly. Minimal allocators
        /// are adapted by calling @c Allocate and reporting the requested size/alignment
        /// when allocation succeeds.
        static MemoryBlock AllocateEx(A& allocator, std::size_t sizeInBytes, std::size_t alignmentInBytes) noexcept
        {
            if constexpr (HasExtendedAllocationCapability)
            {
                return allocator.AllocateEx(sizeInBytes, alignmentInBytes);
            }
            else
            {
                void* ptr = allocator.Allocate(sizeInBytes, alignmentInBytes);
                return MemoryBlock(ptr, ptr ? sizeInBytes : 0, alignmentInBytes, 0);
            }
        }
    };

    /// @brief Default propagation policy for allocator-aware value types.
    /// @details Allocator implementations may specialize this trait to match their
    /// ownership model. The default follows a move-friendly policy: move assignment
    /// and swap propagate, copy assignment does not, and empty allocators are considered
    /// always equal.
    template<class A>
    struct AllocatorPropagationTraits
    {
        /// @brief Whether allocator-aware types should copy-assign the allocator.
        static constexpr bool PropagateOnCopyAssignment = false;
        /// @brief Whether allocator-aware types should move-assign the allocator.
        static constexpr bool PropagateOnMoveAssignment = true;
        /// @brief Whether allocator-aware types should swap allocator instances.
        static constexpr bool PropagateOnSwap           = true;

        /// @brief Whether all instances of @p A can deallocate each other's allocations.
        static constexpr bool IsAlwaysEqual = std::is_empty_v<A>;
    };

    /// @brief Opaque rollback marker for arena-style allocators.
    /// @details Arena allocators can use this as a lightweight checkpoint token.
    /// The pointer value is allocator-defined and should only be interpreted by the
    /// allocator that produced it.
    struct ArenaMarker
    {
        /// @brief Allocator-defined checkpoint pointer.
        void* ptr {nullptr};
    };

}// namespace NGIN::Memory
