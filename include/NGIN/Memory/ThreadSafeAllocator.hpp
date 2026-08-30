#pragma once

#include <NGIN/Defines.hpp>

#include <functional>
#include <mutex>
#include <type_traits>
#include <utility>

#include <NGIN/Memory/AllocatorConcept.hpp>
namespace NGIN::Memory
{
    template<AllocatorConcept Inner, class Lockable = std::mutex>
    class ThreadSafeAllocator
    {
    public:
        /// @brief Propagates whether the wrapped allocator has definitive ownership queries.
        static constexpr bool HasPreciseOwnership = AllocatorTraits<Inner>::HasPreciseOwnershipCapability;

        /// @brief Constructs a wrapper around a default-constructed allocator.
        ThreadSafeAllocator() = default;

        /// @brief Constructs a wrapper around an allocator value.
        explicit ThreadSafeAllocator(Inner inner) : m_inner(std::move(inner)) {}

        /// @brief Allocates while holding the wrapper lock.
        [[nodiscard]] void* Allocate(std::size_t n, std::size_t a) noexcept
        {
            std::lock_guard<Lockable> guard(m_lock);
            return m_inner.Allocate(n, a);
        }
        void Deallocate(void* p, std::size_t n, std::size_t a) noexcept
        {
            std::lock_guard<Lockable> guard(m_lock);
            m_inner.Deallocate(p, n, a);
        }
        [[nodiscard]] std::size_t MaxSize() const noexcept
        {
            std::lock_guard<Lockable> guard(m_lock);
            return AllocatorTraits<Inner>::MaxSize(m_inner);
        }
        [[nodiscard]] std::size_t Remaining() const noexcept
        {
            std::lock_guard<Lockable> guard(m_lock);
            return AllocatorTraits<Inner>::Remaining(m_inner);
        }

        [[nodiscard]] Ownership OwnershipOf(const void* p) const noexcept
        {
            std::lock_guard<Lockable> guard(m_lock);
            return AllocatorTraits<Inner>::OwnershipOf(m_inner, p);
        }

        /// @brief Invokes a callback with mutable inner-allocator access while holding the lock.
        /// @details References returned by the callback must not escape the call.
        template<class Callback>
        decltype(auto) WithInner(Callback&& callback)
        {
            std::lock_guard<Lockable> guard(m_lock);
            return std::invoke(std::forward<Callback>(callback), m_inner);
        }

        /// @brief Invokes a callback with read-only inner-allocator access while holding the lock.
        /// @details References returned by the callback must not escape the call.
        template<class Callback>
        decltype(auto) WithInner(Callback&& callback) const
        {
            std::lock_guard<Lockable> guard(m_lock);
            return std::invoke(std::forward<Callback>(callback), std::as_const(m_inner));
        }

    private:
        mutable Lockable             m_lock {};
        NGIN_NO_UNIQUE_ADDRESS Inner m_inner {};
    };
}// namespace NGIN::Memory
