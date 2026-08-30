/// @file AllocatorRef.hpp
/// @brief Non-owning reference wrapper that adapts an allocator instance to `AllocatorConcept`.
#pragma once

#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    template<class A>
    class AllocatorRef
    {
    public:
        /// @brief Reports whether the referenced allocator has definitive ownership queries.
        static constexpr bool HasPreciseOwnership = AllocatorTraits<A>::HasPreciseOwnershipCapability;

        /// @brief Creates a non-owning reference to an allocator.
        /// @param allocator Allocator that must outlive this reference and its allocations.
        explicit AllocatorRef(A& allocator) noexcept
            : m_allocator(&allocator)
        {
        }

        /// @brief Allocates through the referenced allocator.
        [[nodiscard]] void* Allocate(std::size_t size, std::size_t alignment) noexcept
        {
            return m_allocator->Allocate(size, alignment);
        }

        /// @brief Deallocates through the referenced allocator.
        void Deallocate(void* pointer, std::size_t size, std::size_t alignment) noexcept
        {
            m_allocator->Deallocate(pointer, size, alignment);
        }

        // Optional capabilities, forward if present:
        std::size_t MaxSize() const noexcept
        {
            return AllocatorTraits<A>::MaxSize(*m_allocator);
        }
        std::size_t Remaining() const noexcept
        {
            return AllocatorTraits<A>::Remaining(*m_allocator);
        }

        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            return AllocatorTraits<A>::OwnershipOf(*m_allocator, p);
        }

    private:
        A* m_allocator;
    };
}// namespace NGIN::Memory
