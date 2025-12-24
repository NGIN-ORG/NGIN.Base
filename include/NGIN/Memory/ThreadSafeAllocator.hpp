#pragma once

#include <utility>
#include <mutex>

#include <NGIN/Memory/AllocatorConcept.hpp>
namespace NGIN::Memory
{
    template<AllocatorConcept Inner, class Lockable = std::mutex>
    class ThreadSafeAllocator
    {
    public:
        ThreadSafeAllocator() = default;
        explicit ThreadSafeAllocator(Inner inner) : m_inner(std::move(inner)) {}
        [[nodiscard]] void* Allocate(std::size_t n, std::size_t a) noexcept
        {
            std::lock_guard lock(m_lock);
            return m_inner.Allocate(n, a);
        }
        void Deallocate(void* p, std::size_t n, std::size_t a) noexcept
        {
            std::lock_guard lock(m_lock);
            m_inner.Deallocate(p, n, a);
        }
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            std::lock_guard lock(m_lock);
            return AllocatorTraits<Inner>::MaxSize(m_inner);
        }
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            std::lock_guard lock(m_lock);
            return AllocatorTraits<Inner>::Remaining(m_inner);
        }

        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            std::lock_guard lock(m_lock);
            return AllocatorTraits<Inner>::OwnershipOf(m_inner, p);
        }

        [[nodiscard]] bool Owns(const void* p) const noexcept
            requires AllocatorOwnsPointer<Inner>
        {
            std::lock_guard lock(m_lock);
            return m_inner.Owns(p);
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
        mutable Lockable m_lock {};
        [[no_unique_address]] Inner m_inner {};
    };
}// namespace NGIN::Memory
