/// @file FallbackAllocator.hpp
/// @brief Allocator that tries a primary allocator then falls back to secondary.
#pragma once

#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{
    template<AllocatorConcept Primary, AllocatorConcept Secondary>
    class FallbackAllocator
    {
    public:
        FallbackAllocator() = default;
        FallbackAllocator(Primary p, Secondary s) : m_primary(std::move(p)), m_secondary(std::move(s)) {}

        [[nodiscard]] void* Allocate(std::size_t n, std::size_t a) noexcept
        {
            if (void* p = m_primary.Allocate(n, a))
                return p;
            return m_secondary.Allocate(n, a);
        }
        void Deallocate(void* ptr, std::size_t n, std::size_t a) noexcept
        {
            if (m_primary.Owns(ptr))
                m_primary.Deallocate(ptr, n, a);
            else
                m_secondary.Deallocate(ptr, n, a);
        }
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            return m_primary.MaxSize() + m_secondary.MaxSize();
        }
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            return m_primary.Remaining() + m_secondary.Remaining();
        }
        [[nodiscard]] bool Owns(const void* p) const noexcept
        {
            return m_primary.Owns(p) || m_secondary.Owns(p);
        }

    private:
        [[no_unique_address]] Primary m_primary {};
        [[no_unique_address]] Secondary m_secondary {};
    };
}// namespace NGIN::Memory
