/// @file TrackingAllocator.hpp
/// @brief Decorator allocator adding allocation statistics (current / peak / totals).
#pragma once

#include <NGIN/Memory/AllocationStats.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <cstddef>

#include <utility>

namespace NGIN::Memory
{
    /// @brief Allocator decorator that records byte and allocation counts.
    /// @tparam Inner Allocator that performs the underlying memory operations.
    template<AllocatorConcept Inner>
    class TrackingAllocator
    {
    public:
        /// @brief Constructs the decorator around a default-constructed inner allocator.
        TrackingAllocator() = default;

        /// @brief Constructs the decorator around an existing inner allocator.
        explicit TrackingAllocator(Inner inner)
            : m_inner(std::move(inner))
        {
        }

        /// @brief Allocates memory through the inner allocator and records a successful allocation.
        [[nodiscard]] void* Allocate(std::size_t size, std::size_t align) noexcept
        {
            void* p = m_inner.Allocate(size, align);
            if (p)
            {
                m_stats.currentBytes += size;
                m_stats.totalBytes += size;
                m_stats.currentCount += 1;
                m_stats.totalCount += 1;
                if (m_stats.currentBytes > m_stats.peakBytes)
                    m_stats.peakBytes = m_stats.currentBytes;
            }
            return p;
        }

        /// @brief Releases memory through the inner allocator and updates live-allocation counters.
        void Deallocate(void* ptr, std::size_t size, std::size_t align) noexcept
        {
            if (ptr)
            {
                if (m_stats.currentBytes >= size)
                    m_stats.currentBytes -= size;
                else
                    m_stats.currentBytes = 0;
                if (m_stats.currentCount > 0)
                    m_stats.currentCount -= 1;
            }
            m_inner.Deallocate(ptr, size, align);
        }

        /// @brief Returns the maximum allocation size supported by the inner allocator.
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            return AllocatorTraits<Inner>::MaxSize(m_inner);
        }
        /// @brief Returns the remaining capacity reported by the inner allocator.
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            return AllocatorTraits<Inner>::Remaining(m_inner);
        }

        /// @brief Classifies whether a pointer belongs to the inner allocator.
        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            return AllocatorTraits<Inner>::OwnershipOf(m_inner, p);
        }

        /// @brief Returns whether the inner allocator owns a pointer when that operation is available.
        [[nodiscard]] bool Owns(const void* p) const noexcept
            requires AllocatorOwnsPointer<Inner>
        {
            return m_inner.Owns(p);
        }

        /// @brief Returns the current allocation counters.
        [[nodiscard]] const AllocationStats& GetStats() const noexcept
        {
            return m_stats;
        }

        /// @brief Returns mutable access to the wrapped allocator.
        Inner& InnerAllocator() noexcept
        {
            return m_inner;
        }
        /// @brief Returns read-only access to the wrapped allocator.
        const Inner& InnerAllocator() const noexcept
        {
            return m_inner;
        }

    private:
        [[no_unique_address]] Inner m_inner {};
        AllocationStats             m_stats {};
    };

}// namespace NGIN::Memory
