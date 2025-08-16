/// @file TrackingAllocator.hpp
/// @brief Decorator allocator adding allocation statistics (current / peak / totals).
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    struct AllocationStats
    {
        std::size_t currentBytes {0};
        std::size_t peakBytes {0};
        std::size_t totalBytes {0};
        std::size_t currentCount {0};
        std::size_t totalCount {0};
    };

    template<AllocatorConcept Inner>
    class Tracking
    {
    public:
        Tracking() = default;
        explicit Tracking(Inner inner) : m_inner(std::move(inner)) {}

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

        void Deallocate(void* ptr, std::size_t size, std::size_t align) noexcept
        {
            if (ptr)
            {
                // Defensive: avoid underflow
                if (m_stats.currentBytes >= size)
                    m_stats.currentBytes -= size;
                else
                    m_stats.currentBytes = 0;
                if (m_stats.currentCount > 0)
                    m_stats.currentCount -= 1;
            }
            m_inner.Deallocate(ptr, size, align);
        }

        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            return m_inner.MaxSize();
        }
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            return m_inner.Remaining();
        }
        [[nodiscard]] bool Owns(const void* p) const noexcept
        {
            return m_inner.Owns(p);
        }

        [[nodiscard]] const AllocationStats& GetStats() const noexcept
        {
            return m_stats;
        }

        Inner& InnerAllocator() noexcept
        {
            return m_inner;
        }
        const Inner& InnerAllocator() const noexcept
        {
            return m_inner;
        }

    private:
        [[no_unique_address]] Inner m_inner {};
        AllocationStats m_stats {};
    };
}// namespace NGIN::Memory
