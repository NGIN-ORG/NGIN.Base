#pragma once
#include <utility>
#include <mutex>

#include <NGIN/Memory/AllocatorConcept.hpp>
namespace NGIN::Memory
{
    template<AllocatorConcept Inner>
    class ThreadSafeAllocator
    {
    public:
        ThreadSafeAllocator() = default;
        explicit ThreadSafeAllocator(Inner inner) : m_inner(std::move(inner)) {}
        [[nodiscard]] void* Allocate(std::size_t n, std::size_t a) noexcept
        {
            std::lock_guard lock(mutex_);
            return m_inner.Allocate(n, a);
        }
        void Deallocate(void* p, std::size_t n, std::size_t a) noexcept
        {
            std::lock_guard lock(mutex_);
            m_inner.Deallocate(p, n, a);
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
        Inner& InnerAllocator() noexcept
        {
            return m_inner;
        }
        const Inner& InnerAllocator() const noexcept
        {
            return m_inner;
        }

    private:
        mutable std::mutex mutex_;
        [[no_unique_address]] Inner m_inner {};
    };
}// namespace NGIN::Memory